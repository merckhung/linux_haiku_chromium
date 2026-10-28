/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "haiku_ipc.h"

#include <pthread.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>

struct ThreadArgs {
    thread_func func;
    void*       data;
    thread_id   id;
    int         slot;
};

static __thread thread_id s_current_thread_id = 0;
static __thread pid_t s_current_linux_tid = 0;

static int ensure_thread_slot_locked(struct HaikuSharedRegistry* reg, thread_id id)
{
    for (int i = 0; i < MAX_THREADS; i++) {
        if (reg->threads[i].active && reg->threads[i].id == id)
            return i;
    }

    int slot = -1;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (!reg->threads[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < MAX_THREADS; i++) {
            if (reg->threads[i].exited) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
        return -1;

    struct HaikuThreadEntry* entry = &reg->threads[slot];
    memset(entry, 0, sizeof(*entry));
    entry->active = true;
    entry->started = true;
    entry->exited = false;
    entry->id = id;
    entry->linux_tid = (pid_t)id;
    entry->team = (team_id)getpid();
    snprintf(entry->name, B_OS_NAME_LENGTH, "thread_%d", (int)id);
    entry->priority = B_NORMAL_PRIORITY;
    return slot;
}

static void* thread_trampoline(void* arg)
{
    struct ThreadArgs* targs = (struct ThreadArgs*)arg;
    thread_func func = targs->func;
    void* data = targs->data;
    thread_id id = targs->id;
    int slot = targs->slot;
    delete targs;

    s_current_linux_tid = (pid_t)syscall(SYS_gettid);
    s_current_thread_id = id;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (reg) {
        haiku_lock_registry();
        reg->threads[slot].linux_tid = (pid_t)syscall(SYS_gettid);
        haiku_unlock_registry();

        // Wait until resume_thread is called
        while (true) {
            haiku_lock_registry();
            if (reg->threads[slot].started) {
                haiku_unlock_registry();
                break;
            }
            uint32_t val = reg->threads[slot].start_futex;
            haiku_unlock_registry();
            haiku_futex_wait(&reg->threads[slot].start_futex, val, nullptr);
        }
    }

    status_t ret = func(data);

    if (reg) {
        haiku_lock_registry();
        reg->threads[slot].exited = true;
        reg->threads[slot].exit_code = ret;
        reg->threads[slot].exit_futex++;
        haiku_unlock_registry();
        haiku_futex_wake(&reg->threads[slot].exit_futex, 1000);
    }

    return (void*)(intptr_t)ret;
}

extern "C" thread_id spawn_thread(thread_func func, const char *name, int32 priority, void *data)
{
    if (!func)
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();

    int slot = -1;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (!reg->threads[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        haiku_unlock_registry();
        return B_NO_MORE_THREADS;
    }

    thread_id new_id = reg->next_thread_id++;
    struct HaikuThreadEntry* entry = &reg->threads[slot];
    entry->active = true;
    entry->started = false;
    entry->exited = false;
    entry->id = new_id;
    entry->team = (team_id)getpid();
    if (name) {
        strncpy(entry->name, name, B_OS_NAME_LENGTH - 1);
        entry->name[B_OS_NAME_LENGTH - 1] = '\0';
    } else {
        entry->name[0] = '\0';
    }
    entry->priority = priority;
    entry->exit_code = 0;
    entry->start_futex = 0;
    entry->exit_futex = 0;
    entry->mailbox.has_data = false;
    entry->mailbox_futex = 0;

    haiku_unlock_registry();

    struct ThreadArgs* targs = new ThreadArgs{func, data, new_id, slot};
    pthread_t pt;
    int rc = pthread_create(&pt, nullptr, thread_trampoline, targs);
    if (rc != 0) {
        haiku_lock_registry();
        reg->threads[slot].active = false;
        haiku_unlock_registry();
        delete targs;
        return B_ERROR;
    }

    pthread_detach(pt);
    return new_id;
}

extern "C" status_t resume_thread(thread_id thread)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    for (int i = 0; i < MAX_THREADS; i++) {
        if (reg->threads[i].active && reg->threads[i].id == thread) {
            reg->threads[i].started = true;
            reg->threads[i].start_futex++;
            haiku_unlock_registry();
            haiku_futex_wake(&reg->threads[i].start_futex, 1);
            return B_OK;
        }
    }
    haiku_unlock_registry();
    return B_BAD_THREAD_ID;
}

extern "C" status_t suspend_thread(thread_id thread)
{
    return B_OK;
}

extern "C" status_t kill_thread(thread_id thread)
{
    return B_OK;
}

extern "C" status_t wait_for_thread(thread_id thread, status_t *returnValue)
{
    return wait_for_thread_etc(thread, 0, B_INFINITE_TIMEOUT, returnValue);
}

extern "C" status_t wait_for_thread_etc(thread_id id, uint32 flags, bigtime_t timeout, status_t *returnValue)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    int slot = -1;
    haiku_lock_registry();
    for (int i = 0; i < MAX_THREADS; i++) {
        if (reg->threads[i].active && reg->threads[i].id == id) {
            slot = i;
            break;
        }
    }
    haiku_unlock_registry();

    if (slot < 0)
        return B_BAD_THREAD_ID;

    while (true) {
        haiku_lock_registry();
        if (reg->threads[slot].exited) {
            if (returnValue)
                *returnValue = reg->threads[slot].exit_code;
            reg->threads[slot].active = false;
            haiku_unlock_registry();
            return B_OK;
        }
        uint32_t val = reg->threads[slot].exit_futex;
        haiku_unlock_registry();

        haiku_futex_wait(&reg->threads[slot].exit_futex, val, nullptr);
    }
}

extern "C" thread_id find_thread(const char *name)
{
    if (!name) {
        pid_t tid = (pid_t)syscall(SYS_gettid);
        thread_id self_id = (s_current_thread_id != 0 && s_current_linux_tid == tid)
            ? s_current_thread_id
            : (thread_id)tid;
        struct HaikuSharedRegistry* reg = haiku_get_registry();
        if (reg) {
            haiku_lock_registry();
            ensure_thread_slot_locked(reg, self_id);
            haiku_unlock_registry();
        }
        return self_id;
    }

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    thread_id id = B_NAME_NOT_FOUND;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (reg->threads[i].active && strcmp(reg->threads[i].name, name) == 0) {
            id = reg->threads[i].id;
            break;
        }
    }
    haiku_unlock_registry();
    return id;
}

