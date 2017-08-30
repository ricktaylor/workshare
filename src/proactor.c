/*
 * proactor.c
 *
 *  Created on: 25 Aug 2017
 *      Author: rick
 */

#include "common.h"
#include "task.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <realtimeapiset.h>
#endif

#include "proactor.h"

#if defined(_WIN32)
typedef ULONG nfds_t;
#else
#define closesocket(s) close(s)
#endif

struct Watcher;

struct Timer
{
	uint64_t        m_deadline;
	struct Watcher* m_watcher;
	task_t          m_parent;
	task_fn_t       m_fn;
	uint32_t        m_repeat;
	unsigned int    m_id;
	unsigned int    m_param_len;
	char            m_param[TASK_PARAM_MAX];
};

struct Watcher
{
	struct Timer* m_timer;
	task_t        m_parent;
	task_fn_t     m_fn;
	unsigned int  m_param_len;
	char          m_param[TASK_PARAM_MAX];
};

struct Proactor
{
	task_t          m_task;
	struct pollfd*  m_poll_fds;
	struct Watcher* m_watchers;
	size_t          m_poll_alloc_size;
	struct Timer*   m_timers;
	size_t          m_timer_count;
	size_t          m_timer_alloc_size;
	nfds_t          m_n_poll_fds;
	unsigned int    m_control_offset;
	unsigned char   m_control_buf[1024];

	socket_t        m_control_fd;
	atomic_uint     m_next_timer_id;
};

enum ProactorCommands
{
	CMD_ADD_TIMER,
	CMD_CANCEL_TIMER,
	CMD_UPDATE_TIMER,

	CMD_ADD_RECV_WATCHER,
	CMD_ADD_RECV_T_WATCHER,
	CMD_CANCEL_RECV_WATCHER,
	CMD_ADD_SEND_WATCHER,
	CMD_ADD_SEND_T_WATCHER,
	CMD_CANCEL_SEND_WATCHER,
};

static uint64_t timeNow()
{
#if defined(_WIN32)
	ULONGLONG ulTime;
	QueryUnbiasedInterruptTime(&ulTime); // 100ns intervals, 1e7 per second
	return ulTime / 10000;
#else
	struct timespec t = {1,0};
	clock_gettime(CLOCK_MONOTONIC,&t);
	return (uint64_t)t.tv_sec * 1000 +  t.tv_nsec / 1000000;
#endif
}

static struct Timer* proactorInsertTimer(struct Proactor* pr, uint64_t deadline)
{
	size_t start = 0;
	for (size_t end = pr->m_timer_count;start < end;)
	{
		while (start < end && pr->m_timers[start].m_deadline == 0)
			++start;

		while (start < end && pr->m_timers[end].m_deadline == 0)
			--end;

		size_t mid = start + (end - start) / 2;
		while (start < mid && pr->m_timers[mid].m_deadline == 0)
			--mid;

		if (pr->m_timers[mid].m_deadline > deadline)
			start = mid + 1;
		else
			end = mid;
	}

	// Insert at 'start'
	if (start == pr->m_timer_count || pr->m_timers[start].m_deadline != 0)
	{
		// Find the next gap
		size_t gap = start + 1;
		while (gap < pr->m_timer_count && pr->m_timers[gap].m_deadline == 0)
			++gap;

		if (gap >= pr->m_timer_count)
		{
			// Need to grow
			if (gap >= pr->m_timer_alloc_size)
			{
				// Need to realloc
				size_t new_size = pr->m_timer_alloc_size * 2;
				struct Timer* new_timers = realloc(pr->m_timers,new_size);
				if (!new_timers)
					abort();

				pr->m_timers = new_timers;
				pr->m_timer_alloc_size = new_size;
			}
			gap = pr->m_timer_count++;
		}

		// Shuffle down
		while (gap > start)
		{
			pr->m_timers[gap] = pr->m_timers[gap-1];
			--gap;

			// Fix up watcher
			if (pr->m_timers[gap].m_watcher)
				pr->m_timers[gap].m_watcher->m_timer = &pr->m_timers[gap];
		}
	}
	pr->m_timers[start].m_deadline = deadline;
	return &pr->m_timers[start];
}

