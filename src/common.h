/*
 * common.h
 *
 *  Created on: 5 Jan 2017
 *      Author: rick
 */

#ifndef SRC_COMMON_H_
#define SRC_COMMON_H_

#if defined(__GNUC__)
#define _GNU_SOURCE
#endif

#if defined(__STDC_LIB_EXT1__)
#define __STDC_WANT_LIB_EXT1__ 1
#endif

#if defined(__MINGW32__)
#define __USE_MINGW_ANSI_STDIO 1
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdatomic.h>
#include <assert.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>

#if defined(__MINGW32__)
static inline void* aligned_alloc( size_t alignment, size_t size )
{
	return _aligned_malloc(size,alignment);
}
#define aligned_free _aligned_free
#else
#define aligned_free free
#endif

#endif /* SRC_COMMON_H_ */
