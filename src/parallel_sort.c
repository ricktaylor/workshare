
#include "common.h"
#include "task.h"

typedef int (*task_parallel_sort_fn_t)(const void*,const void*,void*);

struct ParaSort
{
	void* elems;
	size_t elem_count;
	size_t elem_size;
	task_parallel_sort_fn_t fn;
	void* param;
};

static void parallelSortSplit(task_t parent, const void* p)
{
	const struct ParaSort* ps = p;

	if (ps->elem_count * ps->elem_size <= 32 * 1024)
	{
#if defined(__MINGW32__)
		qsort_s(ps->elems,ps->elem_count,ps->elem_size,(int (__cdecl *)(void *,const void *,const void *))ps->fn,ps->param);
#else
		qsort_r(ps->elems,ps->elem_count,ps->elem_size,ps->fn,ps->param);
#endif
	}
	else
	{
		// try to split on 32K boundaries
		size_t split = (ps->elem_count * ps->elem_size) / (32 * 1024 * 2);
		struct ParaSort sub_task_data[2] =
		{
			{
				.elems = ps->elems,
				.elem_count = split,
				.elem_size = ps->elem_size,
				.fn = ps->fn,
				.param = ps->param
			},
			{
				.elems = ((char*)ps->elems) + (split * ps->elem_size),
				.elem_count = ps->elem_count - split,
				.elem_size = ps->elem_size,
				.fn = ps->fn,
				.param = ps->param
			}
		};

		task_t sub_tasks[2] = {0};
		sub_tasks[0] = task_run(parent,&parallelSortSplit,&sub_task_data[0],sizeof(struct ParaSort));
		sub_tasks[1] = task_run(parent,&parallelSortSplit,&sub_task_data[1],sizeof(struct ParaSort));

		task_join(sub_tasks[1]);
		task_join(sub_tasks[0]);

		// Now merge in-place - HERE BE BUGS!
		char* p = sub_task_data[0].elems;
		char* q = sub_task_data[1].elems;
		char* end = q + (sub_task_data[1].elem_count * ps->elem_size);
		while (p < q)
		{
			if ((ps->fn)(p,q,ps->param) > 0)
			{
				// Swap p and q
				char t[ps->elem_size];
				memcpy(t,p,ps->elem_size);
				memcpy(p,q,ps->elem_size);
				memcpy(q,t,ps->elem_size);

				// Inc q
				if (q < end - ps->elem_size)
					q += ps->elem_size;
			}

			// Inc p
			p += ps->elem_size;
		}
	}
}

task_t task_parallel_sort(void* elems, size_t elem_count, size_t elem_size, task_t pt, task_parallel_sort_fn_t fn, void* param)
{
	struct ParaSort task_data =
	{
		.elems = elems,
		.elem_count = elem_count,
		.elem_size = elem_size,
		.fn = fn,
		.param = param
	};
	return task_run(pt,&parallelSortSplit,&task_data,sizeof(struct ParaSort));
}