static void proactorReorderTimer(struct Proactor* pr, struct Timer* t)
{
	if (t > pr->m_timers && (t-1)->m_deadline < t->m_deadline)
	{
		struct Timer t_old = *t;
		t->m_deadline = 0;
		struct Timer* t_new = proactorInsertTimer(pr,t_old.m_deadline);
		*t_new = t_old;
		if (t_new->m_watcher)
			t_new->m_watcher->m_timer = t_new;
	}
}

#define READ_ARG(D,P) \
	do { P = (void*)(((uintptr_t)P + _Alignof(D) - 1) & ~(_Alignof(D) - 1)); \
		memcpy(&D,P,sizeof(D)); \
		P += sizeof(D); \
	} while (0)

#define WRITE_ARG(S,P) \
	do { P = (void*)(((uintptr_t)P + _Alignof(S) - 1) & ~(_Alignof(S) - 1)); \
		memcpy(P,&S,sizeof(S)); \
		P += sizeof(S); \
	} while (0)

static void proactorWriteControl(struct Proactor* pr, const unsigned char* msg)
{
#if defined(_WIN32)
	int w = send(pr->m_control_fd,(void*)msg,msg[1],0);
#else
	ssize_t w;
	do
	{
		w = send(pr->m_control_fd,msg,msg[1],0);
	}
	while (w == -1 && errno == EINTR);
#endif
	if (w != msg[1])
		abort();
}

static struct Timer* proactorAddTimer(struct Proactor* pr, unsigned char* p)
{
	uint64_t deadline;
	READ_ARG(deadline,p);

	struct Timer* t = proactorInsertTimer(pr,deadline);
	t->m_watcher = NULL;
	READ_ARG(t->m_fn,p);
	READ_ARG(t->m_id,p);
	READ_ARG(t->m_parent,p);
	READ_ARG(t->m_repeat,p);
	READ_ARG(t->m_param_len,p);
	if (t->m_param_len)
		memcpy(t->m_param,p,t->m_param_len);
	return t;
}

static unsigned char* proactorSendAddTimer(struct Proactor* pr, unsigned char* p, unsigned int id, uint32_t timeout, uint32_t repeat, task_t pt, task_fn_t fn, const void* param, unsigned int param_len)
{
	uint64_t deadline = timeNow() + timeout;
	WRITE_ARG(deadline,p);
	WRITE_ARG(fn,p);
	WRITE_ARG(id,p);
	WRITE_ARG(pt,p);
	WRITE_ARG(repeat,p);
	WRITE_ARG(param_len,p);
	if (param_len)
	{
		memcpy(p,param,param_len);
		p += param_len;
	}

	return p;
}

unsigned int proactor_add_timer(proactor_t ph, uint32_t timeout, uint32_t repeat, task_t pt, task_fn_t fn, const void* param, unsigned int param_len)
{
	struct Proactor* pr = (struct Proactor*)ph;

	unsigned int id = 0;
	while (id == 0)
		id = atomic_fetch_add(&pr->m_next_timer_id,1);

	unsigned char msg[sizeof(struct Timer) + 8] = { CMD_ADD_TIMER };
	static_assert(sizeof(msg) < 256, "Message buffer size > 255");

	unsigned char* p = proactorSendAddTimer(pr,msg + 2,id,timeout,repeat,pt,fn,param,param_len);

	msg[1] = (p - msg);
	assert(p - msg <= sizeof(msg));
	proactorWriteControl(pr,msg);
	return id;
}

