
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

static const size_t L1_data_cache_size = 32 * 1024;

static void parallelFor(task_t parent, void* p)
{
	struct ParaFor* pf = p;

	// Find the split point
	size_t split = (pf->elem_count * pf->elem_size) / 2;

	// Round up to L1_data_cache_size
	split = (split + (L1_data_cache_size-1)) & ~(L1_data_cache_size-1);

	// Round up to elem_size
	split = (split + (pf->elem_size-1)) & ~(pf->elem_size-1);

	if (split > pf->elem_count * pf->elem_size)
		split = pf->elem_count * pf->elem_size;

	if (split <= L1_data_cache_size)
		(*pf->fn)(pf->elems,pf->elem_count,pf->param);
	else
	{
		struct ParaFor sub_task_data =
		{
			.elems = pf->elems,
			.elem_count = split / pf->elem_size,
			.elem_size = pf->elem_size,
			.fn = pf->fn,
			.param = pf->param
		};
		task_run(parent,&parallelFor,&sub_task_data,sizeof(sub_task_data));

		pf->elems = (char*)pf->elems + split;
		pf->elem_count -= sub_task_data.elem_count;

		if (pf->elem_count * pf->elem_size > L1_data_cache_size)
			task_run(parent,&parallelFor,pf,sizeof(struct ParaFor));
		else
			(*pf->fn)(pf->elems,pf->elem_count,pf->param);
	}
}

task_t task_parallel_for(void* elems, size_t elem_count, size_t elem_size, task_t pt, task_parallel_for_fn_t fn, void* param)
{
	struct ParaFor task_data =
	{
		.elems = elems,
		.elem_count = elem_count,
		.elem_size = elem_size,
		.fn = fn,
		.param = param
	};
	return task_run(pt,&parallelFor,&task_data,sizeof(struct ParaFor));
}
