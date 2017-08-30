
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

// Check out http://www.drdobbs.com/parallel/parallel-in-place-merge-sort/240169094 for details
static void blockRotate(void* elems, size_t elem_size, size_t elem_count)
{
	// Reverse a range of values from [0..elem_count-1]
	char* l = elems;
	char* r = (char*)elems + ((elem_count - 1) * elem_size);
	char tmp[elem_size];

	for (;l < r; l += elem_size, r -= elem_size)
	{
		memcpy(tmp,r,elem_size);
		memcpy(r,l,elem_size);
		memcpy(l,tmp,elem_size);
	}
}

static void blockSwap(void* elems, size_t elem_size, size_t split, size_t elem_count)
{
	blockRotate(elems,elem_size,split);
	blockRotate((char*)elems + (split * elem_size),elem_size,elem_count - split);
	blockRotate(elems,elem_size,elem_count);
}

static size_t findMidpoint(const void* s, void* elems, size_t elem_size, size_t elem_count, task_parallel_compare_fn_t fn, void* param)
{
	// Binary search
	size_t start = 0;
	for (size_t end = elem_count;start < end;)
	{
		size_t mid = start + (end - start) / 2;
		if ((*fn)(s,(char*)elems + (mid * elem_size),param) > 0)
			start = mid + 1;
		else
			end = mid;
	}
	return start;
}

static void serialMerge(void* elems, size_t elem_size, size_t split, size_t elem_count, task_parallel_compare_fn_t fn, void* param)
{
	if (split == elem_count || split == 0)
		return;

	if (split >= elem_count - split)
	{
		size_t q1 = split / 2;  // q1 is the midpoint of the left
		size_t q2 = split + findMidpoint((char*)elems + (q1 * elem_size),(char*)elems + (split * elem_size),elem_size,elem_count - split,fn,param); // q2 is the position of the first right value > elems[q1]

		blockSwap((char*)elems + (q1 * elem_size),elem_size,split - q1,q2 - 1 - q1);

		size_t q3 = q1 + (q2 - split);  // q3 is the final position of elems[q1]
		if (q1 && q3 && q1 < q3)
			serialMerge(elems,elem_size,q1,q3,fn,param);

		if (q3 + 1 < elem_count && q3 >= q2)
			serialMerge((char*)elems + ((q3 + 1) * elem_size),elem_size,q2 - (q3 + 1),elem_count - (q3 + 1),fn,param);
	}
	else
	{
		size_t q1 = split + ((elem_count - split) / 2);  // q1 is the midpoint of the right side
		size_t q2 = findMidpoint((char*)elems + (q1 * elem_size),elems,elem_size,split,fn,param); // q2 is the position of the first left value > elems[q1]

		blockSwap((char*)elems + (q2 * elem_size),elem_size,split - q2,q1 - q2);

		size_t q3 = q2 + (q1 - split);  // q3 is the final position of elems[q1]
		if (q2 && q3 && q2 < q3)
			serialMerge(elems,elem_size,q2,q3,fn,param);

		if (q3 + 1 < elem_count && q3 > q1)
			serialMerge((char*)elems + ((q3 + 1) * elem_size),elem_size,q1 + 1 - (q3 + 1),elem_count - (q3 + 1),fn,param);
	}
}

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

static void parallelSort(task_t parent, void* p)
{
	struct ParaSort* ps = p;

	// Find the split point
	size_t split = (ps->elem_count * ps->elem_size) / 2;

	// Round up to L1_data_cache_size
	split = (split + (L1_data_cache_size-1)) & ~(L1_data_cache_size-1);

	// Round up to elem_size
	split = (split + (ps->elem_size-1)) & ~(ps->elem_size-1);

	if (split > ps->elem_count * ps->elem_size)
		split = ps->elem_count * ps->elem_size;

	if (split <= L1_data_cache_size)
		serialSort(ps);
	else
	{
		struct ParaSort sub_task_data[2] =
		{
			{
				.elems = ps->elems,
				.elem_count = split / ps->elem_size,
				.elem_size = ps->elem_size,
				.fn = ps->fn,
				.param = ps->param
			},
			{
				.elems = (char*)ps->elems + split,
				.elem_count = ps->elem_count - (split / ps->elem_size),
				.elem_size = ps->elem_size,
				.fn = ps->fn,
				.param = ps->param
			},
		};

		task_t sub_tasks[2] = { NULL, NULL};
		sub_tasks[0] = task_run(parent,&parallelSort,&sub_task_data[0],sizeof(sub_task_data[0]));

		if (sub_task_data[1].elem_count * ps->elem_size > L1_data_cache_size)
			sub_tasks[1] = task_run(parent,&parallelSort,&sub_task_data[1],sizeof(sub_task_data[1]));
		else
			serialSort(&sub_task_data[1]);

		if (sub_tasks[1])
			task_join(sub_tasks[1]);
		task_join(sub_tasks[0]);

		serialMerge(ps->elems,ps->elem_size,split / ps->elem_size,ps->elem_count,ps->fn,ps->param);
	}
}

task_t task_parallel_sort(void* elems, size_t elem_count, size_t elem_size, task_t pt, task_parallel_compare_fn_t fn, void* param)
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