static void proactorCancelTimer(struct Proactor* pr, unsigned char* p)
{
	unsigned int id;
	READ_ARG(id,p);

	for (size_t i = 0; i < pr->m_timer_count; ++i)
	{
		if (pr->m_timers[i].m_id == id)
		{
			if (pr->m_timers[i].m_deadline != 0)
			{
				if (pr->m_timers[i].m_watcher)
					pr->m_timers[i].m_watcher->m_timer = NULL;

				if (i == pr->m_timer_count - 1)
					--pr->m_timer_count;
				else
					pr->m_timers[i].m_deadline = 0;
			}
			break;
		}
	}
}

void proactor_cancel_timer(proactor_t ph, unsigned int timer_id)
{
	struct Proactor* pr = (struct Proactor*)ph;

	unsigned char msg[16] = { CMD_CANCEL_TIMER };
	unsigned char* p = msg + 2;

	WRITE_ARG(timer_id,p);

	msg[1] = (p - msg);
	assert(p - msg <= sizeof(msg));
	proactorWriteControl(pr,msg);
}

static void proactorUpdateTimer(struct Proactor* pr, unsigned char* p)
{
	unsigned int id;
	READ_ARG(id,p);

	for (size_t i = 0; i < pr->m_timer_count; ++i)
	{
		if (pr->m_timers[i].m_id == id)
		{
			if (pr->m_timers[i].m_deadline != 0)
			{
				READ_ARG(pr->m_timers[i].m_deadline,p);
				READ_ARG(pr->m_timers[i].m_repeat,p);

				pr->m_timers[i].m_deadline = timeNow() + pr->m_timers[i].m_repeat;
				proactorReorderTimer(pr,&pr->m_timers[i]);
			}
			break;
		}
	}
}

void proactor_update_timer(proactor_t ph, unsigned int timer_id, uint32_t timeout, uint32_t repeat)
{
	struct Proactor* pr = (struct Proactor*)ph;

	unsigned char msg[32] = { CMD_UPDATE_TIMER };
	unsigned char* p = msg + 2;

	WRITE_ARG(timer_id,p);
	uint64_t deadline = timeNow() + timeout;
	WRITE_ARG(deadline,p);
	WRITE_ARG(repeat,p);

	msg[1] = (p - msg);
	assert(p - msg <= sizeof(msg));
	proactorWriteControl(pr,msg);
}

static struct Watcher* proactorAddWatcher(struct Proactor* pr, unsigned char** p, unsigned int flags)
{
	socket_t fd;
	READ_ARG(fd,p);

	size_t i = 1;
	for (; i < pr->m_n_poll_fds; ++i)
	{
		if (pr->m_poll_fds[i].fd == fd)
			break;
	}

	if (i == pr->m_n_poll_fds)
	{
		if (i == pr->m_poll_alloc_size)
		{
			// Realloc
			size_t new_size = pr->m_poll_alloc_size * 2;
			struct pollfd* new_poll_fds = realloc(pr->m_poll_fds,new_size * sizeof(struct pollfd));
			if (!new_poll_fds)
				abort();
			pr->m_poll_fds = new_poll_fds;

			struct Watcher* new_watchers = realloc(pr->m_watchers,new_size * sizeof(struct Watcher) * 2);
			if (!new_watchers)
				abort();
			pr->m_watchers = new_watchers;
			pr->m_poll_alloc_size = new_size;
		}

		++pr->m_n_poll_fds;
		pr->m_poll_fds[i].fd = fd;
		pr->m_poll_fds[i].events = 0;
	}

	assert(!(pr->m_poll_fds[i].events & flags));
	pr->m_poll_fds[i].events |= flags;

	struct Watcher* w = &pr->m_watchers[i * 2];
	if (flags & POLLWRNORM)
		++w;

	w->m_timer = NULL;
	READ_ARG(w->m_parent,p);
	READ_ARG(w->m_fn,p);
	READ_ARG(w->m_param_len,p);
	if (w->m_param_len)
		memcpy(w->m_param,p,w->m_param_len);

