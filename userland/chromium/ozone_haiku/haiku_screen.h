// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_SCREEN_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_SCREEN_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/display/display_list.h"
#include "ui/gfx/geometry/point.h"
#include "ui/ozone/public/platform_screen.h"

namespace ui {

class HaikuWindowManager;

class HaikuScreen : public PlatformScreen {
 public:
  explicit HaikuScreen(HaikuWindowManager* window_manager);

  HaikuScreen(const HaikuScreen&) = delete;
  HaikuScreen& operator=(const HaikuScreen&) = delete;

  ~HaikuScreen() override;

  // ui::PlatformScreen:
  const std::vector<display::Display>& GetAllDisplays() const override;
  display::Display GetPrimaryDisplay() const override;
  display::Display GetDisplayForAcceleratedWidget(
      gfx::AcceleratedWidget widget) const override;
  gfx::Point GetCursorScreenPoint() const override;
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point) const override;
  display::Display GetDisplayNearestPoint(
      const gfx::Point& point) const override;
  display::Display GetDisplayMatching(
      const gfx::Rect& match_rect) const override;
  bool IsScreenSaverActive() const override;
  base::TimeDelta CalculateIdleTime() const override;
  void AddObserver(display::DisplayObserver* observer) override;
  void RemoveObserver(display::DisplayObserver* observer) override;

 private:
  raw_ptr<HaikuWindowManager> window_manager_;
  display::DisplayList display_list_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_SCREEN_H_