extern "C" void exit_thread(status_t status)
{
    pthread_exit((void*)(intptr_t)status);
}

extern "C" status_t snooze(bigtime_t amount)
{
    if (amount <= 0)
        return B_OK;

    struct timespec req;
    haiku_usecs_to_timespec(amount, &req);
    struct timespec rem;
    while (nanosleep(&req, &rem) != 0 && errno == EINTR) {
        req = rem;
    }
    return B_OK;
}

extern "C" status_t snooze_etc(bigtime_t amount, int timeBase, uint32 flags)
{
    return snooze(amount);
}

extern "C" status_t snooze_until(bigtime_t time, int timeBase)
{
    bigtime_t now = system_time();
    if (time > now)
        return snooze(time - now);
    return B_OK;
}

extern "C" status_t send_data(thread_id thread, int32 code, const void *buffer, size_t bufferSize)
{
    if (bufferSize > sizeof(((struct HaikuThreadMailbox*)0)->data))
        return B_NO_MEMORY;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    thread_id self = find_thread(nullptr);

    while (true) {
        haiku_lock_registry();
        int slot = ensure_thread_slot_locked(reg, thread);
        if (slot < 0) {
            haiku_unlock_registry();
            return B_BAD_THREAD_ID;
        }
        struct HaikuThreadMailbox* mb = &reg->threads[slot].mailbox;
        if (!mb->has_data) {
            mb->code = code;
            mb->size = bufferSize;
            mb->sender = self;
            if (bufferSize > 0 && buffer)
                memcpy(mb->data, buffer, bufferSize);
            mb->has_data = true;
            reg->threads[slot].mailbox_futex++;
            haiku_unlock_registry();

            haiku_futex_wake(&reg->threads[slot].mailbox_futex, 1000);
            return B_OK;
        }
        uint32_t val = reg->threads[slot].mailbox_futex;
        haiku_unlock_registry();

        haiku_futex_wait(&reg->threads[slot].mailbox_futex, val, nullptr);
    }
}