	return w;
}

static unsigned char* proactorSendAddWatcher(struct Proactor* pr, unsigned char* p, socket_t fd, task_t pt, task_fn_t fn, const void* param, unsigned int param_len)
{
	WRITE_ARG(fd,p);
	WRITE_ARG(pt,p);
	WRITE_ARG(fn,p);
	WRITE_ARG(param_len,p);
	if (param_len)
	{
		memcpy(p,param,param_len);
		p += param_len;
	}

	return p;
}

static void proactorSendSimpleAddWatcher(enum ProactorCommands op_code, proactor_t ph, socket_t fd, task_t pt, task_fn_t fn, const void* param, unsigned int param_len)
{
	struct Proactor* pr = (struct Proactor*)ph;

	unsigned char msg[sizeof(struct Watcher) + 8] = { op_code };
	static_assert(sizeof(msg) < 256, "Message buffer size > 255");

	unsigned char* p = proactorSendAddWatcher(pr,msg + 2,fd,pt,fn,param,param_len);

	msg[1] = (p - msg);
	assert(p - msg <= sizeof(msg));
	proactorWriteControl(pr,msg);
}

void proactor_add_recv_watcher(proactor_t ph, socket_t fd, task_t pt, task_fn_t fn, const void* param, unsigned int param_len)
{
	proactorSendSimpleAddWatcher(CMD_ADD_RECV_WATCHER,ph,fd,pt,fn,param,param_len);
}

void proactor_add_send_watcher(proactor_t ph, socket_t fd, task_t pt, task_fn_t fn, const void* param, unsigned int param_len)
{
	proactorSendSimpleAddWatcher(CMD_ADD_SEND_WATCHER,ph,fd,pt,fn,param,param_len);
}

static void proactorAddTimedWatcher(struct Proactor* pr, unsigned char* p, unsigned int flags)
{
	struct Watcher* watcher = proactorAddWatcher(pr,&p,flags);

	watcher->m_timer = proactorAddTimer(pr,p);
	watcher->m_timer->m_param_len = watcher->m_param_len;
	if (watcher->m_param_len)
		memcpy(watcher->m_timer->m_param,watcher->m_param,watcher->m_param_len);

	watcher->m_timer->m_watcher = watcher;
}

static void proactorSendTimedAddWatcher(enum ProactorCommands op_code, proactor_t ph, socket_t fd, uint32_t timeout, task_t pt, task_fn_t io_fn, task_fn_t tmo_fn, const void* param, unsigned int param_len)
{
	struct Proactor* pr = (struct Proactor*)ph;

	unsigned char msg[sizeof(struct Watcher) + sizeof(struct Timer) + 16 - TASK_PARAM_MAX] = { op_code };
	static_assert(sizeof(msg) < 256, "Message buffer size > 255");

	unsigned char* p = proactorSendAddWatcher(pr,msg + 2,fd,pt,io_fn,param,param_len);

	unsigned int id = 0;
	while (id == 0)
		id = atomic_fetch_add(&pr->m_next_timer_id,1);

	p = proactorSendAddTimer(pr,p,id,timeout,0,pt,tmo_fn,NULL,0);

	msg[1] = (p - msg);
	assert(p - msg <= sizeof(msg));
	proactorWriteControl(pr,msg);
}

void proactor_add_timed_recv_watcher(proactor_t ph, socket_t fd, uint32_t timeout, task_t pt, task_fn_t io_fn, task_fn_t tmo_fn, const void* param, unsigned int param_len)
{
	proactorSendTimedAddWatcher(CMD_ADD_RECV_T_WATCHER,ph,fd,timeout,pt,io_fn,tmo_fn,param,param_len);
}

void proactor_add_timed_send_watcher(proactor_t ph, socket_t fd, uint32_t timeout, task_t pt, task_fn_t io_fn, task_fn_t tmo_fn, const void* param, unsigned int param_len)
{
	proactorSendTimedAddWatcher(CMD_ADD_SEND_T_WATCHER,ph,fd,timeout,pt,io_fn,tmo_fn,param,param_len);
}

