
typedef void* task_t;
typedef void (*task_fn_t)(task_t task, const void* param);

#define TASK_PARAM_MAX 32

int task_run(task_t* t, task_t pt, task_fn_t fn, const void* param, unsigned int param_len);
void task_join(task_t handle);

typedef struct opaque_scheduler_t
{
	int _unused;
}* scheduler_t;

scheduler_t scheduler_create(unsigned int threads);
void scheduler_destroy(scheduler_t sc);

typedef void (*task_parallel_for_fn_t)(void* elems, size_t elem_count, void* param);

int task_parallel_for(void* elems, size_t elem_count, size_t elem_size, task_parallel_for_fn_t fn, void* param);