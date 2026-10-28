/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "haiku_ipc.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

static int get_sem_slot(struct HaikuSharedRegistry* reg, sem_id id)
{
    for (int i = 0; i < MAX_SEMS; i++) {
        if (reg->sems[i].active && reg->sems[i].id == id)
            return i;
    }
    return -1;
}

extern "C" sem_id create_sem(int32 count, const char *name)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();

    int slot = -1;
    for (int i = 0; i < MAX_SEMS; i++) {
        if (!reg->sems[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        haiku_unlock_registry();
        return B_NO_MORE_SEMS;
    }

    sem_id new_id = reg->next_sem_id++;
    struct HaikuSemEntry* entry = &reg->sems[slot];
    entry->active = true;
    entry->id = new_id;
    entry->owner_team = (team_id)getpid();
    if (name) {
        strncpy(entry->name, name, B_OS_NAME_LENGTH - 1);
        entry->name[B_OS_NAME_LENGTH - 1] = '\0';
    } else {
        entry->name[0] = '\0';
    }
    entry->count = count;
    entry->futex_val = 0;
    entry->latest_holder = 0;

    haiku_unlock_registry();
    return new_id;
}

extern "C" status_t delete_sem(sem_id id)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_sem_slot(reg, id);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_SEM_ID;
    }

    reg->sems[slot].active = false;
    reg->sems[slot].futex_val++;
    haiku_unlock_registry();

    haiku_futex_wake(&reg->sems[slot].futex_val, 1000);
    return B_OK;
}

extern "C" status_t acquire_sem(sem_id id)
{
    return acquire_sem_etc(id, 1, 0, B_INFINITE_TIMEOUT);
}

extern "C" status_t acquire_sem_etc(sem_id id, int32 count, uint32 flags, bigtime_t timeout)
{
    if (count <= 0)
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    struct timespec deadline_ts;
    bool has_timeout = false;
    if (timeout != B_INFINITE_TIMEOUT) {
        has_timeout = true;
        clock_gettime(CLOCK_MONOTONIC, &deadline_ts);
        if (flags & B_ABSOLUTE_TIMEOUT) {
            haiku_usecs_to_timespec(timeout, &deadline_ts);
        } else {
            struct timespec delta;
            haiku_usecs_to_timespec(timeout, &delta);
            deadline_ts.tv_sec += delta.tv_sec;
            deadline_ts.tv_nsec += delta.tv_nsec;
            if (deadline_ts.tv_nsec >= 1000000000L) {
                deadline_ts.tv_sec += 1;
                deadline_ts.tv_nsec -= 1000000000L;
            }
        }
    }

    while (true) {
        haiku_lock_registry();
        int slot = get_sem_slot(reg, id);
        if (slot < 0) {
            haiku_unlock_registry();
            return B_BAD_SEM_ID;
        }

        struct HaikuSemEntry* s = &reg->sems[slot];
        if (s->count >= count) {
            s->count -= count;
            s->latest_holder = 0;
            haiku_unlock_registry();
            return B_OK;
        }

        if (has_timeout && timeout <= 0) {
            haiku_unlock_registry();
            return B_WOULD_BLOCK;
        }

        uint32_t expected = s->futex_val;
        haiku_unlock_registry();

        struct timespec rel_ts;
        struct timespec* wait_ts = nullptr;
        if (has_timeout) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec > deadline_ts.tv_sec ||
                (now.tv_sec == deadline_ts.tv_sec && now.tv_nsec >= deadline_ts.tv_nsec)) {
                return B_TIMED_OUT;
            }
            rel_ts.tv_sec = deadline_ts.tv_sec - now.tv_sec;
            rel_ts.tv_nsec = deadline_ts.tv_nsec - now.tv_nsec;
            if (rel_ts.tv_nsec < 0) {
                rel_ts.tv_sec -= 1;
                rel_ts.tv_nsec += 1000000000L;
            }
            wait_ts = &rel_ts;
        }

        int rc = haiku_futex_wait(&s->futex_val, expected, wait_ts);
        if (rc < 0 && errno == ETIMEDOUT)
            return B_TIMED_OUT;
    }
}

extern "C" status_t release_sem(sem_id id)
{
    return release_sem_etc(id, 1, 0);
}

extern "C" status_t release_sem_etc(sem_id id, int32 count, uint32 flags)
{
    if (count <= 0)
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_sem_slot(reg, id);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_SEM_ID;
    }

    struct HaikuSemEntry* s = &reg->sems[slot];
    s->count += count;
    s->futex_val++;
    haiku_unlock_registry();

    haiku_futex_wake(&s->futex_val, count);
    return B_OK;
}

extern "C" status_t get_sem_count(sem_id id, int32 *threadCount)
{
    if (!threadCount)
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_sem_slot(reg, id);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_SEM_ID;
    }

    *threadCount = reg->sems[slot].count;
    haiku_unlock_registry();
    return B_OK;
}

extern "C" status_t set_sem_owner(sem_id id, team_id team)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_sem_slot(reg, id);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_SEM_ID;
    }

    reg->sems[slot].owner_team = team;
    haiku_unlock_registry();
    return B_OK;
}

extern "C" status_t _get_sem_info(sem_id id, struct sem_info *info, size_t infoSize)
{
    if (!info || infoSize < sizeof(sem_info))
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_sem_slot(reg, id);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_SEM_ID;
    }

    info->sem = id;
    info->team = reg->sems[slot].owner_team;
    snprintf(info->name, sizeof(info->name), "%s", reg->sems[slot].name);
    info->count = reg->sems[slot].count;
    info->latest_holder = reg->sems[slot].latest_holder;

    haiku_unlock_registry();
    return B_OK;
}
