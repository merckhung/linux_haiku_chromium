/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "userland/haiku_compat.h"
#include <locks.h>
#include <image.h>

#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>

extern "C" {

__attribute__((weak)) int __libc_argc = 0;
__attribute__((weak)) const char* const *__libc_argv = nullptr;

__attribute__((constructor))
static void __init_libc_args(int argc, char** argv, char** envp)
{
    __libc_argc = argc;
    __libc_argv = (const char* const*)argv;
}

void __mutex_init(mutex *lock, const char *name)
{
    if (lock) {
        lock->name = name;
        lock->lock = 0;
        lock->flags = 0;
    }
}

void __mutex_init_etc(mutex *lock, const char *name, uint32 flags)
{
    if (lock) {
        lock->name = name;
        lock->lock = 0;
        lock->flags = flags;
    }
}

void __mutex_destroy(mutex *lock)
{
    if (lock) {
        __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
    }
}

status_t __mutex_lock(mutex *lock)
{
    if (!lock)
        return B_BAD_VALUE;
    int spins = 0;
    while (__atomic_exchange_n(&lock->lock, 1, __ATOMIC_ACQUIRE) != 0) {
        if (++spins > 100) {
            sched_yield();
        }
    }
    return B_OK;
}

void __mutex_unlock(mutex *lock)
{
    if (!lock)
        return;
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}

// The embedded mutex protects the ABI's reader/writer counters. A timed
// futex wait parks contenders without adding private fields to the Haiku ABI.
// The timeout also closes the unlock-before-wait race when the reader count
// stays unchanged (for example when the last writer releases the lock).
static void wait_rw_change(rw_lock* lock, int32 observed)
{
    const struct timespec interval = {0, 1000000};
    syscall(SYS_futex, &lock->reader_count, FUTEX_WAIT_PRIVATE, observed,
        &interval, nullptr, 0);
}

static void wake_rw_waiters(rw_lock* lock)
{
    syscall(SYS_futex, &lock->reader_count, FUTEX_WAKE_PRIVATE, INT_MAX,
        nullptr, nullptr, 0);
}

void __rw_lock_init(rw_lock *lock, const char *name)
{
    if (lock) {
        memset(lock, 0, sizeof(*lock));
        __mutex_init(&lock->lock, name);
        lock->holder = -1;
    }
}

void __rw_lock_init_etc(rw_lock *lock, const char *name, uint32 flags)
{
    __rw_lock_init(lock, name);
    if (lock)
        lock->lock.flags = flags;
}

void __rw_lock_destroy(rw_lock *lock)
{
    if (lock)
        __mutex_destroy(&lock->lock);
}

status_t __rw_lock_read_lock(rw_lock *lock)
{
    if (!lock)
        return B_BAD_VALUE;
    const thread_id self = find_thread(NULL);
    for (;;) {
        __mutex_lock(&lock->lock);
        if (lock->holder == self) {
            ++lock->owner_count; // A writer may acquire a nested read lock.
            __mutex_unlock(&lock->lock);
            return B_OK;
        }
        if (lock->writer_count == 0) {
            ++lock->reader_count;
            __mutex_unlock(&lock->lock);
            return B_OK;
        }
        const int32 observed = lock->reader_count;
        __mutex_unlock(&lock->lock);
        wait_rw_change(lock, observed);
    }
}

status_t __rw_lock_read_unlock(rw_lock *lock)
{
    if (!lock)
        return B_BAD_VALUE;
    __mutex_lock(&lock->lock);
    if (lock->holder == find_thread(NULL)) {
        if (--lock->owner_count == 0) {
            lock->holder = -1;
            --lock->writer_count;
        }
    } else if (lock->reader_count > 0) {
        --lock->reader_count;
    } else {
        __mutex_unlock(&lock->lock);
        return B_ERROR;
    }
    __mutex_unlock(&lock->lock);
    wake_rw_waiters(lock);
    return B_OK;
}

status_t __rw_lock_write_lock(rw_lock *lock)
{
    if (!lock)
        return B_BAD_VALUE;
    const thread_id self = find_thread(NULL);
    __mutex_lock(&lock->lock);
    if (lock->holder == self) {
        ++lock->owner_count;
    } else {
        ++lock->writer_count; // Queued writers prevent new readers entering.
        while (lock->reader_count != 0 || lock->holder != -1) {
            const int32 observed = lock->reader_count;
            __mutex_unlock(&lock->lock);
            wait_rw_change(lock, observed);
            __mutex_lock(&lock->lock);
        }
        lock->holder = self;
        lock->owner_count = 1;
    }
    __mutex_unlock(&lock->lock);
    return B_OK;
}

status_t __rw_lock_write_unlock(rw_lock *lock)
{
    if (!lock)
        return B_BAD_VALUE;
    __mutex_lock(&lock->lock);
    if (lock->holder != find_thread(NULL)) {
        __mutex_unlock(&lock->lock);
        return B_ERROR;
    }
    if (--lock->owner_count == 0) {
        lock->holder = -1;
        --lock->writer_count;
    }
    __mutex_unlock(&lock->lock);
    wake_rw_waiters(lock);
    return B_OK;
}

void __recursive_lock_init(recursive_lock *lock, const char *name)
{
    if (lock) {
        memset(lock, 0, sizeof(*lock));
        lock->holder = -1;
    }
}

void __recursive_lock_init_etc(recursive_lock *lock, const char *name, uint32 flags)
{
    if (lock) {
        memset(lock, 0, sizeof(*lock));
        lock->holder = -1;
    }
}

void __recursive_lock_destroy(recursive_lock *lock)
{
    if (lock) {
        lock->holder = -1;
        __atomic_store_n(&lock->lock.lock, 0, __ATOMIC_RELEASE);
    }
}

status_t __recursive_lock_lock(recursive_lock *lock)
{
    if (!lock) return B_BAD_VALUE;
    thread_id self = find_thread(NULL);
    if (__atomic_load_n(&lock->holder, __ATOMIC_RELAXED) == self && lock->recursion > 0) {
        lock->recursion++;
        return B_OK;
    }
    __mutex_lock(&lock->lock);
    __atomic_store_n(&lock->holder, self, __ATOMIC_RELAXED);
    lock->recursion = 1;
    return B_OK;
}

void __recursive_lock_unlock(recursive_lock *lock)
{
    if (!lock) return;
    thread_id self = find_thread(NULL);
    if (__atomic_load_n(&lock->holder, __ATOMIC_RELAXED) == self) {
        lock->recursion--;
        if (lock->recursion <= 0) {
            lock->recursion = 0;
            __atomic_store_n(&lock->holder, -1, __ATOMIC_RELAXED);
            __mutex_unlock(&lock->lock);
        }
    }
}

int32 __recursive_lock_get_recursion(recursive_lock *lock)
{
    return lock ? lock->recursion : 0;
}

status_t __init_once(int32* control, status_t (*initRoutine)(void*), void* data)
{
    if (!control || !initRoutine)
        return B_BAD_VALUE;
    constexpr int32 initializing = -2;
    for (;;) {
        int32 state = __atomic_load_n(control, __ATOMIC_ACQUIRE);
        if (state == INIT_ONCE_INITIALIZED)
            return B_OK;
        if (state == INIT_ONCE_UNINITIALIZED) {
            if (!__atomic_compare_exchange_n(control, &state, initializing,
                    false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                continue;
            const status_t status = initRoutine(data);
            __atomic_store_n(control, status == B_OK ? INIT_ONCE_INITIALIZED
                : INIT_ONCE_UNINITIALIZED, __ATOMIC_RELEASE);
            syscall(SYS_futex, control, FUTEX_WAKE_PRIVATE, INT_MAX,
                nullptr, nullptr, 0);
            return status;
        }
        if (state != initializing)
            return B_BAD_VALUE;
        syscall(SYS_futex, control, FUTEX_WAIT_PRIVATE, initializing,
            nullptr, nullptr, 0);
    }
}

status_t unload_add_on(image_id id)
{
    return B_NOT_SUPPORTED;
}

image_id load_add_on(const char *path)
{
    return B_ERROR;
}

status_t get_image_symbol(image_id id, const char *name,
    int32 symbolType, void **_location)
{
    return B_ERROR;
}

float __swap_float(float arg)
{
    uint32_t val;
    memcpy(&val, &arg, 4);
    val = __builtin_bswap32(val);
    float res;
    memcpy(&res, &val, 4);
    return res;
}

status_t send_signal(team_id team, uint32 signal)
{
    return (kill((pid_t)team, (int)signal) == 0) ? B_OK : B_ERROR;
}

status_t set_thread_priority(thread_id id, int32 newPriority)
{
    return B_NOT_SUPPORTED;
}

} // extern "C"

namespace BPrivate {
    class KMessage;
    __attribute__((weak)) status_t get_extended_team_info(team_id team, uint32 flags, BPrivate::KMessage& message)
    {
        return B_NOT_SUPPORTED;
    }
}
