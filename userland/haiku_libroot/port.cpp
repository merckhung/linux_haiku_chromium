/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "haiku_ipc.h"

#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

static int get_port_slot(struct HaikuSharedRegistry* reg, port_id id)
{
    for (int i = 0; i < MAX_PORTS; i++) {
        if (reg->ports[i].active && reg->ports[i].id == id)
            return i;
    }
    return -1;
}

extern "C" port_id create_port(int32 capacity, const char *name)
{
    if (capacity <= 0 || capacity > PORT_QUEUE_CAPACITY)
        capacity = PORT_QUEUE_CAPACITY;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();

    int slot = -1;
    for (int i = 0; i < MAX_PORTS; i++) {
        if (!reg->ports[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        haiku_unlock_registry();
        return B_NO_MORE_PORTS;
    }

    port_id new_id = reg->next_port_id++;
    struct HaikuPortEntry* entry = &reg->ports[slot];
    entry->active = true;
    entry->closed = false;
    entry->id = new_id;
    entry->owner_team = (team_id)getpid();
    if (name) {
        strncpy(entry->name, name, B_OS_NAME_LENGTH - 1);
        entry->name[B_OS_NAME_LENGTH - 1] = '\0';
    } else {
        entry->name[0] = '\0';
    }
    entry->capacity = capacity;
    entry->head = 0;
    entry->tail = 0;
    entry->count = 0;
    entry->total_read = 0;
    entry->futex_read = 0;
    entry->futex_write = 0;

    haiku_unlock_registry();
    return new_id;
}

extern "C" port_id find_port(const char *name)
{
    if (!name)
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    port_id id = B_NAME_NOT_FOUND;
    for (int i = 0; i < MAX_PORTS; i++) {
        if (reg->ports[i].active && strcmp(reg->ports[i].name, name) == 0) {
            id = reg->ports[i].id;
            break;
        }
    }
    haiku_unlock_registry();
    return id;
}

extern "C" status_t write_port(port_id port, int32 code, const void *buffer, size_t bufferSize)
{
    return write_port_etc(port, code, buffer, bufferSize, 0, B_INFINITE_TIMEOUT);
}

extern "C" status_t write_port_etc(port_id port, int32 code, const void *buffer,
                                  size_t bufferSize, uint32 flags, bigtime_t timeout)
{
    if (bufferSize > PORT_MSG_DATA_SIZE)
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
        int slot = get_port_slot(reg, port);
        if (slot < 0 || reg->ports[slot].closed) {
            haiku_unlock_registry();
            return B_BAD_PORT_ID;
        }

        struct HaikuPortEntry* p = &reg->ports[slot];
        if (p->count < p->capacity) {
            // Write message
            struct HaikuPortMessage* msg = &p->messages[p->tail];
            msg->code = code;
            msg->size = bufferSize;
            msg->sender_team = (team_id)getpid();
            msg->sender_thread = 0;
            if (bufferSize > 0 && buffer) {
                memcpy(msg->data, buffer, bufferSize);
            }
            p->tail = (p->tail + 1) % p->capacity;
            p->count++;
            p->futex_read++;

            haiku_unlock_registry();
            haiku_futex_wake(&p->futex_read, 1);
            return B_OK;
        }

        if (has_timeout && timeout <= 0) {
            haiku_unlock_registry();
            return B_WOULD_BLOCK;
        }

        uint32_t expected = p->futex_write;
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

        int rc = haiku_futex_wait(&p->futex_write, expected, wait_ts);
        if (rc < 0 && errno == ETIMEDOUT)
            return B_TIMED_OUT;
    }
}

extern "C" ssize_t read_port(port_id port, int32 *code, void *buffer, size_t bufferSize)
{
    return read_port_etc(port, code, buffer, bufferSize, 0, B_INFINITE_TIMEOUT);
}

extern "C" ssize_t read_port_etc(port_id port, int32 *code, void *buffer,
                                size_t bufferSize, uint32 flags, bigtime_t timeout)
{
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
        int slot = get_port_slot(reg, port);
        if (slot < 0) {
            haiku_unlock_registry();
            return B_BAD_PORT_ID;
        }

        struct HaikuPortEntry* p = &reg->ports[slot];
        if (p->count > 0) {
            struct HaikuPortMessage* msg = &p->messages[p->head];
            if (code)
                *code = msg->code;

            size_t copy_size = (bufferSize < msg->size) ? bufferSize : msg->size;
            if (copy_size > 0 && buffer) {
                memcpy(buffer, msg->data, copy_size);
            }
            ssize_t actual_size = (ssize_t)msg->size;

            p->head = (p->head + 1) % p->capacity;
            p->count--;
            p->total_read++;
            p->futex_write++;

            haiku_unlock_registry();
            haiku_futex_wake(&p->futex_write, 1);
            return actual_size;
        }

        if (p->closed) {
            haiku_unlock_registry();
            return B_BAD_PORT_ID;
        }

        if (has_timeout && timeout <= 0) {
            haiku_unlock_registry();
            return B_WOULD_BLOCK;
        }

        uint32_t expected = p->futex_read;
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

        int rc = haiku_futex_wait(&p->futex_read, expected, wait_ts);
        if (rc < 0 && errno == ETIMEDOUT)
            return B_TIMED_OUT;
    }
}