static void proactorRemoveWatcher(struct Proactor* pr, size_t i)
{
	// Swap array member with last member
	if (--pr->m_n_poll_fds > 1 && i < pr->m_n_poll_fds)
	{
		struct Watcher* w = &pr->m_watchers[i*2];

		pr->m_poll_fds[i] = pr->m_poll_fds[pr->m_n_poll_fds];
		memcpy(w,&pr->m_watchers[pr->m_n_poll_fds*2],2 * sizeof(struct Watcher));

		// Fix up timers
		if (w->m_timer)
			w->m_timer->m_watcher = w;

		++w;
		if (w->m_timer)
			w->m_timer->m_watcher = w;
	}
}

static void proactorCancelWatcher(struct Proactor* pr, unsigned char* p, unsigned int flags)
{
	socket_t fd;
	READ_ARG(fd,p);

	size_t i = 1;
	for (; i < pr->m_n_poll_fds; ++i)
	{
		if (pr->m_poll_fds[i].fd == fd)
		{
			if (pr->m_poll_fds[i].events & flags)
			{
				pr->m_poll_fds[i].events &= ~flags;

				struct Watcher* watcher = &pr->m_watchers[i * 2];
				if (flags & POLLWRNORM)
					++watcher;

				if (watcher->m_timer)
					watcher->m_timer->m_deadline = 0;

				if (!pr->m_poll_fds[i].events)
					proactorRemoveWatcher(pr,i);
			}
			break;
		}
	}
}

void proactorSendCancelWatcher(enum ProactorCommands op_code, proactor_t ph, socket_t fd)
{
	struct Proactor* pr = (struct Proactor*)ph;

	unsigned char msg[32] = { op_code };
	unsigned char* p = msg + 2;

	WRITE_ARG(fd,p);

	msg[1] = (p - msg);
	assert(p - msg <= sizeof(msg));
	proactorWriteControl(pr,msg);
}

void proactor_cancel_recv_watcher(proactor_t ph, socket_t fd)
{
	proactorSendCancelWatcher(CMD_CANCEL_SEND_WATCHER,ph,fd);
}

void proactor_cancel_send_watcher(proactor_t ph, socket_t fd)
{
	proactorSendCancelWatcher(CMD_CANCEL_SEND_WATCHER,ph,fd);
}

