/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <execinfo.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" {

__attribute__((noipa))
char* strdup(const char* str)
{
    uintptr_t addr = (uintptr_t)str;
    if (addr == 0)
        return NULL;
    const char* s = (const char*)addr;
    size_t len = 0;
    while (s[len] != '\0')
        len++;
    char* copy = (char*)malloc(len + 1);
    if (copy != NULL) {
        for (size_t i = 0; i <= len; i++)
            copy[i] = s[i];
    }
    return copy;
}

void debugger(const char *message)
{
    fprintf(stderr, "\n*** HAIKU DEBUGGER TRIGGERED ***\n%s\n", message ? message : "(no message)");
    fflush(stderr);
    void *callstack[128];
    int frames = backtrace(callstack, 128);
    backtrace_symbols_fd(callstack, frames, STDERR_FILENO);
    fsync(STDERR_FILENO);
    abort();
}

int disable_debugger(int state)
{
    return 0;
}

int debug_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vfprintf(stderr, format, args);
    va_end(args);
    return ret;
}

int debug_vprintf(const char *format, va_list args)
{
    return vfprintf(stderr, format, args);
}

void ktrace_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void ktrace_vprintf(const char *format, va_list args)
{
    vfprintf(stderr, format, args);
}

status_t debug_thread(thread_id thread)
{
    return B_NOT_SUPPORTED;
}

}
