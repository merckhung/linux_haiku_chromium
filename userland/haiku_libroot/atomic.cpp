/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include <SupportDefs.h>

extern "C" {

int32 atomic_add(int32 *value, int32 addValue)
{
    return __sync_fetch_and_add(value, addValue);
}

int32 atomic_and(int32 *value, int32 andValue)
{
    return __sync_fetch_and_and(value, andValue);
}

int32 atomic_or(int32 *value, int32 orValue)
{
    return __sync_fetch_and_or(value, orValue);
}

int32 atomic_set(int32 *value, int32 setTo)
{
    int32 old;
    do {
        old = *value;
    } while (!__sync_bool_compare_and_swap(value, old, setTo));
    return old;
}

int32 atomic_test_and_set(int32 *value, int32 setTo, int32 testValue)
{
    return __sync_val_compare_and_swap(value, testValue, setTo);
}

int32 atomic_get(int32 *value)
{
    return __sync_fetch_and_add(value, 0);
}

int64 atomic_add64(int64 *value, int64 addValue)
{
    return __sync_fetch_and_add(value, addValue);
}

int64 atomic_and64(int64 *value, int64 andValue)
{
    return __sync_fetch_and_and(value, andValue);
}

int64 atomic_or64(int64 *value, int64 orValue)
{
    return __sync_fetch_and_or(value, orValue);
}

int64 atomic_set64(int64 *value, int64 setTo)
{
    int64 old;
    do {
        old = *value;
    } while (!__sync_bool_compare_and_swap(value, old, setTo));
    return old;
}

int64 atomic_test_and_set64(int64 *value, int64 setTo, int64 testValue)
{
    return __sync_val_compare_and_swap(value, testValue, setTo);
}

int64 atomic_get64(int64 *value)
{
    return __sync_fetch_and_add(value, 0);
}

}
