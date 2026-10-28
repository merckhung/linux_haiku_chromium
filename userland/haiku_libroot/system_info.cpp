/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include <OS.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>

extern "C" status_t get_system_info(system_info *info)
{
    if (!info)
        return B_BAD_VALUE;

    memset(info, 0, sizeof(*info));

    struct sysinfo si;
    const long page_size = sysconf(_SC_PAGESIZE);
    if (sysinfo(&si) != 0 || page_size <= 0)
        return B_ERROR;
    {
        const uint64 unit = si.mem_unit;
        time_t now = time(nullptr);
        info->boot_time = ((bigtime_t)(now - si.uptime)) * 1000000LL;
        info->max_pages = (uint64)si.totalram * unit / page_size;
        info->free_memory = (uint64)si.freeram * unit;
        info->used_pages = (uint64)(si.totalram - si.freeram) * unit / page_size;
        info->max_swap_pages = (uint64)si.totalswap * unit / page_size;
        info->free_swap_pages = (uint64)si.freeswap * unit / page_size;
    }

    info->cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    return B_OK;
}

extern "C" status_t _get_cpu_info_etc(uint32 first_cpu, uint32 cpu_count, cpu_info *info, size_t size)
{
    const long configured = sysconf(_SC_NPROCESSORS_CONF);
    const long ticks = sysconf(_SC_CLK_TCK);
    if (!info || size != sizeof(cpu_info) || configured <= 0 || ticks <= 0
            || first_cpu >= (uint32)configured
            || cpu_count > (uint32)configured - first_cpu)
        return B_BAD_VALUE;

    FILE* stat = fopen("/proc/stat", "r");
    if (!stat)
        return B_ERROR;
    memset(info, 0, cpu_count * sizeof(cpu_info));
    char line[1024];
    while (fgets(line, sizeof(line), stat)) {
        unsigned cpu;
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        if (sscanf(line, "cpu%u %llu %llu %llu %llu %llu %llu %llu %llu",
                &cpu, &user, &nice, &system, &idle, &iowait, &irq, &softirq,
                &steal) != 9 || cpu < first_cpu || cpu - first_cpu >= cpu_count)
            continue;
        cpu_info& value = info[cpu - first_cpu];
        value.enabled = true;
        // Idle, I/O wait and hypervisor steal time are not useful CPU work.
        value.active_time = (user + nice + system + irq + softirq)
            * 1000000ULL / ticks;
        char path[128];
        snprintf(path, sizeof(path),
            "/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq", cpu);
        FILE* frequency = fopen(path, "r");
        unsigned long long khz;
        if (frequency) {
            if (fscanf(frequency, "%llu", &khz) == 1)
                value.current_frequency = khz * 1000ULL;
            fclose(frequency);
        }
        // Frequency remains zero when the kernel does not expose it (e.g. QEMU).
    }
    const bool failed = ferror(stat);
    fclose(stat);
    return failed ? B_ERROR : B_OK;
}

extern "C" status_t kill_team(team_id team)
{
    if (kill((pid_t)team, SIGKILL) == 0)
        return B_OK;
    return B_BAD_TEAM_ID;
}

extern "C" status_t _get_team_info(team_id id, team_info *info, size_t size)
{
    if (!info || size < sizeof(team_info))
        return B_BAD_VALUE;

    const pid_t pid = id == B_CURRENT_TEAM ? getpid() : id;
    if (pid <= 0)
        return B_BAD_TEAM_ID;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE* status = fopen(path, "r");
    if (!status)
        return B_BAD_TEAM_ID;
    memset(info, 0, sizeof(*info));
    info->team = pid;
    info->debugger_nub_thread = -1;
    info->debugger_nub_port = -1;
    char line[512];
    while (fgets(line, sizeof(line), status)) {
        unsigned real, effective;
        int count;
        if (strncmp(line, "Name:\t", 6) == 0) {
            line[strcspn(line, "\n")] = 0;
            snprintf(info->name, sizeof(info->name), "%s", line + 6);
        } else if (sscanf(line, "Threads: %d", &count) == 1) {
            info->thread_count = count;
        } else if (sscanf(line, "PPid: %d", &count) == 1) {
            info->parent = count;
        } else if (sscanf(line, "Uid: %u %u", &real, &effective) == 2) {
            info->real_uid = real;
            info->uid = effective;
        } else if (sscanf(line, "Gid: %u %u", &real, &effective) == 2) {
            info->real_gid = real;
            info->gid = effective;
        }
    }
    const bool failed = ferror(status);
    fclose(status);
    if (failed)
        return B_ERROR;
    info->group_id = getpgid(pid);
    info->session_id = getsid(pid);
    // Image/area counts and start_time are not available through this adapter.
    return B_OK;
}

extern "C" status_t _get_next_team_info(int32 *cookie, team_info *info, size_t size)
{
    return B_BAD_VALUE;
}
