// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_WINDOW_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_WINDOW_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_pump_for_ui.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/ozone/platform/haiku/haiku_capi.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_delegate.h"

namespace ui {

class HaikuWindowManager;

class HaikuWindow : public PlatformWindow,
                    public base::MessagePumpForUI::FdWatcher {
 public:
  HaikuWindow(PlatformWindowDelegate* delegate,
              HaikuWindowManager* manager,
              const gfx::Rect& bounds, bool is_tooltip);

  HaikuWindow(const HaikuWindow&) = delete;
  HaikuWindow& operator=(const HaikuWindow&) = delete;

  ~HaikuWindow() override;

  // PlatformWindow:
  void Show(bool inactive) override;
  void Hide() override;
  void Close() override;
  bool IsVisible() const override;
  void PrepareForShutdown() override;
  void SetBoundsInPixels(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInPixels() const override;
  void SetBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInDIP() const override;
  void SetTitle(const std::u16string& title) override;
  void SetCapture() override;
  void ReleaseCapture() override;
  void SetFullscreen(bool fullscreen, int64_t target_display_id) override;
  bool HasCapture() const override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  PlatformWindowState GetPlatformWindowState() const override;
  void Activate() override;
  void Deactivate() override;
  void SetUseNativeFrame(bool use_native_frame) override;
  bool ShouldUseNativeFrame() const override;
  void SetCursor(scoped_refptr<PlatformCursor> cursor) override;
  void MoveCursorTo(const gfx::Point& location) override;
  void ConfineCursorToBounds(const gfx::Rect& bounds) override;
  void SetRestoredBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetRestoredBoundsInDIP() const override;
  void SetWindowIcons(const gfx::ImageSkia& window_icon,
                      const gfx::ImageSkia& app_icon) override;
  void SizeConstraintsChanged() override;

  // base::MessagePumpForUI::FdWatcher:
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;

  gfx::AcceleratedWidget widget() const { return widget_; }
  void* native_handle() const { return native_handle_; }

 private:
  void DispatchHaikuEvent(const HaikuOzoneEvent& ev);
  void UpdateBounds(const gfx::Rect& bounds);
  void UpdateWindowState(PlatformWindowState new_window_state);
  void ZoomWindowBounds();
  void RestoreWindowBounds();

  raw_ptr<PlatformWindowDelegate> delegate_ = nullptr;
  raw_ptr<HaikuWindowManager> manager_ = nullptr;
  gfx::Rect bounds_;
  gfx::AcceleratedWidget widget_ = gfx::kNullAcceleratedWidget;

  raw_ptr<void> native_handle_ = nullptr;
  int event_fd_ = -1;
  base::MessagePumpForUI::FdWatchController fd_watch_controller_;

  bool visible_ = false;
  bool is_tooltip_ = false;
  bool has_capture_ = false;
  bool use_native_frame_ = true;
  gfx::PointF last_mouse_location_;
  std::optional<gfx::Rect> restored_bounds_;
  PlatformWindowState window_state_ = PlatformWindowState::kNormal;

  base::WeakPtrFactory<HaikuWindow> weak_ptr_factory_{this};
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_WINDOW_H_
