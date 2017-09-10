/*
 * quicksort.c
 *
 *  Created on: 10 Sep 2017
 *      Author: rick
 */


#include "common.h"
#include "task.h"

struct ParaSort
{
	void* elems;
	size_t elem_count;
	size_t elem_size;
	task_parallel_compare_fn_t fn;
	void* param;
};

static const size_t L1_data_cache_size = 32 * 1024;

static void serialSort(struct ParaSort* ps)
{
#if defined(__MINGW32__)
	int sort_trampoline(void* c, const void* p1, const void* p2)
	{
		return ps->fn(p1,p2,c);
	}
	qsort_s(ps->elems,ps->elem_count,ps->elem_size,&sort_trampoline,ps->param);
#else
	qsort_r(ps->elems,ps->elem_count,ps->elem_size,ps->fn,ps->param);
#endif
}

static size_t serialPivot(struct ParaSort* ps)
{
	// Select a random pivot
	char t[ps->elem_size];
	char pivot[ps->elem_size];
	size_t p = rand() % ps->elem_count;
	memcpy(pivot,(char*)ps->elems + (p * ps->elem_size),ps->elem_size);

	char* l = ps->elems;
	char* r = l + ((ps->elem_count-1) * ps->elem_size);
	while (l < r)
	{
		int lc;
		while (l < r && (lc = (*ps->fn)(l,pivot,ps->param)) < 0)
			l += ps->elem_size;

		int rc;
		while (l < r && (rc = (*ps->fn)(r,pivot,ps->param)) > 0)
			r -= ps->elem_size;

		if (l < r)
		{
			if (lc == 0 && rc == 0)
				l += ps->elem_size;
			else
			{
				memcpy(t,l,ps->elem_size);
				memcpy(l,r,ps->elem_size);
				memcpy(r,t,ps->elem_size);
			}
		}
	}

	return (l - (char*)ps->elems) / ps->elem_size;
}

static void parallelSort(task_t t, void* p)
{
	struct ParaSort* ps = p;

	if (ps->elem_count <= L1_data_cache_size)
		serialSort(ps);
	else
	{
		size_t pivot = serialPivot(ps);

		struct ParaSort sub_task_data =
		{
			.elems = ps->elems,
			.elem_count = pivot,
			.elem_size = ps->elem_size,
			.fn = ps->fn,
			.param = ps->param
		};
		task_run(t,&parallelSort,&sub_task_data,sizeof(sub_task_data));

		ps->elems = (char*)ps->elems + ((pivot + 1) * ps->elem_size);
		ps->elem_count = ps->elem_count - (pivot + 1);

		task_run(t,&parallelSort,ps,sizeof(*ps));
	}
}

task_t task_quick_sort(void* elems, size_t elem_count, size_t elem_size, task_t pt, task_parallel_compare_fn_t fn, void* param)
{
	struct ParaSort task_data =
	{
		.elems = elems,
		.elem_count = elem_count,
		.elem_size = elem_size,
		.fn = fn,
		.param = param
	};

	return task_run(pt,&parallelSort,&task_data,sizeof(struct ParaSort));
}
