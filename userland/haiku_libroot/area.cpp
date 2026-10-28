/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "haiku_ipc.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define PAGE_SIZE 4096

static inline size_t round_to_page(size_t size)
{
    return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static int haiku_prot_to_posix(uint32_t protection)
{
    int prot = 0;
    if (protection & B_READ_AREA)
        prot |= PROT_READ;
    if (protection & B_WRITE_AREA)
        prot |= PROT_WRITE;
    if (protection & B_EXECUTE_AREA)
        prot |= PROT_EXEC;
    return prot;
}

extern "C" area_id create_area(const char *name, void **startAddress,
                               uint32 addressSpec, size_t size, uint32 lock,
                               uint32 protection)
{
    if (!name || !startAddress || size == 0)
        return B_BAD_VALUE;

    size = round_to_page(size);

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_NO_MEMORY;

    haiku_lock_registry();

    int slot = -1;
    for (int i = 0; i < MAX_AREAS; i++) {
        if (!reg->areas[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        haiku_unlock_registry();
        return B_NO_MEMORY;
    }

    area_id new_id = reg->next_area_id++;
    char shm_name[64];
    snprintf(shm_name, sizeof(shm_name), "%s%d", HAIKU_AREA_SHM_PREFIX, new_id);

    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        haiku_unlock_registry();
        return B_ERROR;
    }

    if (ftruncate(fd, size) != 0) {
        close(fd);
        shm_unlink(shm_name);
        haiku_unlock_registry();
        return B_ERROR;
    }

    int prot = haiku_prot_to_posix(protection);
    int flags = MAP_SHARED;
    void* hint = (addressSpec == B_EXACT_ADDRESS) ? *startAddress : nullptr;
    if (addressSpec == B_EXACT_ADDRESS)
        flags |= MAP_FIXED;

    void* mapped = mmap(hint, size, prot, flags, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED) {
        shm_unlink(shm_name);
        haiku_unlock_registry();
        return B_NO_MEMORY;
    }

    struct HaikuAreaEntry* entry = &reg->areas[slot];
    entry->active = true;
    entry->id = new_id;
    entry->creator_team = (team_id)getpid();
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->shm_name, sizeof(entry->shm_name), "%s", shm_name);
    entry->size = size;
    entry->lock = lock;
    entry->protection = protection;
    entry->creator_addr = mapped;

    *startAddress = mapped;

    haiku_unlock_registry();
    return new_id;
}

extern "C" area_id clone_area(const char *name, void **destAddress,
                              uint32 addressSpec, uint32 protection, area_id source)
{
    if (!name || !destAddress)
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_NO_MEMORY;

    haiku_lock_registry();

    const struct HaikuAreaEntry* src_entry = nullptr;
    for (int i = 0; i < MAX_AREAS; i++) {
        if (reg->areas[i].active && reg->areas[i].id == source) {
            src_entry = &reg->areas[i];
            break;
        }
    }

    if (!src_entry) {
        haiku_unlock_registry();
        return B_BAD_VALUE;
    }

    int slot = -1;
    for (int i = 0; i < MAX_AREAS; i++) {
        if (!reg->areas[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        haiku_unlock_registry();
        return B_NO_MEMORY;
    }

    int fd = shm_open(src_entry->shm_name, O_RDWR, 0666);
    if (fd < 0) {
        haiku_unlock_registry();
        return B_ERROR;
    }

    int prot = haiku_prot_to_posix(protection);
    int flags = MAP_SHARED;
    void* hint = (addressSpec == B_EXACT_ADDRESS) ? *destAddress : nullptr;
    if (addressSpec == B_EXACT_ADDRESS)
        flags |= MAP_FIXED;

    void* mapped = mmap(hint, src_entry->size, prot, flags, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED) {
        haiku_unlock_registry();
        return B_NO_MEMORY;
    }

    area_id new_id = reg->next_area_id++;
    struct HaikuAreaEntry* entry = &reg->areas[slot];
    entry->active = true;
    entry->id = new_id;
    entry->creator_team = (team_id)getpid();
    strncpy(entry->name, name, B_OS_NAME_LENGTH - 1);
    entry->name[B_OS_NAME_LENGTH - 1] = '\0';
    strncpy(entry->shm_name, src_entry->shm_name, sizeof(entry->shm_name) - 1);
    entry->size = src_entry->size;
    entry->lock = src_entry->lock;
    entry->protection = protection;
    entry->creator_addr = mapped;

    *destAddress = mapped;

    haiku_unlock_registry();
    return new_id;
}

extern "C" area_id find_area(const char *name)
{
    if (!name)
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    area_id id = B_NAME_NOT_FOUND;
    for (int i = 0; i < MAX_AREAS; i++) {
        if (reg->areas[i].active && strcmp(reg->areas[i].name, name) == 0) {
            id = reg->areas[i].id;
            break;
        }
    }
    haiku_unlock_registry();
    return id;
}

extern "C" area_id area_for(void *address)
{
    if (!address)
        return B_ERROR;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    area_id id = B_ERROR;
    uintptr_t target = (uintptr_t)address;

    for (int i = 0; i < MAX_AREAS; i++) {
        if (reg->areas[i].active && reg->areas[i].creator_team == (team_id)getpid()) {
            uintptr_t start = (uintptr_t)reg->areas[i].creator_addr;
            uintptr_t end = start + reg->areas[i].size;
            if (target >= start && target < end) {
                id = reg->areas[i].id;
                break;
            }
        }
    }
    haiku_unlock_registry();
    return id;
}

extern "C" status_t delete_area(area_id id)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    for (int i = 0; i < MAX_AREAS; i++) {
        if (reg->areas[i].active && reg->areas[i].id == id) {
            void* addr = reg->areas[i].creator_addr;
            size_t size = reg->areas[i].size;
            char shm_name[64];
            snprintf(shm_name, sizeof(shm_name), "%s", reg->areas[i].shm_name);

            reg->areas[i].active = false;
            haiku_unlock_registry();

            munmap(addr, size);
            shm_unlink(shm_name);
            return B_OK;
        }
    }
    haiku_unlock_registry();
    return B_BAD_VALUE;
}

extern "C" status_t resize_area(area_id id, size_t newSize)
{
    // Resizing shared cross-process areas in-place is not supported.
    // Returning B_ERROR forces ClientMemoryAllocator to allocate a fresh dedicated chunk.
    return B_ERROR;
}

extern "C" status_t set_area_protection(area_id id, uint32 newProtection)
{
    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    for (int i = 0; i < MAX_AREAS; i++) {
        if (reg->areas[i].active && reg->areas[i].id == id) {
            int prot = haiku_prot_to_posix(newProtection);
            int rc = mprotect(reg->areas[i].creator_addr, reg->areas[i].size, prot);
            if (rc == 0) {
                reg->areas[i].protection = newProtection;
                haiku_unlock_registry();
                return B_OK;
            }
            haiku_unlock_registry();
            return B_ERROR;
        }
    }
    haiku_unlock_registry();
    return B_BAD_VALUE;
}

extern "C" status_t _get_area_info(area_id id, area_info *info, size_t size)
{
    if (!info || size < sizeof(area_info))
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    for (int i = 0; i < MAX_AREAS; i++) {
        if (reg->areas[i].active && reg->areas[i].id == id) {
            info->area = id;
            snprintf(info->name, sizeof(info->name), "%s", reg->areas[i].name);
            info->size = reg->areas[i].size;
            info->lock = reg->areas[i].lock;
            info->protection = reg->areas[i].protection;
            info->team = reg->areas[i].creator_team;
            info->ram_size = (uint32)reg->areas[i].size;
            info->copy_count = 1;
            info->in_count = 1;
            info->out_count = 0;
            info->address = reg->areas[i].creator_addr;
            haiku_unlock_registry();
            return B_OK;
        }
    }
    haiku_unlock_registry();
    return B_BAD_VALUE;
}

extern "C" status_t _get_next_area_info(team_id team, ssize_t *cookie,
                                       area_info *info, size_t size)
{
    if (!cookie || !info || size < sizeof(area_info))
        return B_BAD_VALUE;

    struct HaikuSharedRegistry* reg = haiku_get_registry();
    if (!reg)
        return B_ERROR;

    haiku_lock_registry();
    ssize_t idx = *cookie;
    while (idx < MAX_AREAS) {
        if (reg->areas[idx].active && (team == 0 || reg->areas[idx].creator_team == team)) {
            info->area = reg->areas[idx].id;
            snprintf(info->name, sizeof(info->name), "%s", reg->areas[idx].name);
            info->size = reg->areas[idx].size;
            info->lock = reg->areas[idx].lock;
            info->protection = reg->areas[idx].protection;
            info->team = reg->areas[idx].creator_team;
            info->address = reg->areas[idx].creator_addr;
            *cookie = idx + 1;
            haiku_unlock_registry();
            return B_OK;
        }
        idx++;
    }
    haiku_unlock_registry();
    return B_BAD_VALUE;
}
