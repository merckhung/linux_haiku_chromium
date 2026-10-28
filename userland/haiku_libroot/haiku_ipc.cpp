/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "haiku_ipc.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/futex.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

static struct HaikuSharedRegistry* s_registry = nullptr;
static pthread_once_t s_init_once = PTHREAD_ONCE_INIT;

static void init_registry(void)
{
    int fd = shm_open(HAIKU_IPC_SHM_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("haiku_libroot: shm_open registry failed");
        return;
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size < (off_t)sizeof(struct HaikuSharedRegistry)) {
        if (ftruncate(fd, sizeof(struct HaikuSharedRegistry)) != 0) {
            perror("haiku_libroot: ftruncate registry failed");
            close(fd);
            return;
        }
    }

    void* addr = mmap(nullptr, sizeof(struct HaikuSharedRegistry),
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        perror("haiku_libroot: mmap registry failed");
        return;
    }

    s_registry = (struct HaikuSharedRegistry*)addr;

    // Initialize lock if first creator
    // Using atomic compare on next_area_id as an init flag
    if (__sync_bool_compare_and_swap(&s_registry->next_area_id, 0, 1)) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
        pthread_mutex_init(&s_registry->lock, &attr);
        pthread_mutexattr_destroy(&attr);

        s_registry->next_port_id = 1;
        s_registry->next_sem_id = 1;
        s_registry->next_thread_id = 100000;
    }
}

extern "C" struct HaikuSharedRegistry* haiku_get_registry(void)
{
    pthread_once(&s_init_once, init_registry);
    return s_registry;
}

extern "C" void haiku_lock_registry(void)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (reg) {
        int rc = pthread_mutex_lock(&reg->lock);
        if (rc == EOWNERDEAD) {
            pthread_mutex_consistent(&reg->lock);
        }
    }
}

extern "C" void haiku_unlock_registry(void)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (reg) {
        pthread_mutex_unlock(&reg->lock);
    }
}

extern "C" int haiku_futex_wait(uint32_t* uaddr, uint32_t val, const struct timespec* timeout)
{
    return syscall(SYS_futex, uaddr, FUTEX_WAIT, val, timeout, nullptr, 0);
}

extern "C" int haiku_futex_wake(uint32_t* uaddr, int count)
{
    return syscall(SYS_futex, uaddr, FUTEX_WAKE, count, nullptr, nullptr, 0);
}

extern "C" bigtime_t haiku_timespec_to_usecs(const struct timespec* ts)
{
    return ((bigtime_t)ts->tv_sec * 1000000LL) + (ts->tv_nsec / 1000LL);
}

extern "C" void haiku_usecs_to_timespec(bigtime_t usecs, struct timespec* ts)
{
    ts->tv_sec = usecs / 1000000LL;
    ts->tv_nsec = (usecs % 1000000LL) * 1000LL;
}
