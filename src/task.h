
#ifndef SRC_TASK_H_
#define SRC_TASK_H_

#include <stddef.h>

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

#endif /* SRC_TASK_H_ */
