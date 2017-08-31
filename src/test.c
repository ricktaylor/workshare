/*
 * main.c
 *
 *  Created on: 5 Jan 2017
 *      Author: rick
 */

#include "common.h"
#include "task.h"

#include <windows.h>
#include <math.h>

static void for_fn(void* elems, size_t elem_count, void* param)
{
	const double* data = elems;

	double* total = param;
	double t = 0;
	for (size_t i = 0; i < elem_count; ++i)
		t += sqrt(data[i]);

	*total += t;
}

static void test_for()
{
	const size_t data_count = 1024 * 1024 *1024 + 129;
	double* data = malloc(data_count * sizeof(double));
	if (!data)
		abort();

	for (size_t i=0;i < data_count;++i)
		data[i] = rand();

	DWORD dwStart = GetTickCount();

	double total1 = 0.0;
	for_fn(data,data_count,&total1);

	DWORD dwEnd = GetTickCount();

	printf("FOR: Linear elapsed time: %zu\n",(size_t)(dwEnd-dwStart));
	fflush(stdout);

	dwStart = GetTickCount();

	double total2 = 0.0;
	task_join(task_parallel_for(data,data_count,sizeof(uint32_t),NULL,&for_fn,&total2));

	dwEnd = GetTickCount();

	printf("FOR: Parallel elapsed time: %zu\n",(size_t)(dwEnd-dwStart));
	fflush(stdout);

	assert(total1 == total2);

	free(data);
}

static int sort_fn(const void* p1, const void* p2, void* p)
{
	return (*(unsigned char*)p1 - *(unsigned char*)p2);
}

static void test_sort_each(size_t data_count)
{
	uint32_t* data1 = malloc(data_count * sizeof(uint32_t));
	if (!data1)
		abort();

	uint32_t* data2 = malloc(data_count * sizeof(uint32_t));
	if (!data2)
		abort();

	uint32_t* data3 = malloc(data_count * sizeof(uint32_t));
	if (!data3)
		abort();

	for (size_t i=0;i < data_count;++i)
		data1[i] = rand();

	memcpy(data2,data1,data_count * sizeof(uint32_t));
	memcpy(data3,data1,data_count * sizeof(uint32_t));

	DWORD dwStart = GetTickCount();

#if defined(__MINGW32__)
	int sort_trampoline(void* c, const void* p1, const void* p2)
	{
		return sort_fn(p1,p2,c);
	}
	qsort_s(data1,data_count,sizeof(data1[0]),&sort_trampoline,NULL);
#else
	qsort_r(data1,data_count,sizeof(data1[0]),sort_fn,NULL);
#endif

	DWORD dwEnd = GetTickCount();

	printf("SORT (%f): Linear elapsed time: %zu\n",(double)data_count,(size_t)(dwEnd-dwStart));
	fflush(stdout);

	dwStart = GetTickCount();

	task_join(task_parallel_sort(data2,data_count,sizeof(data2[0]),NULL,&sort_fn,NULL));

	dwEnd = GetTickCount();

	printf("SORT (%f): Parallel elapsed time: %zu\n",(double)data_count,(size_t)(dwEnd-dwStart));
	fflush(stdout);

	assert(memcmp(data1,data2,data_count * sizeof(data1[0])) == 0);

	/*dwStart = GetTickCount();

	task_join(task_bitonic_sort(data3,data_count,sizeof(data3[0]),NULL,&sort_fn,NULL));

	dwEnd = GetTickCount();

	printf("SORT (%f): Bitonic elapsed time: %zu\r\n",(double)data_count,(size_t)(dwEnd-dwStart));
	fflush(stdout);

	assert(memcmp(data1,data3,data_count * sizeof(data1[0])) == 0);*/

	free(data1);
	free(data2);
	free(data3);
}

static void test_sort()
{
	for (size_t l = 64; l < 1024 * 1024; l *= 2)
	{
		test_sort_each(l * 1024);
	}
}

int main(int argc, char* argv[])
{
	scheduler_t s = scheduler_create(8);

	//test_for();

	test_sort();

	scheduler_destroy(s);

	return 0;
}
