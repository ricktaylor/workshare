/*
 * main.c
 *
 *  Created on: 5 Jan 2017
 *      Author: rick
 */

#include "common.h"
#include "task.h"

#include <windows.h>

void test_fn(void* elems, size_t elem_count, void* param)
{
	const uint32_t* data = elems;

	size_t total = 0;
	for (size_t i = 0; i < elem_count; ++i)
		total += data[i];
}

int main(int argc, char* argv[])
{
	const size_t data_set = 1000000000;
	uint32_t* data1 = malloc(data_set * sizeof(uint32_t));
	if (!data1)
		return -1;

	uint32_t* data2 = malloc(data_set * sizeof(uint32_t));
	if (!data2)
		return -1;

	for (size_t i=0;i < data_set;++i)
		data1[i] = rand();

	memcpy(data2,data1,data_set * sizeof(uint32_t));

	DWORD dwStart = GetTickCount();

	test_fn(data1,data_set,NULL);

	DWORD dwEnd = GetTickCount();

	printf("Linear elapsed time: %zu\n",(size_t)(dwEnd-dwStart));

	scheduler_t s = scheduler_create(8);

	dwStart = GetTickCount();

	task_join(task_parallel_for(data2,data_set,sizeof(uint32_t),&test_fn,NULL));

	dwEnd = GetTickCount();

	printf("Parallel elapsed time: %zu\n",(size_t)(dwEnd-dwStart));

	scheduler_destroy(s);

	fflush(stdout);

	return 0;
}