static void proactorReadControl(struct Proactor* pr)
{
	for (;;)
	{
#if defined(_WIN32)
		int r = recv(pr->m_poll_fds[0].fd,(void*)pr->m_control_buf + pr->m_control_offset,sizeof(pr->m_control_buf) - pr->m_control_offset,0);
		if (r == -1 && WSAGetLastError() == WSAEWOULDBLOCK)
			break;
#else
		ssize_t r;
		do
			r = recv(pr->m_poll_fds[0].fd,pr->m_control_buf + pr->m_control_offset,sizeof(pr->m_control_buf) - pr->m_control_offset,0);
		while (r == -1 && errno == EINTR);

		if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			break;
#endif
		if (r == 0)
		{
			// Control fd has closed, this means exit!
			assert(pr->m_control_offset == 0);
			assert(pr->m_n_poll_fds == 1);

			closesocket(pr->m_poll_fds[0].fd);
			pr->m_n_poll_fds = 0;
			break;
		}

		if (r < 0)
			abort();

		pr->m_control_offset += r;
		size_t p = 0;
		for (; p + 2 <= pr->m_control_offset && pr->m_control_offset >= p + pr->m_control_buf[p+1]; p += pr->m_control_buf[p+1])
		{
			switch (pr->m_control_buf[p])
			{
			case CMD_ADD_TIMER:
				proactorAddTimer(pr,&pr->m_control_buf[p+2]);
				break;

			case CMD_CANCEL_TIMER:
				proactorCancelTimer(pr,&pr->m_control_buf[p+2]);
				break;

			case CMD_UPDATE_TIMER:
				proactorUpdateTimer(pr,&pr->m_control_buf[p+2]);
				break;

			case CMD_ADD_RECV_WATCHER:
				{
					unsigned char* pp = &pr->m_control_buf[p+2];
					proactorAddWatcher(pr,&pp,POLLRDNORM);
					break;
				}

			case CMD_ADD_RECV_T_WATCHER:
				proactorAddTimedWatcher(pr,&pr->m_control_buf[p+2],POLLRDNORM);
				break;

			case CMD_CANCEL_RECV_WATCHER:
				proactorCancelWatcher(pr,&pr->m_control_buf[p+2],POLLRDNORM);
				break;

			case CMD_ADD_SEND_WATCHER:
				{
					unsigned char* pp = &pr->m_control_buf[p+2];
					proactorAddWatcher(pr,&pp,POLLWRNORM);
					break;
				}

			case CMD_ADD_SEND_T_WATCHER:
				proactorAddTimedWatcher(pr,&pr->m_control_buf[p+2],POLLWRNORM);
				break;

			case CMD_CANCEL_SEND_WATCHER:
				proactorCancelWatcher(pr,&pr->m_control_buf[p+2],POLLWRNORM);
				break;

			default:
				abort();
			}
		}

		if (p < pr->m_control_offset)
			memmove(pr->m_control_buf,pr->m_control_buf + p,pr->m_control_offset - p);
		pr->m_control_offset -= p;
	}
}

static int proactorPoll(struct Proactor* pr, int timeout)
{
	int ret;
#if defined(_WIN32)
	ret = WSAPoll(pr->m_poll_fds,pr->m_n_poll_fds,timeout);
	if (ret == -1 && WSAGetLastError() == WSAENOBUFS)
		ret = WSAPoll(pr->m_poll_fds,pr->m_n_poll_fds / 2,timeout);
#else
	do
		ret = poll(pr->m_poll_fds,pr->m_n_poll_fds,timeout);
	while (ret == -1 && (errno == EINTR || errno == EAGAIN));

	if (ret == -1 && errno == ENOMEM)
	{
		do
			ret = poll(pr->m_poll_fds,pr->m_n_poll_fds / 2,timeout);
		while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	}
#endif
	if (ret == -1)
		abort();

	return ret;
}

static void proactorRun(task_t task, void* param)
{
	for (struct Proactor* pr = *(struct Proactor* const*)param; pr->m_n_poll_fds > 0; )
	{
		// Loop through timers, firing off expired tasks, back to front!
		uint64_t tNow = timeNow();
		size_t i;
		for (i = pr->m_timer_count; i-- > 0 && pr->m_timers[i].m_deadline <= tNow;)
		{
			if (pr->m_timers[i].m_deadline)
			{
				task_run(pr->m_timers[i].m_parent,pr->m_timers[i].m_fn,pr->m_timers[i].m_param,pr->m_timers[i].m_param_len);

				if (pr->m_timers[i].m_repeat != 0)
				{
					pr->m_timers[i].m_deadline = tNow + pr->m_timers[i].m_repeat;
					pr->m_timer_count = i;
					proactorReorderTimer(pr,&pr->m_timers[i]);
					i = pr->m_timer_count;
				}
			}
		}
		pr->m_timer_count = i + 1;

		// Poll
		int timeout = -1;
		if (pr->m_timer_count)
			timeout = pr->m_timers[pr->m_timer_count-1].m_deadline - tNow;

		int fds = proactorPoll(pr,timeout);

		// Check watchers
		for (i = 0; i < pr->m_n_poll_fds && fds > 0; ++i)
		{
			if (pr->m_poll_fds[i].revents)
			{
				--fds;
				if (i == 0)
				{
					// Do the message pipe i/o
					proactorReadControl(pr);
					continue;
				}

				struct Watcher* rd_watcher = &pr->m_watchers[i*2];
				if ((pr->m_poll_fds[i].events & POLLRDNORM) && (pr->m_poll_fds[i].revents & (POLLERR | POLLHUP | POLLRDNORM)))
				{
					// Run the read task
					task_run(rd_watcher->m_parent,rd_watcher->m_fn,rd_watcher->m_param,rd_watcher->m_param_len);

					if (rd_watcher->m_timer)
						rd_watcher->m_timer->m_deadline = 0;

					pr->m_poll_fds[i].events &= ~POLLRDNORM;
				}

				if ((pr->m_poll_fds[i].events & POLLWRNORM) && (pr->m_poll_fds[i].revents & (POLLERR | POLLWRNORM)))
				{
					// Run the write task
					struct Watcher* wr_watcher = rd_watcher+1;
					task_run(wr_watcher->m_parent,wr_watcher->m_fn,wr_watcher->m_param,wr_watcher->m_param_len);

					if (wr_watcher->m_timer)
						wr_watcher->m_timer->m_deadline = 0;

					pr->m_poll_fds[i].events &= ~POLLWRNORM;
				}

				if (!pr->m_poll_fds[i].events)
				{
					proactorRemoveWatcher(pr,i);

					// Check i again
					--i;
				}
			}
		}
	}
}

