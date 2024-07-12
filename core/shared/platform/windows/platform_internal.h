/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <sys/timeb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <malloc.h>
#include <process.h>
#include <winapifamily.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <basetsd.h>
#include <signal.h>

#include "platform_wasi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BH_PLATFORM_WINDOWS
#define BH_PLATFORM_WINDOWS
#endif

#ifdef _MSC_VER
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#endif /* #ifdef _MSC_VER */

/* Default thread priority */
#define BH_THREAD_DEFAULT_PRIORITY 0

typedef SSIZE_T ssize_t;

typedef void *korp_thread;
typedef void *korp_tid;
typedef void *korp_mutex;

/**
 * Create the mutex when os_mutex_lock is called, and no need to
 * CloseHandle() for the static lock's lifetime, since
 * "The system closes the handle automatically when the process
 *  terminates. The mutex object is destroyed when its last
 *  handle has been closed."
 * Refer to:
 *   https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexa
 */
#define OS_THREAD_MUTEX_INITIALIZER NULL

struct os_thread_wait_node;
typedef struct os_thread_wait_node *os_thread_wait_list;
typedef struct korp_cond {
    korp_mutex wait_list_lock;
    os_thread_wait_list thread_wait_list;
    struct os_thread_wait_node *thread_wait_list_end;
} korp_cond;

#define strncasecmp _strnicmp
#define strcasecmp _stricmp

#ifdef __cplusplus
}
#endif

#endif /* end of _PLATFORM_INTERNAL_H */
