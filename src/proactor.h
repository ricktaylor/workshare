/*
 * proactor.h
 *
 *  Created on: 26 Aug 2017
 *      Author: rick
 */

#ifndef SRC_PROACTOR_H_
#define SRC_PROACTOR_H_

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

#endif /* SRC_PROACTOR_H_ */
