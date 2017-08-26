
#include "common.h"
#include "task.h"

struct ParaFor
{
	void* elems;
	size_t elem_count;
	size_t elem_size;
	task_parallel_for_fn_t fn;
	void* param;
};

static void parallelForSplit(task_t parent, const void* p)
{
	const struct ParaFor* pf = p;

	// Check for L1 cache size
	if (pf->elem_count * pf->elem_size <= 32 * 1024)
		(*pf->fn)(pf->elems,pf->elem_count,pf->param);
	else
	{
		size_t split = pf->elem_count / 2;
		struct ParaFor sub_task_data[2] =
		{
			{
				.elems = pf->elems,
				.elem_count = split,
				.elem_size = pf->elem_size,
				.fn = pf->fn,
				.param = pf->param
			},
			{
				.elems = ((char*)pf->elems) + (split * pf->elem_size),
				.elem_count = pf->elem_count - split,
				.elem_size = pf->elem_size,
				.fn = pf->fn,
				.param = pf->param
			}
		};

		int err = task_run(NULL,parent,&parallelForSplit,&sub_task_data[0],sizeof(struct ParaFor));
		if (!err)
			task_run(NULL,parent,&parallelForSplit,&sub_task_data[1],sizeof(struct ParaFor));
	}
}

task_t task_parallel_for(void* elems, size_t elem_count, size_t elem_size, task_parallel_for_fn_t fn, void* param)
{
	struct ParaFor task_data =
	{
		.elems = elems,
		.elem_count = elem_count,
		.elem_size = elem_size,
		.fn = fn,
		.param = param
	};
	task_t task;
	task_run(&task,NULL,&parallelForSplit,&task_data,sizeof(struct ParaFor));
	return task;
}
