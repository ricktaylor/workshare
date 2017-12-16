
#include "threads.h"
#include "task.h"

#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>

#if defined(__MINGW32__)
static inline void* aligned_alloc(size_t alignment, size_t size)
{
	return _aligned_malloc(size,alignment);
}
#define aligned_free _aligned_free
#else
#define aligned_free free
#endif

#if defined(_WIN64) || (__WORDSIZE == 64)
#define THREAD_BITS 8
#else
#define THREAD_BITS 5
#endif
#define MAX_THREADS (1 << (THREAD_BITS))

struct TaskParts
{
	uintptr_t thread : THREAD_BITS;
	uintptr_t generation : 8;
	uintptr_t offset : ((sizeof(uintptr_t)*8) - 8 - THREAD_BITS);
};

union TaskPun
{
	struct TaskParts parts;
	task_t           task;
};

static_assert(sizeof(task_t) == sizeof(struct TaskParts),"TaskParts is incorrect!");

struct Task
{
	task_fn_t    m_fn;
	struct Task* m_parent;
	task_t       m_handle;
	
	atomic_uint  m_active;
	unsigned int m_generation;

	_Alignas(max_align_t) char m_data[64];
};

// Aim for a round number of 64 bytes - the common L1 line width
#define TASK_SIZE  ((sizeof(struct Task) + 63) & (~63))

// Check our #define is valid
static_assert(TASK_SIZE - sizeof(struct Task) == 32,"struct Task alignment is wrong");

// Aim for 32 Kb - about the size of L1 cache
#define TASK_COUNT (32 * 1024 / TASK_SIZE)

// Check we have enough bits in task_t
static_assert(UINT64_C(1) << ((sizeof(uintptr_t)*8) - 8 - THREAD_BITS) >= TASK_COUNT,"TASK_COUNT too low");

struct Scheduler;

struct ThreadInfo
{
	struct Scheduler* m_scheduler;
		
	thrd_t     m_thread_id;
	
	atomic_int m_top;
	atomic_int m_bottom;
		
	uint32_t     m_rng;
	unsigned int m_free_task;
	
	unsigned m_close : 1;
			
	struct Task*  m_pool;
	struct Task** m_deque;
};

struct Scheduler
{
	unsigned int m_threads;

	atomic_int   m_status;
	sema_t       m_sema;

	struct ThreadInfo m_thread_info[];
};

static tss_t s_thread_info;
static once_flag s_task_once = ONCE_FLAG_INIT;

static void schedulerSignal(struct Scheduler* s)
{
	int old_status = atomic_load_explicit(&s->m_status,memory_order_relaxed);
	int new_status;

	do
	{
		new_status = old_status < 1 ? old_status + 1 : 1;
	}
	while (!atomic_compare_exchange_weak_explicit(&s->m_status,&old_status,new_status,memory_order_release,memory_order_relaxed));

	if (old_status < 0)
	{
		if (sema_signal(&s->m_sema,1) != thrd_success)
			abort();
	}
}

static void schedulerWait(struct Scheduler* s)
{
	if (atomic_fetch_sub_explicit(&s->m_status,1,memory_order_acquire) < 1)
	{
		if (sema_wait(&s->m_sema) != thrd_success)
			abort();
	}
}

// See http://www.di.ens.fr/~zappa/readings/ppopp13.pdf for details
static struct Task* taskPop(struct ThreadInfo* info) 
{
	struct Task* task = NULL;
	
	int b = atomic_load_explicit(&info->m_bottom,memory_order_relaxed) - 1;
	atomic_store_explicit(&info->m_bottom,b,memory_order_relaxed);
	
	atomic_thread_fence(memory_order_seq_cst);
	
	int t = atomic_load_explicit(&info->m_top,memory_order_relaxed);
	if (t <= b)
	{
		/* Non-empty queue. */
		task = atomic_load_explicit(&info->m_deque[b % TASK_COUNT],memory_order_relaxed);
		if (t == b) 
		{
			/* Single last element in queue. */
			if (!atomic_compare_exchange_strong_explicit(&info->m_top,&t,t+1,memory_order_seq_cst,memory_order_relaxed))
			{
				/* Failed race. */
				task = NULL;
			}
			atomic_store_explicit(&info->m_bottom,b+1,memory_order_relaxed);
		}
	} 
	else 
	{
		/* Empty queue. */
		atomic_store_explicit(&info->m_bottom,b+1,memory_order_relaxed);
	}
	
