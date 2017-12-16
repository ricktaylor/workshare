/*
 * threads.c
 *
 *  Created on: 5 Jan 2017
 *      Author: rick
 */

#include "threads.h"

#if defined(_WIN32)

static BOOL CALLBACK thunk(once_flag* flag, void* p1, void ** p2)
{
	void (*func)(void) = p1;
	(*func)();
	return TRUE;
}

void call_once( once_flag* flag, void (*func)() )
{
	InitOnceExecuteOnce(flag,&thunk,func,NULL);
}

static once_flag s_thrd_once = ONCE_FLAG_INIT;
static DWORD s_thrd_self;

thrd_t thrd_current()
{
	return TlsGetValue(s_thrd_self);
}

struct thrd_thunk_args
{
	HANDLE self;
	thrd_start_t fn;
	void* arg;
};

static void init_thrd_current()
{
	s_thrd_self = TlsAlloc();
}

static unsigned __stdcall thrd_thunk(void* args)
{
	struct thrd_thunk_args* t = args;

	call_once(&s_thrd_once,&init_thrd_current);

	thrd_start_t fn = t->fn;
	void* arg = t->arg;
	TlsSetValue(s_thrd_self,t->self);

	free(t);

	return (*fn)(arg);
}

int thrd_create( thrd_t *thr, thrd_start_t func, void *arg )
{
	struct thrd_thunk_args* thunk_args = calloc(1,sizeof(struct thrd_thunk_args));
	if (!thunk_args)
		return thrd_nomem;

	thunk_args->fn = func;
	thunk_args->arg = arg;
	thunk_args->self = (thrd_t)_beginthreadex(NULL,0,&thrd_thunk,thunk_args,CREATE_SUSPENDED,NULL);
	if (!thunk_args->self)
	{
		free(thunk_args);
		return thrd_error;
	}

	*thr = thunk_args->self;
	ResumeThread(*thr);
	return thrd_success;
}

#endif
