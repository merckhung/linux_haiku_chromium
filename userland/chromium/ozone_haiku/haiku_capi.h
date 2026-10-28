// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_CAPI_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_CAPI_H_

#include <stddef.h>
#include <stdint.h>

namespace ui {

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

struct HaikuOzoneEvent {
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
};

struct HaikuCapi {
  static const HaikuCapi& Get();

  bool available = false;

  int (*init_app)(const char* signature) = nullptr;
  void* (*create_window)(int x,
                         int y,
                         int width,
                         int height,
                         const char* title,
                         int* out_event_fd) = nullptr;
  void (*destroy_window)(void* win_handle) = nullptr;
  int (*set_window_type)(void* win_handle, int type) = nullptr;
  void (*show_window)(void* win_handle) = nullptr;
  void (*hide_window)(void* win_handle) = nullptr;
  void (*set_window_bounds)(void* win_handle,
                            int x,
                            int y,
                            int width,
                            int height) = nullptr;
  void (*get_window_bounds)(void* win_handle,
                            int* x,
                            int* y,
                            int* width,
                            int* height) = nullptr;
  void (*set_window_title)(void* win_handle, const char* title) = nullptr;
  void (*activate_window)(void* win_handle) = nullptr;
  void (*minimize_window)(void* win_handle, int minimize) = nullptr;
  int (*poll_event)(void* win_handle, HaikuOzoneEvent* out_event) = nullptr;
  int (*present_pixels)(void* win_handle,
                        const uint8_t* bgra_pixels,
                        int src_row_bytes,
                        int width,
                        int height,
                        int damage_x,
                        int damage_y,
                        int damage_w,
                        int damage_h) = nullptr;
  int (*get_screen_size)(int* out_width, int* out_height) = nullptr;
  int (*get_ui_color)(int which, uint32_t* out_argb) = nullptr;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_CAPI_H_