extern "C" int32 receive_data(thread_id *sender, void *buffer, size_t bufferSize)
{
    thread_id self = find_thread(nullptr);
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = ensure_thread_slot_locked(reg, self);
    haiku_unlock_registry();

    if (slot < 0)
        return B_BAD_THREAD_ID;

    while (true) {
        haiku_lock_registry();
        struct HaikuThreadMailbox* mb = &reg->threads[slot].mailbox;
        if (mb->has_data) {
            if (sender)
                *sender = mb->sender;
            int32 code = mb->code;
            size_t copy_size = (bufferSize < mb->size) ? bufferSize : mb->size;
            if (copy_size > 0 && buffer)
                memcpy(buffer, mb->data, copy_size);
            mb->has_data = false;
            reg->threads[slot].mailbox_futex++;
            haiku_unlock_registry();
            haiku_futex_wake(&reg->threads[slot].mailbox_futex, 1000);
            return code;
        }
        uint32_t val = reg->threads[slot].mailbox_futex;
        haiku_unlock_registry();

        haiku_futex_wait(&reg->threads[slot].mailbox_futex, val, nullptr);
    }
}

extern "C" bool has_data(thread_id thread)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return false;

    haiku_lock_registry();
    for (int i = 0; i < MAX_THREADS; i++) {
        if (reg->threads[i].active && reg->threads[i].id == thread) {
            bool hd = reg->threads[i].mailbox.has_data;
            haiku_unlock_registry();
            return hd;
        }
    }
    haiku_unlock_registry();
    return false;
}

extern "C" status_t _get_thread_info(thread_id id, thread_info *info, size_t size)
{
    if (!info) return B_BAD_VALUE;
    memset(info, 0, size);
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg) return B_ERROR;

    haiku_lock_registry();
    for (int i = 0; i < MAX_THREADS; i++) {
        if (reg->threads[i].active && reg->threads[i].id == id) {
            info->thread = reg->threads[i].id;
            info->team = reg->threads[i].team;
            strncpy(info->name, reg->threads[i].name, B_OS_NAME_LENGTH - 1);
            info->state = reg->threads[i].exited ? B_THREAD_ASLEEP : B_THREAD_RUNNING;
            info->priority = reg->threads[i].priority;
            haiku_unlock_registry();
            return B_OK;
        }
    }
    haiku_unlock_registry();
    info->thread = id;
    info->team = (team_id)getpid();
    strncpy(info->name, "thread", B_OS_NAME_LENGTH - 1);
    info->state = B_THREAD_RUNNING;
    info->priority = B_NORMAL_PRIORITY;
    return B_OK;
}

extern "C" status_t rename_thread(thread_id id, const char *new_name)
{
    if (!new_name) return B_BAD_VALUE;
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (reg) {
        haiku_lock_registry();
        for (int i = 0; i < MAX_THREADS; i++) {
            if (reg->threads[i].active && reg->threads[i].id == id) {
                strncpy(reg->threads[i].name, new_name, B_OS_NAME_LENGTH - 1);
                reg->threads[i].name[B_OS_NAME_LENGTH - 1] = '\0';
                break;
            }
        }
        haiku_unlock_registry();
    }
    pthread_setname_np(pthread_self(), new_name);
    return B_OK;
}
