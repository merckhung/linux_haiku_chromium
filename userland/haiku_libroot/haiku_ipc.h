/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#ifndef _HAIKU_IPC_H_
#define _HAIKU_IPC_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>

#include <OS.h>

#define HAIKU_IPC_SHM_PATH      "/haiku_ipc_registry"
#define HAIKU_AREA_SHM_PREFIX   "/haiku_area_"

#define MAX_AREAS       1024
#define MAX_PORTS       1024
#define MAX_SEMS        4096
#define MAX_THREADS     1024

#define PORT_MSG_DATA_SIZE      2048
#define PORT_QUEUE_CAPACITY     64

struct HaikuPortMessage {
    int32_t     code;
    size_t      size;
    team_id     sender_team;
    thread_id   sender_thread;
    uint8_t     data[PORT_MSG_DATA_SIZE];
};

struct HaikuPortEntry {
    bool        active;
    bool        closed;
    port_id     id;
    team_id     owner_team;
    char        name[B_OS_NAME_LENGTH];
    int32_t     capacity;
    int32_t     head;       // next read
    int32_t     tail;       // next write
    int32_t     count;      // messages currently queued
    int32_t     total_read; // total read count
    uint32_t    futex_read; // incremented on write to wake readers
    uint32_t    futex_write;// incremented on read to wake writers
    struct HaikuPortMessage messages[PORT_QUEUE_CAPACITY];
};

struct HaikuAreaEntry {
    bool        active;
    area_id     id;
    team_id     creator_team;
    char        name[B_OS_NAME_LENGTH];
    char        shm_name[64];
    size_t      size;
    uint32_t    lock;
    uint32_t    protection;
    void*       creator_addr;
};

struct HaikuSemEntry {
    bool        active;
    sem_id      id;
    team_id     owner_team;
    char        name[B_OS_NAME_LENGTH];
    int32_t     count;
    uint32_t    futex_val;
    thread_id   latest_holder;
};

struct HaikuThreadMailbox {
    bool        has_data;
    int32_t     code;
    size_t      size;
    thread_id   sender;
    uint8_t     data[512];
};

struct HaikuThreadEntry {
    bool        active;
    bool        started;
    bool        exited;
    thread_id   id;
    pid_t       linux_tid;
    team_id     team;
    char        name[B_OS_NAME_LENGTH];
    int32_t     priority;
    status_t    exit_code;
    uint32_t    start_futex;
    uint32_t    exit_futex;
    struct HaikuThreadMailbox mailbox;
    uint32_t    mailbox_futex;
};

struct HaikuSharedRegistry {
    pthread_mutex_t lock;
    int32_t         next_area_id;
    int32_t         next_port_id;
    int32_t         next_sem_id;
    int32_t         next_thread_id;
    struct HaikuAreaEntry   areas[MAX_AREAS];
    struct HaikuPortEntry   ports[MAX_PORTS];
    struct HaikuSemEntry    sems[MAX_SEMS];
    struct HaikuThreadEntry threads[MAX_THREADS];
};

#ifdef __cplusplus
extern "C" {
#endif

struct HaikuSharedRegistry* haiku_get_registry(void);
void haiku_lock_registry(void);
void haiku_unlock_registry(void);

// Linux futex helpers
int haiku_futex_wait(uint32_t* uaddr, uint32_t val, const struct timespec* timeout);
int haiku_futex_wake(uint32_t* uaddr, int count);

// Microseconds conversion
bigtime_t haiku_timespec_to_usecs(const struct timespec* ts);
void haiku_usecs_to_timespec(bigtime_t usecs, struct timespec* ts);

#ifdef __cplusplus
}
#endif

#endif /* _HAIKU_IPC_H_ */
