
#include "common.h"
#include "task.h"

struct BitonicSort
{
	int sort; // -1 or 1
	void* elems;
	size_t elem_count;
	size_t elem_size;
	task_parallel_compare_fn_t fn;
	void* param;
};

static const size_t L1_data_cache_size = 32 * 1024;

// See http://www.iti.fh-flensburg.de/lang/algorithmen/sortieren/bitonic/oddn.htm

static void serialSort(struct BitonicSort* bs)
{
#if defined(__MINGW32__)
	int sort_trampoline(void* c, const void* p1, const void* p2)
	{
		return bs->sort * (*bs->fn)(p1,p2,c);
	}
	qsort_s(bs->elems,bs->elem_count,bs->elem_size,&sort_trampoline,bs->param);
#else
	int sort_trampoline(const void* p1, const void* p2, void* c)
	{
		return bs->sort * (*bs->fn)(p1,p2,c);
	}
	qsort_r(bs->elems,bs->elem_count,bs->elem_size,&sort_trampoline,bs->param);
#endif
}

static void bitonicMerge(task_t parent, void* p)
{
	struct BitonicSort* bs = p;

	// Greatest power of 2 less than elem_count
	size_t m = bs->elem_count;
	m |= (m >> 1);
	m |= (m >> 2);
	m |= (m >> 4);
	m |= (m >> 8);
	m |= (m >> 16);
#if SIZE_MAX > 0xFFFFFFFF
	m |= (m >> 32);
#endif
	m = (m >> 1) + 1;
	if (m == bs->elem_count)
		m >>= 1;

	// Do the bitonic compare and swap
	char tmp[bs->elem_size];
	for (size_t i=0;i < bs->elem_count - m; ++i)
	{
		void* p1 = (char*)bs->elems + (i * bs->elem_size);
		void* p2 = (char*)p1 + (m * bs->elem_size);

		if (bs->sort * (*bs->fn)(p1,p2,bs->param) > 0)
		{
			memcpy(tmp,p2,bs->elem_size);
			memcpy(p2,p1,bs->elem_size);
			memcpy(p1,tmp,bs->elem_size);
		}
	}

	struct BitonicSort sub_task_data = *bs;
	sub_task_data.elem_count = m;

	bs->elems = (char*)bs->elems + (m * bs->elem_size);
	bs->elem_count -= m;

	task_t sub_task = NULL;
	if (sub_task_data.elem_count > 1)
	{
		if (sub_task_data.elem_count * bs->elem_size >= L1_data_cache_size)
			sub_task = task_run(parent,&bitonicMerge,&sub_task_data,sizeof(sub_task_data));
		else
			bitonicMerge(parent,&sub_task_data);
	}

	if (bs->elem_count > 1)
		bitonicMerge(parent,bs);

	if (sub_task)
		task_join(sub_task);
}

static void bitonicSort(task_t parent, void* p)
{
	struct BitonicSort* bs = p;

	// Find the split point
	size_t split = (bs->elem_count * bs->elem_size) / 2;

	// Round up to L1_data_cache_size
	split = (split + (L1_data_cache_size-1)) & ~(L1_data_cache_size-1);

	// Round up to elem_size
	if (split % bs->elem_size)
		split += bs->elem_size - (split % bs->elem_size);

	if (split > bs->elem_count * bs->elem_size)
		split = bs->elem_count * bs->elem_size;

	if (split <= L1_data_cache_size)
		serialSort(bs);
	else
	{
		struct BitonicSort sub_task_data[2] =
		{
			{
				.sort = -bs->sort,
				.elems = bs->elems,
				.elem_count = split / bs->elem_size,
				.elem_size = bs->elem_size,
				.fn = bs->fn,
				.param = bs->param
			},
			{
				.sort = bs->sort,
				.elems = (char*)bs->elems + split,
				.elem_count = bs->elem_count - (split / bs->elem_size),
				.elem_size = bs->elem_size,
				.fn = bs->fn,
				.param = bs->param
			},
		};

		task_t sub_tasks[2] = { NULL, NULL};
		sub_tasks[0] = task_run(parent,&bitonicSort,&sub_task_data[0],sizeof(sub_task_data[0]));

		if (sub_task_data[1].elem_count * bs->elem_size >= L1_data_cache_size)
			sub_tasks[1] = task_run(parent,&bitonicSort,&sub_task_data[1],sizeof(sub_task_data[1]));
		else
			serialSort(&sub_task_data[1]);

		if (sub_tasks[1])
			task_join(sub_tasks[1]);
		task_join(sub_tasks[0]);

		bitonicMerge(parent,bs);
	}
}

task_t task_bitonic_sort(void* elems, size_t elem_count, size_t elem_size, task_t pt, task_parallel_compare_fn_t fn, void* param)
{
	struct BitonicSort task_data =
	{
		.sort = 1,
		.elems = elems,
		.elem_count = elem_count,
		.elem_size = elem_size,
		.fn = fn,
		.param = param
	};
	return task_run(pt,&bitonicSort,&task_data,sizeof(struct BitonicSort));
}
