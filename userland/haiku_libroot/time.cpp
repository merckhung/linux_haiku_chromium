/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include <OS.h>
#include <time.h>
#include <sys/time.h>

extern "C" bigtime_t system_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return ((bigtime_t)ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000LL);
}

extern "C" nanotime_t system_time_nsecs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return ((nanotime_t)ts.tv_sec * 1000000000LL) + ts.tv_nsec;
}

extern "C" unsigned long real_time_clock(void)
{
    return (unsigned long)time(nullptr);
}

extern "C" void set_real_time_clock(unsigned long secsSinceJan1st1970)
{
    struct timeval tv;
    tv.tv_sec = (time_t)secsSinceJan1st1970;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

extern "C" bigtime_t real_time_clock_usecs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((bigtime_t)ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000LL);
}
