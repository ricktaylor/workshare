
#include "common.h"
#include "task.h"

struct ParaSort
{
	void* elems;
	size_t elem_count;
	size_t elem_size;
	size_t split;
	task_parallel_compare_fn_t fn;
	void* param;
};

static const size_t L1_data_cache_size = 32 * 1024;

// Check out http://www.drdobbs.com/parallel/parallel-in-place-merge-sort/240169094 for details
static void blockRotate(task_t parent, void* p)
{
	struct ParaSort* ps = p;

	// Reverse a range of values from [0..elem_count-1]
	char* l = ps->elems;
	char* r = l + ((ps->elem_count - 1) * ps->elem_size);
	char tmp[ps->elem_size];

	for (;l < r; l += ps->elem_size, r -= ps->elem_size)
	{
		memcpy(tmp,r,ps->elem_size);
		memcpy(r,l,ps->elem_size);
		memcpy(l,tmp,ps->elem_size);
	}
}

static void blockSwap(void* elems, size_t elem_size, size_t split, size_t elem_count)
{
	if (split * elem_size <= 1024 * 1024 || (elem_count - split) * elem_size <= 1024 * 1024)
	{
		if (split < (elem_count - split))
		{
			char buf[split * elem_size];
			memcpy(buf,elems,split * elem_size);
			memmove(elems,(char*)elems + (split * elem_size),elem_count - split);
			memcpy((char*)elems + ((elem_count - split) * elem_size),buf,split * elem_size);
		}
		else
		{
			char buf[(elem_count - split) * elem_size];
			memcpy(buf,(char*)elems + (split * elem_size),(elem_count - split) * elem_size);
			memmove((char*)elems + ((elem_count - split) * elem_size),elems,split * elem_size);
			memcpy(elems,buf,(elem_count - split) * elem_size);
		}
	}
	else
	{
		struct ParaSort sub_task_data[2] =
		{
			{
				.elems = elems,
				.elem_count = split,
				.elem_size = elem_size
			},
			{
				.elems = (char*)elems + (split * elem_size),
				.elem_count = elem_count - split,
				.elem_size = elem_size
			},
		};

		task_t sub_tasks[2] = { NULL, NULL };
		sub_tasks[0] = task_run(NULL,&blockRotate,&sub_task_data[0],sizeof(sub_task_data[0]));
		sub_tasks[1] = task_run(NULL,&blockRotate,&sub_task_data[1],sizeof(sub_task_data[1]));

		task_join(sub_tasks[0]);
		task_join(sub_tasks[1]);

		sub_task_data[0].elem_count = elem_count;
		blockRotate(NULL,&sub_task_data[0]);
	}
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

static void parallelMerge(task_t parent, void* p)
{
	struct ParaSort* ps = p;

	/*if (ps->elem_count * ps->elem_size <= 1024 * 1024)
	{
		char* a = ps->elems;
		char* a_end = a + (ps->split * ps->elem_size);
		char* b = a_end;
		char* b_end = a + (ps->elem_count * ps->elem_size);
		char buf[ps->elem_count * ps->elem_size];
		char* c = buf;

		while (a < a_end && b < b_end)
		{
			char* a_start = a;
			while ((*ps->fn)(a,b,ps->param) <= 0 && a < a_end)
				a += ps->elem_size;

			if (a_start < a)
			{
				memcpy(c,a_start,a - a_start);
				c += a - a_start;
			}

			char* b_start = b;
			do
			{
				b += ps->elem_size;
			}
			while ((*ps->fn)(a,b,ps->param) > 0 && b < b_end);

			if (b_start < b)
			{
				memcpy(c,b_start,b - b_start);
				c += b - b_start;
			}
		}

		if (a < a_end)
			memcpy(c,a,a_end - a);

		if (b < b_end)
			memcpy(c,b,b_end - b);

		memcpy(ps->elems,buf,ps->elem_count * ps->elem_size);

		//atomic_fetch_add(&tiny_merges,1);
		return;
	}*/

	struct ParaSort sub_task_data = *ps;
	if (ps->split >= ps->elem_count - ps->split)
	{
		size_t q1 = ps->split / 2;  // q1 is the midpoint of the left
		size_t q2 = ps->split + findMidpoint((char*)ps->elems + (q1 * ps->elem_size),(char*)ps->elems + (ps->split * ps->elem_size),ps->elem_size,ps->elem_count - ps->split,ps->fn,ps->param); // q2 is the position of the first right value > elems[q1]
		size_t q3 = q1 + (q2 - ps->split);  // q3 is the final position of elems[q1]

		blockSwap((char*)ps->elems + (q1 * ps->elem_size),ps->elem_size,ps->split - q1,q2 - q1);

		sub_task_data.elems = ps->elems;
		sub_task_data.split = q1;
		sub_task_data.elem_count = q3;

		ps->elems = (char*)ps->elems + ((q3 + 1) * ps->elem_size);
		ps->split = q2 - (q3 + 1);
		ps->elem_count -= (q3 + 1);
	}
	else
	{
		size_t q1 = ps->split + ((ps->elem_count - ps->split) / 2);  // q1 is the midpoint of the right side
		size_t q2 = findMidpoint((char*)ps->elems + (q1 * ps->elem_size),ps->elems,ps->elem_size,ps->split,ps->fn,ps->param); // q2 is the position of the first left value > elems[q1]
		size_t q3 = q2 + (q1 - ps->split);  // q3 is the final position of elems[q1]

		blockSwap((char*)ps->elems + (q2 * ps->elem_size),ps->elem_size,ps->split - q2,q1 + 1 - q2);

		sub_task_data.elems = ps->elems;
		sub_task_data.split = q2;
		sub_task_data.elem_count = q3;

		ps->elems = (char*)ps->elems + ((q3 + 1) * ps->elem_size);
		ps->split = q1 - q3;
		ps->elem_count -= (q3 + 1);
	}

	if (sub_task_data.split && sub_task_data.split < sub_task_data.elem_count)
		task_run(parent,&parallelMerge,&sub_task_data,sizeof(sub_task_data));

	if (ps->split && ps->split < ps->elem_count)
		task_run(parent,&parallelMerge,ps,sizeof(*ps));
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
	if (split % ps->elem_size)
		split += ps->elem_size - (split % ps->elem_size);

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
		if (sub_task_data[0].elem_count > 1)
			sub_tasks[0] = task_run(NULL,&parallelSort,&sub_task_data[0],sizeof(sub_task_data[0]));

		if (sub_task_data[1].elem_count > 1)
			sub_tasks[1] = task_run(NULL,&parallelSort,&sub_task_data[1],sizeof(sub_task_data[1]));

		task_join(sub_tasks[0]);
		task_join(sub_tasks[1]);

		ps->split = split / ps->elem_size;
		parallelMerge(parent,ps);
	}
}

#include <math.h>

task_t task_merge_sort(void* elems, size_t elem_count, size_t elem_size, task_t pt, task_parallel_compare_fn_t fn, void* param)
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
