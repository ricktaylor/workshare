
#include "common.h"
#include "task.h"

#if defined(__MINGW32__)
static inline int sort(void *ptr, size_t count, size_t size, int (*comp)(const void *, const void *, void *), void *context)
{
	qsort_s(ptr,count,size,(int (__cdecl *)(void *,const void *,const void *))comp,context);
	return 0;
}
#else
static inline int sort(void *ptr, size_t count, size_t size, int (*comp)(const void *, const void *, void *), void *context)
{
	qsort_r(ptr,count,size,comp,context);
	return 0;
}
#endif

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
		sort(ps->elems,ps->elem_count,ps->elem_size,ps->fn,ps->param);
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
		int err = task_run(&sub_tasks[0],NULL,&parallelSortSplit,&sub_task_data[0],sizeof(struct ParaSort));
		if (!err)
		{
			err = task_run(&sub_tasks[1],NULL,&parallelSortSplit,&sub_task_data[1],sizeof(struct ParaSort));
			if (!err)
			{
				task_join(sub_tasks[1]);
				task_join(sub_tasks[0]);
			}
		}

		if (!err)
		{
			// See: http://www.drdobbs.com/parallel/parallel-in-place-merge/240008783?pgno=4


		}
	}
}


int task_parallel_sort(void* elems, size_t elem_count, size_t elem_size, task_parallel_sort_fn_t fn, void* param)
{
	// Check for L1 cache size
	int err = 0;
	if (elem_count * elem_size <= 32 * 1024)
		err = sort(elems,elem_count,elem_size,fn,param);
	else
	{
		struct ParaSort task_data =
		{
			.elems = elems,
			.elem_count = elem_count,
			.elem_size = elem_size,
			.fn = fn,
			.param = param
		};
		task_t task;
		err = task_run(&task,NULL,&parallelSortSplit,&task_data,sizeof(struct ParaSort));
		if (!err)
			task_join(task);
	}
	return err;
}