extern "C" status_t close_port(port_id port)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_port_slot(reg, port);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_PORT_ID;
    }
    reg->ports[slot].closed = true;
    reg->ports[slot].futex_read++;
    reg->ports[slot].futex_write++;
    haiku_unlock_registry();

    haiku_futex_wake(&reg->ports[slot].futex_read, 1000);
    haiku_futex_wake(&reg->ports[slot].futex_write, 1000);
    return B_OK;
}

extern "C" status_t delete_port(port_id port)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_port_slot(reg, port);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_PORT_ID;
    }
    reg->ports[slot].active = false;
    reg->ports[slot].closed = true;
    reg->ports[slot].futex_read++;
    reg->ports[slot].futex_write++;
    haiku_unlock_registry();

    haiku_futex_wake(&reg->ports[slot].futex_read, 1000);
    haiku_futex_wake(&reg->ports[slot].futex_write, 1000);
    return B_OK;
}

extern "C" ssize_t port_buffer_size(port_id port)
{
    return port_buffer_size_etc(port, 0, B_INFINITE_TIMEOUT);
}

extern "C" ssize_t port_buffer_size_etc(port_id port, uint32 flags, bigtime_t timeout)
{
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
        int slot = get_port_slot(reg, port);
        if (slot < 0) {
            haiku_unlock_registry();
            return B_BAD_PORT_ID;
        }

        struct HaikuPortEntry* p = &reg->ports[slot];
        if (p->count > 0) {
            ssize_t sz = (ssize_t)p->messages[p->head].size;
            haiku_unlock_registry();
            return sz;
        }

        if (p->closed) {
            haiku_unlock_registry();
            return B_BAD_PORT_ID;
        }

        if (has_timeout && timeout <= 0) {
            haiku_unlock_registry();
            return B_WOULD_BLOCK;
        }

        uint32_t expected = p->futex_read;
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

        int rc = haiku_futex_wait(&p->futex_read, expected, wait_ts);
        if (rc < 0 && errno == ETIMEDOUT)
            return B_TIMED_OUT;
    }
}

extern "C" ssize_t port_count(port_id port)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_port_slot(reg, port);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_PORT_ID;
    }
    ssize_t cnt = reg->ports[slot].count;
    haiku_unlock_registry();
    return cnt;
}

extern "C" status_t set_port_owner(port_id port, team_id team)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_port_slot(reg, port);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_PORT_ID;
    }
    reg->ports[slot].owner_team = team;
    haiku_unlock_registry();
    return B_OK;
}

extern "C" status_t _get_port_info(port_id port, port_info *info, size_t portInfoSize)
{
    if (!info || portInfoSize < sizeof(port_info))
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_port_slot(reg, port);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_PORT_ID;
    }

    info->port = port;
    info->team = reg->ports[slot].owner_team;
    snprintf(info->name, sizeof(info->name), "%s", reg->ports[slot].name);
    info->capacity = reg->ports[slot].capacity;
    info->queue_count = reg->ports[slot].count;
    info->total_count = reg->ports[slot].total_read;
    haiku_unlock_registry();
    return B_OK;
}

extern "C" status_t _get_next_port_info(team_id team, int32 *cookie,
                                       port_info *info, size_t portInfoSize)
{
    if (!cookie || !info || portInfoSize < sizeof(port_info))
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int32 idx = *cookie;
    while (idx < MAX_PORTS) {
        if (reg->ports[idx].active && (team == 0 || reg->ports[idx].owner_team == team)) {
            info->port = reg->ports[idx].id;
            info->team = reg->ports[idx].owner_team;
            snprintf(info->name, sizeof(info->name), "%s", reg->ports[idx].name);
            info->capacity = reg->ports[idx].capacity;
            info->queue_count = reg->ports[idx].count;
            info->total_count = reg->ports[idx].total_read;
            *cookie = idx + 1;
            haiku_unlock_registry();
            return B_OK;
        }
        idx++;
    }
    haiku_unlock_registry();
    return B_BAD_VALUE;
}

extern "C" status_t _get_port_message_info_etc(port_id port,
    port_message_info *info, size_t infoSize, uint32 flags,
    bigtime_t timeout)
{
    if (!info)
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    int slot = get_port_slot(reg, port);
    if (slot < 0) {
        haiku_unlock_registry();
        return B_BAD_PORT_ID;
    }

    struct HaikuPortEntry* p = &reg->ports[slot];
    if (p->count > 0) {
        info->size = (size_t)p->messages[p->head].size;
        info->sender = getuid();
        info->sender_group = getgid();
        info->sender_team = getpid();
        haiku_unlock_registry();
        return B_OK;
    }
    haiku_unlock_registry();
    return B_WOULD_BLOCK;
}
