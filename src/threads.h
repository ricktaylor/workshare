#ifndef SRC_THREADS_H_
#define SRC_THREADS_H_

#if defined(_WIN32)

#include <windows.h>
#include <process.h>

enum
{
	thrd_success = 0,
	thrd_error = ERROR_INVALID_HANDLE,
	thrd_nomem = ERROR_OUTOFMEMORY
};

typedef INIT_ONCE once_flag;

#define ONCE_FLAG_INIT INIT_ONCE_STATIC_INIT

void call_once( once_flag* flag, void (*func)(void) );

typedef DWORD tss_t;
typedef void (*tss_dtor_t)(void*);

static inline int tss_create( tss_t* tss_key, tss_dtor_t destructor )
{
	return ((*tss_key = FlsAlloc(destructor)) == FLS_OUT_OF_INDEXES) ? thrd_error : thrd_success;
}

#define tss_delete FlsFree
#define tss_get FlsGetValue

static inline int tss_set( tss_t tss_id, void *val )
{
	return FlsSetValue(tss_id,val) ? thrd_success : thrd_error;
}

typedef HANDLE thrd_t;
typedef int(*thrd_start_t)(void*);

static inline int thrd_join(thrd_t thr, int* res)
{
	if (WaitForSingleObject(thr,INFINITE) != WAIT_OBJECT_0)
		return thrd_error;

	DWORD dres = 0;
	if (!GetExitCodeThread(thr,&dres) && res)
		return thrd_error;

	if (res)
		*res = dres;

	return thrd_success;
}

int thrd_create( thrd_t *thr, thrd_start_t func, void *arg );
thrd_t thrd_current();

#define thrd_equal(t1,t2) (t1==t2)

typedef HANDLE sema_t;

static inline int sema_init(sema_t* s, unsigned int initial_count)
{
	return (!(*s = CreateSemaphore(NULL,initial_count,LONG_MAX,NULL))) ? thrd_error : thrd_success;
}

static inline int sema_wait(sema_t* s)
{
	return WaitForSingleObject(*s,INFINITE) == WAIT_OBJECT_0 ? thrd_success : thrd_error;
}

static inline int sema_signal(sema_t* s, unsigned int count)
{
	return ReleaseSemaphore(*s,count,NULL) ? thrd_success : thrd_error;
}

static inline void sema_destroy(sema_t* s)
{
	CloseHandle(*s);
}

#elif !defined(__STDC_NO_THREADS__)
#include <threads.h>
#else

#include <pthread.h>

enum
{
	thrd_success = 0,
	thrd_error = EDEADLK,
	thrd_nomem = ENOMEM
};

typedef pthread_once_t once_flag;

#define ONCE_FLAG_INIT PTHREAD_ONCE_INIT

#define call_once pthread_once

typedef pthread_key_t tss_t;
typedef void (*tss_dtor_t)(void*);

#define tss_create pthread_key_create
#define tss_delete pthread_key_delete
#define tss_get pthread_getspecific
#define tss_set pthread_setspecific

typedef pthread_t thrd_t;

typedef int(*thrd_start_t)(void*);

#define thrd_yield pthread_yield
#define thrd_equal pthread_equal
#define thrd_current pthread_self

static inline int thrd_join(thrd_t thr, int* res)
{
	void* vres = NULL;
	int ret = pthread_join(thr,&vres);
	if (ret)
		ret = thrd_error;
	else if (res)
		*res = (uintptr_t)vres;
	return ret;
}

static inline int thrd_create( thrd_t *thr, thrd_start_t func, void *arg )
{
	int err = pthread_create(thr,NULL,(void*(*)(void*))func,arg);
	if (err == EAGAIN)
		err = thrd_nomem;
	else if (err)
		err = thrd_error;
	return err;
}

#include <semaphore.h>

typedef sem_t sema_t;

static inline int sema_init(sema_t* s, unsigned int initial_count)
{
	return sem_init(s,0,initial_count) == -1 ? thrd_error : thrd_success;
}

#define sema_destroy sem_destroy

static inline int sema_wait(sema_t* s)
{
	int err;
	do
	{
		err = sem_wait(s);
	}
	while (err == -1 && errno == EINTR);

	if (err == -1)
		err = thrd_error;
	return err;
}

static inline int sema_signal(sema_t* s, unsigned int count)
{
	while (count-- > 0)
	{
		if (sem_post(s) == -1)
			return thrd_error;
	}
	return thrd_success;
}

#endif

#endif /* SRC_THREADS_H_ */