static int proactorSocketPair(socket_t* fd1, socket_t* fd2)
{
	int err = 0;
#if defined(_WIN32)
	// TODO: Use a loopback TCP socket
#else
	int fds[2] = {-1, -1};
	err = socketpair(AF_UNIX,SOCK_STREAM | SOCK_CLOEXEC,0,fds);
	if (!err)
	{
		// Set fd2 to O_NONBLOCK
		int flags = fcntl(fds[1], F_GETFL, 0);
		if (flags == -1)
			err = errno
		else
		{
			flags |= O_NONBLOCK;
			err = fcntl(fds[1], F_SETFL, flags);
		}

		if (err)
		{
			socketclose(fds[0]);
			socketclose(fds[1]);
		}
		else
		{
			*fd1 = fds[0];
			*fd2 = fds[1];
		}
	}
#endif
	return err;
}

proactor_t proactor_create(task_t parent)
{
	struct Proactor* pr = malloc(sizeof(struct Proactor));
	if (!pr)
		abort();

	pr->m_task = NULL;
	pr->m_timer_alloc_size = 0;
	pr->m_timers = NULL;
	pr->m_timer_count = 0;
	pr->m_control_offset = 0;
	atomic_store(&pr->m_next_timer_id,1);

	pr->m_poll_alloc_size = 4;
	pr->m_n_poll_fds = 1;
	pr->m_poll_fds = malloc(pr->m_poll_alloc_size * sizeof(struct pollfd));
	if (!pr->m_poll_fds)
		abort();

	pr->m_watchers = malloc(pr->m_poll_alloc_size * 2 * sizeof(struct Watcher));
	if (!pr->m_watchers)
		abort();

	// Create socket_pair
	socket_t control_fd = -1;
	if (proactorSocketPair(&pr->m_control_fd,&control_fd) != 0)
		abort();

	// Add control i/o watcher
	pr->m_poll_fds[0].fd = control_fd;
	pr->m_poll_fds[0].events = POLLRDNORM;

	// Kick off a task to run it
	pr->m_task = task_run(parent,&proactorRun,&pr,sizeof(pr));

	return (proactor_t)pr;
}

void proactor_destroy(proactor_t pt)
{
	struct Proactor* pr = (struct Proactor*)pt;
	if (pr)
	{
		// Close the control socket
		closesocket(pr->m_control_fd);
		task_join(pr->m_task);
		free(pr->m_watchers);
		free(pr->m_poll_fds);
		free(pr);
	}
}
