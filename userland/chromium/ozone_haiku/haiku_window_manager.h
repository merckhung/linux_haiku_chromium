// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_WINDOW_MANAGER_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_WINDOW_MANAGER_H_

#include <stdint.h>
#include <map>

#include "base/synchronization/lock.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/native_ui_types.h"

namespace ui {

class HaikuWindow;

class HaikuWindowManager {
 public:
  HaikuWindowManager();

  HaikuWindowManager(const HaikuWindowManager&) = delete;
  HaikuWindowManager& operator=(const HaikuWindowManager&) = delete;

  ~HaikuWindowManager();

  gfx::AcceleratedWidget AddWindow(HaikuWindow* window, void* native_handle);
  void RemoveWindow(gfx::AcceleratedWidget widget, HaikuWindow* window);

  HaikuWindow* GetWindow(gfx::AcceleratedWidget widget);
  void* GetNativeWindowHandle(gfx::AcceleratedWidget widget);

  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point);

 private:
  base::Lock lock_;
  int32_t next_widget_id_ = 1;
  std::map<gfx::AcceleratedWidget, HaikuWindow*> windows_;
  std::map<gfx::AcceleratedWidget, void*> native_handles_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_WINDOW_MANAGER_H_