	return task;
}

static int taskPush(struct ThreadInfo* info, struct Task* task) 
{
	int b = atomic_load_explicit(&info->m_bottom,memory_order_relaxed);
	int t = atomic_load_explicit(&info->m_top,memory_order_acquire);
	
	if (b - t >= TASK_COUNT) 
	{ 
		/* Full queue. */
		return 0;
	}
	
	atomic_store_explicit(&info->m_deque[b % TASK_COUNT],task,memory_order_relaxed);
	
	atomic_thread_fence(memory_order_release);
	
	atomic_store_explicit(&info->m_bottom,b+1,memory_order_relaxed);
	
	return 1;
}

static struct Task* taskSteal(struct ThreadInfo* info)
{
	struct Task* task = NULL;
	
	int t = atomic_load_explicit(&info->m_top,memory_order_acquire);
	
	atomic_thread_fence(memory_order_seq_cst);
	
	int b = atomic_load_explicit(&info->m_bottom,memory_order_acquire);
	if (t < b)
	{
		/* Non-empty queue. */
		task = atomic_load_explicit(&info->m_deque[t % TASK_COUNT],memory_order_relaxed);
		if (!atomic_compare_exchange_strong_explicit(&info->m_top,&t,t+1,memory_order_seq_cst,memory_order_relaxed))
		{
			/* Failed race. */
			task = NULL;
		}
	}
	
	return task;
}

static void taskFinish(struct Task* task)
{
	if (atomic_fetch_sub_explicit(&task->m_active,1,memory_order_acq_rel) == 1)
	{
		if (task->m_parent)
			taskFinish(task->m_parent);
	}
}

static uint32_t xorshift(uint32_t v)
{
	v ^= v << 13;
	v ^= v >> 17;
	v ^= v << 5;
	return v;
}

static int taskRunNext(struct ThreadInfo* info)
{
	struct Task* task = taskPop(info);
	if (!task)
	{
		// Pick a random other thread
		struct ThreadInfo* other_info = info;
		while (other_info == info)
		{
			// This doesn't need to be better than xorshift
			info->m_rng = xorshift(info->m_rng);
			
			other_info = &info->m_scheduler->m_thread_info[info->m_rng % info->m_scheduler->m_threads];
		}
		
		task = taskSteal(other_info);
	}
	
	if (task)
	{
		(*task->m_fn)(task->m_handle,task->m_data);
		taskFinish(task);
		return 1;
	}
	return 0;
}

static struct Task* taskAllocate(struct ThreadInfo* info)
{
	struct Task* task = NULL;
	union TaskPun p = { .parts = {0} };
	for (unsigned int i = 0; i < info->m_scheduler->m_threads; ++i)
	{
		if (info == &info->m_scheduler->m_thread_info[i])
		{
			p.parts.thread = i;
			break;
		}
	}	
	
	for (unsigned int i = 0; i < TASK_COUNT; ++i)
	{
		p.parts.offset = (info->m_free_task++) % TASK_COUNT;
		task = &info->m_pool[p.parts.offset];
		if (atomic_load_explicit(&task->m_active,memory_order_relaxed) == 0)
		{
			atomic_store_explicit(&task->m_active,1,memory_order_relaxed);
			
			p.parts.generation = ++task->m_generation;
			if (!p.parts.generation)
				p.parts.generation = ++task->m_generation;
			
			task->m_handle = p.task;
			break;
		}
	}
	
	return task;
}

static struct Task* taskDeref(struct ThreadInfo* info, task_t t)
{
	struct Task* task = NULL;
	union TaskPun p = { .task = t };

	if (p.parts.offset < TASK_COUNT && p.parts.thread < info->m_scheduler->m_threads)
	{
		task = &info->m_scheduler->m_thread_info[p.parts.thread].m_pool[p.parts.offset];
		if (task->m_handle != t)
			task = NULL;
	}
	return task;
}

