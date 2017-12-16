/*
 * proactor.h
 *
 *  Created on: 26 Aug 2017
 *      Author: rick
 */

#ifndef SRC_PROACTOR_H_
#define SRC_PROACTOR_H_

#include "task.h"

#include <stdint.h>

#if defined(_WIN32)
#include <winsock2.h>
#endif

#if defined(_WIN32)
typedef SOCKET socket_t;
#else
typedef int socket_t;
#endif

typedef struct opaque_proactor_t
{
	int _unused;
}* proactor_t;

proactor_t proactor_create(task_t parent);
void proactor_destroy(proactor_t pr);

unsigned int proactor_add_timer(proactor_t ph, uint32_t timeout, uint32_t repeat, task_t pt, task_fn_t fn, const void* param, unsigned int param_len);
void proactor_cancel_timer(proactor_t ph, unsigned int timer_id);
void proactor_update_timer(proactor_t ph, unsigned int timer_id, uint32_t timeout, uint32_t repeat);

void proactor_add_recv_watcher(proactor_t ph, socket_t fd, task_t pt, task_fn_t fn, const void* param, unsigned int param_len);
void proactor_add_send_watcher(proactor_t ph, socket_t fd, task_t pt, task_fn_t fn, const void* param, unsigned int param_len);

void proactor_add_timed_recv_watcher(proactor_t ph, socket_t fd, uint32_t timeout, task_t pt, task_fn_t io_fn, task_fn_t tmo_fn, const void* param, unsigned int param_len);
void proactor_add_timed_send_watcher(proactor_t ph, socket_t fd, uint32_t timeout, task_t pt, task_fn_t io_fn, task_fn_t tmo_fn, const void* param, unsigned int param_len);

void proactor_cancel_recv_watcher(proactor_t ph, socket_t fd);
void proactor_cancel_send_watcher(proactor_t ph, socket_t fd);

#endif /* SRC_PROACTOR_H_ */
