// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#include "ui/ozone/platform/haiku/haiku_capi.h"

#include <dlfcn.h>

#include "base/logging.h"
#include "base/no_destructor.h"

namespace ui {

namespace {

HaikuCapi LoadHaikuCapi() {
  HaikuCapi api;
  void* handle = dlopen("libbe.so", RTLD_NOW | RTLD_GLOBAL);
  if (!handle) {
    handle = dlopen("/lib/libbe.so", RTLD_NOW | RTLD_GLOBAL);
  }
  if (!handle) {
    LOG(ERROR) << "HaikuCapi: failed to dlopen libbe.so: " << dlerror();
    return api;
  }

  api.init_app = reinterpret_cast<decltype(api.init_app)>(
      dlsym(handle, "haiku_ozone_init_app"));
  api.create_window = reinterpret_cast<decltype(api.create_window)>(
      dlsym(handle, "haiku_ozone_create_window"));
  api.destroy_window = reinterpret_cast<decltype(api.destroy_window)>(
      dlsym(handle, "haiku_ozone_destroy_window"));
  api.set_window_type = reinterpret_cast<decltype(api.set_window_type)>(
      dlsym(handle, "haiku_ozone_set_window_type"));
  api.show_window = reinterpret_cast<decltype(api.show_window)>(
      dlsym(handle, "haiku_ozone_show_window"));
  api.hide_window = reinterpret_cast<decltype(api.hide_window)>(
      dlsym(handle, "haiku_ozone_hide_window"));
  api.set_window_bounds = reinterpret_cast<decltype(api.set_window_bounds)>(
      dlsym(handle, "haiku_ozone_set_window_bounds"));
  api.get_window_bounds = reinterpret_cast<decltype(api.get_window_bounds)>(
      dlsym(handle, "haiku_ozone_get_window_bounds"));
  api.set_window_title = reinterpret_cast<decltype(api.set_window_title)>(
      dlsym(handle, "haiku_ozone_set_window_title"));
  api.activate_window = reinterpret_cast<decltype(api.activate_window)>(
      dlsym(handle, "haiku_ozone_activate_window"));
  api.minimize_window = reinterpret_cast<decltype(api.minimize_window)>(
      dlsym(handle, "haiku_ozone_minimize_window"));
  api.poll_event = reinterpret_cast<decltype(api.poll_event)>(
      dlsym(handle, "haiku_ozone_poll_event"));
  api.present_pixels = reinterpret_cast<decltype(api.present_pixels)>(
      dlsym(handle, "haiku_ozone_present_pixels"));
  api.get_screen_size = reinterpret_cast<decltype(api.get_screen_size)>(
      dlsym(handle, "haiku_ozone_get_screen_size"));
  api.get_ui_color = reinterpret_cast<decltype(api.get_ui_color)>(
      dlsym(handle, "haiku_ozone_get_ui_color"));

  api.available = api.init_app && api.create_window && api.destroy_window &&
                  api.set_window_type &&
                  api.show_window && api.hide_window && api.set_window_bounds &&
                  api.get_window_bounds && api.set_window_title &&
                  api.poll_event && api.present_pixels && api.get_screen_size;
  if (!api.available) {
    LOG(ERROR) << "HaikuCapi: missing symbols in libbe.so";
  }
  return api;
}

}  // namespace

// static
const HaikuCapi& HaikuCapi::Get() {
  static const HaikuCapi instance = LoadHaikuCapi();
  return instance;
}

}  // namespace ui
