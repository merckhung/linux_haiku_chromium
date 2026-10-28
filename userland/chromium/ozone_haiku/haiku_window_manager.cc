// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#include "ui/ozone/platform/haiku/haiku_window_manager.h"

#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/platform/haiku/haiku_window.h"

namespace ui {

HaikuWindowManager::HaikuWindowManager() = default;

HaikuWindowManager::~HaikuWindowManager() = default;

gfx::AcceleratedWidget HaikuWindowManager::AddWindow(HaikuWindow* window,
                                                     void* native_handle) {
  base::AutoLock auto_lock(lock_);
  gfx::AcceleratedWidget widget =
      static_cast<gfx::AcceleratedWidget>(next_widget_id_++);
  windows_[widget] = window;
  native_handles_[widget] = native_handle;
  return widget;
}

void HaikuWindowManager::RemoveWindow(gfx::AcceleratedWidget widget,
                                      HaikuWindow* window) {
  base::AutoLock auto_lock(lock_);
  windows_.erase(widget);
  native_handles_.erase(widget);
}

HaikuWindow* HaikuWindowManager::GetWindow(gfx::AcceleratedWidget widget) {
  base::AutoLock auto_lock(lock_);
  auto it = windows_.find(widget);
  return it != windows_.end() ? it->second : nullptr;
}

void* HaikuWindowManager::GetNativeWindowHandle(gfx::AcceleratedWidget widget) {
  base::AutoLock auto_lock(lock_);
  auto it = native_handles_.find(widget);
  return it != native_handles_.end() ? it->second : nullptr;
}

gfx::AcceleratedWidget HaikuWindowManager::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) {
  base::AutoLock auto_lock(lock_);
  for (const auto& pair : windows_) {
    const HaikuWindow* window = pair.second;
    if (window && window->GetBoundsInPixels().Contains(point)) {
      return pair.first;
    }
  }
  return gfx::kNullAcceleratedWidget;
}

}  // namespace ui
