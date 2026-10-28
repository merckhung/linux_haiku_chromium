/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * SPDX-License-Identifier: MIT
 */

#ifndef USERLAND_HAIKU_LIBBE_HAIKU_OZONE_CAPI_H_
#define USERLAND_HAIKU_LIBBE_HAIKU_OZONE_CAPI_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum HaikuOzoneEventType {
    HAIKU_OZONE_EVENT_NONE = 0,
    HAIKU_OZONE_EVENT_MOUSE_DOWN = 1,
    HAIKU_OZONE_EVENT_MOUSE_UP = 2,
    HAIKU_OZONE_EVENT_MOUSE_MOVE = 3,
    HAIKU_OZONE_EVENT_MOUSE_WHEEL = 4,
    HAIKU_OZONE_EVENT_KEY_DOWN = 5,
    HAIKU_OZONE_EVENT_KEY_UP = 6,
    HAIKU_OZONE_EVENT_WINDOW_MOVED = 7,
    HAIKU_OZONE_EVENT_WINDOW_RESIZED = 8,
    HAIKU_OZONE_EVENT_WINDOW_ACTIVATED = 9,
    HAIKU_OZONE_EVENT_WINDOW_CLOSE_REQUEST = 10,
};

typedef struct HaikuOzoneEvent {
    int32_t type;
    float x;
    float y;
    float delta_x;
    float delta_y;
    uint32_t buttons;
    uint32_t modifiers;
    int32_t key_code;
    int32_t raw_char;
    char utf8[16];
    int32_t width;
    int32_t height;
    int32_t active;
    int32_t clicks;
} HaikuOzoneEvent;

int haiku_ozone_init_app(const char* signature);

void* haiku_ozone_create_window(int x, int y, int width, int height,
                                const char* title, int* out_event_fd);
void haiku_ozone_destroy_window(void* win_handle);
// type 0: normal window; type 1: non-activating Chromium tooltip.
int haiku_ozone_set_window_type(void* win_handle, int type);

void haiku_ozone_show_window(void* win_handle);
void haiku_ozone_hide_window(void* win_handle);
void haiku_ozone_set_window_bounds(void* win_handle, int x, int y, int width, int height);
void haiku_ozone_get_window_bounds(void* win_handle, int* x, int* y, int* width, int* height);
void haiku_ozone_set_window_title(void* win_handle, const char* title);
void haiku_ozone_activate_window(void* win_handle);
void haiku_ozone_minimize_window(void* win_handle, int minimize);

int haiku_ozone_poll_event(void* win_handle, HaikuOzoneEvent* out_event);

int haiku_ozone_present_pixels(void* win_handle,
                               const uint8_t* bgra_pixels,
                               int src_row_bytes,
                               int width,
                               int height,
                               int damage_x,
                               int damage_y,
                               int damage_w,
                               int damage_h);

int haiku_ozone_get_screen_size(int* out_width, int* out_height);
int haiku_ozone_get_ui_color(int which, uint32_t* out_argb);

#ifdef __cplusplus
}
#endif

#endif  // USERLAND_HAIKU_LIBBE_HAIKU_OZONE_CAPI_H_
