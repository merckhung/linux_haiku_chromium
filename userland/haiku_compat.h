/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#ifndef _HAIKU_LINUX_COMPAT_H_
#define _HAIKU_LINUX_COMPAT_H_

#include <stdint.h>
#include <stddef.h>

#define fd_set _glibc_fd_set
#define dev_t _glibc_dev_t
#define ino_t _glibc_ino_t
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#undef fd_set
#undef dev_t
#undef ino_t

typedef int32_t dev_t;
typedef int64_t ino_t;

#include <limits.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#ifndef O_RWMASK
#define O_RWMASK O_ACCMODE
#endif

#ifndef O_NOTRAVERSE
#define O_NOTRAVERSE O_NOFOLLOW
#endif

#ifndef SYMLOOP_MAX
#define SYMLOOP_MAX 16
#endif

#ifndef LONGLONG_MIN
#define LONGLONG_MIN (-9223372036854775807LL - 1)
#endif
#ifndef LONGLONG_MAX
#define LONGLONG_MAX (9223372036854775807LL)
#endif
#ifndef ULONGLONG_MAX
#define ULONGLONG_MAX (0xffffffffffffffffULL)
#endif

#ifdef RUN_WITHOUT_REGISTRAR
static const bool registerApp = false;
#endif

#ifndef VSWTCH
#ifdef VSWTC
#define VSWTCH VSWTC
#else
#define VSWTCH 7
#endif
#endif

#ifndef CSWTCH
#define CSWTCH 0
#endif

#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

#ifndef S_IUMSK
#define S_IUMSK 07777
#endif

#ifndef st_crtime
#define st_crtime st_ctim.tv_sec
#endif

#ifndef S_ATTR_DIR
#define S_ATTR_DIR 01000000000
#define S_ATTR 02000000000
#define S_INDEX_DIR 04000000000
#define S_STR_INDEX 00100000000
#define S_INT_INDEX 00200000000
#define S_UINT_INDEX 00400000000
#define S_LONG_LONG_INDEX 00010000000
#define S_ULONG_LONG_INDEX 00020000000
#define S_FLOAT_INDEX 00040000000
#define S_DOUBLE_INDEX 00001000000
#define S_ALLOW_DUPS 00002000000
#define S_ISINDEX(mode) (((mode) & S_INDEX_DIR) == S_INDEX_DIR)
#endif

typedef uintptr_t addr_t;

#define dirent _glibc_dirent
#include <dirent.h>
#undef dirent

struct dirent {
    dev_t          d_dev;
    dev_t          d_pdev;
    ino_t          d_ino;
    ino_t          d_pino;
    unsigned short d_reclen;
    char           d_name[256];
};
typedef struct dirent dirent;

struct fd_set {
    long fds_bits[1024 / (8 * sizeof(long))];
};
typedef struct fd_set fd_set;

#ifdef __cplusplus
extern "C" {
#endif
#include <pthread.h>
static inline int atfork(void (*prepare)(void)) {
    return pthread_atfork(NULL, NULL, prepare);
}
#ifdef __cplusplus
}
#endif

#include <config/types.h>
#include <SupportDefs.h>

#endif // _HAIKU_LINUX_COMPAT_H_