static struct ThreadInfo* get_thread_info()
{
	return tss_get(s_thread_info);
}

void task_join(task_t handle)
{
	struct ThreadInfo* info = get_thread_info();
	struct Task* task = taskDeref(info,handle);
	
	while (task && atomic_load_explicit(&task->m_active,memory_order_relaxed) != 0)
		taskRunNext(info);
}

task_t task_run(task_t pt, task_fn_t fn, const void* param, unsigned int param_len)
{
	if (!fn || param_len > TASK_PARAM_MAX)
	{
		errno = EINVAL;
		return NULL;
	}
	
	struct ThreadInfo* info = get_thread_info();
	struct Task* parent = NULL;
	if (pt && !(parent = taskDeref(info,pt)))
	{
		errno = EINVAL;
		return NULL;
	}
	
	struct Task* task = NULL;
	while (!(task = taskAllocate(info)))
	{
		// Out of pool space for this thread
		taskRunNext(info);
	}
	
	task->m_fn = fn;
	memcpy(task->m_data,param,param_len);
	task->m_parent = parent;
	
	if (task->m_parent)
		atomic_fetch_add_explicit(&parent->m_active,1,memory_order_relaxed);
	
	while (!taskPush(info,task))
	{
		// Out of deque space for this thread
		taskRunNext(info);
	}

	schedulerSignal(info->m_scheduler);

	return task->m_handle;
}

static int schedulerThread(void* p)
{
	struct ThreadInfo* info = p;
	
	tss_set(s_thread_info,info);

	while (!info->m_close)
	{
		if (!taskRunNext(info))
			schedulerWait(info->m_scheduler);
	}
		
	return 0;
}

void scheduler_destroy(scheduler_t sc)
{
	struct Scheduler* s = (struct Scheduler*)sc;
	struct ThreadInfo* info = get_thread_info();

	if (s && info && info->m_scheduler == s)
	{
		for (unsigned int i = 0; i < s->m_threads; ++i)
			s->m_thread_info[i].m_close = 1;

		if (sema_signal(&s->m_sema,s->m_threads) != thrd_success)
			abort();

		while (s->m_threads-- > 0)
		{
			info = &s->m_thread_info[s->m_threads];

			if (!thrd_equal(info->m_thread_id,thrd_current()))
			{
				if (thrd_join(info->m_thread_id,NULL) != thrd_success)
					abort();
			}

			free(info->m_deque);
			aligned_free(info->m_pool);
		}
		
		sema_destroy(&s->m_sema);

		free(s);
	}
}

static void init_tss()
{
	tss_create(&s_thread_info,NULL);
}

scheduler_t scheduler_create(unsigned int threads)
{
	if (threads < 2)
		threads = 2;
	else if (threads > MAX_THREADS)
		threads = MAX_THREADS;

	call_once(&s_task_once,&init_tss);
	
	struct Scheduler* s = malloc(sizeof(struct Scheduler) + (threads * sizeof(struct ThreadInfo)));
	if (!s)
		abort();

	atomic_store(&s->m_status,0);

	if (sema_init(&s->m_sema,0) != thrd_success)
		abort();

	for (s->m_threads = 0; s->m_threads  < threads; ++s->m_threads)
	{
		// Init ThreadInfo
		struct ThreadInfo* info = &s->m_thread_info[s->m_threads];

		info->m_scheduler = s;
		info->m_rng = xorshift((uintptr_t)info);
		info->m_close = 0;
		info->m_bottom = info->m_top = 0;
		info->m_free_task = 0;

		info->m_pool = aligned_alloc(64,TASK_COUNT * TASK_SIZE);
		if (!info->m_pool)
			abort();

		memset(info->m_pool,0,TASK_COUNT * TASK_SIZE);

		info->m_deque = calloc(TASK_COUNT,sizeof(struct Task*));
		if (!info->m_deque)
			abort();

		if (s->m_threads == 0)
		{
			info->m_thread_id = thrd_current();
			tss_set(s_thread_info,info);
		}
		else if (thrd_create(&info->m_thread_id,&schedulerThread,info) != thrd_success)
			abort();
	}
	
	return (scheduler_t)s;
}
