
#ifndef SRC_TASK_H_
#define SRC_TASK_H_

typedef void* task_t;
typedef void (*task_fn_t)(task_t task, void* param);

#define TASK_PARAM_MAX (32 + 64)

task_t task_run(task_t pt, task_fn_t fn, const void* param, unsigned int param_len);
void task_join(task_t handle);

typedef struct opaque_scheduler_t
{
	int _unused;
}* scheduler_t;

scheduler_t scheduler_create(unsigned int threads);
void scheduler_destroy(scheduler_t sc);

typedef void (*task_parallel_for_fn_t)(void* elems, size_t elem_count, void* param);
task_t task_parallel_for(void* elems, size_t elem_count, size_t elem_size, task_t pt, task_parallel_for_fn_t fn, void* param);

typedef int (*task_parallel_compare_fn_t)(const void*,const void*,void*);

#endif /* SRC_TASK_H_ */
