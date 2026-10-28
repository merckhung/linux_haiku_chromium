// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#include "ui/ozone/platform/haiku/haiku_screen.h"

#include "base/check_op.h"
#include "ui/display/display.h"
#include "ui/display/display_finder.h"
#include "ui/ozone/platform/haiku/haiku_capi.h"
#include "ui/ozone/platform/haiku/haiku_window.h"
#include "ui/ozone/platform/haiku/haiku_window_manager.h"

namespace ui {

HaikuScreen::HaikuScreen(HaikuWindowManager* window_manager)
    : window_manager_(window_manager) {
  int width = 0;
  int height = 0;
  const auto& capi = HaikuCapi::Get();
  CHECK(capi.available && capi.get_screen_size)
      << "Haiku screen API is unavailable";
  CHECK_EQ(capi.get_screen_size(&width, &height), 0)
      << "Cannot query the Haiku display";
  CHECK_GT(width, 0);
  CHECK_GT(height, 0);

  display::Display primary_display(1, gfx::Rect(0, 0, width, height));
  primary_display.set_work_area(gfx::Rect(0, 0, width, height));
  primary_display.set_device_scale_factor(1.0f);
  primary_display.set_color_depth(24);
  display_list_.AddDisplay(primary_display, display::DisplayList::Type::PRIMARY);
}

HaikuScreen::~HaikuScreen() = default;

const std::vector<display::Display>& HaikuScreen::GetAllDisplays() const {
  return display_list_.displays();
}

display::Display HaikuScreen::GetPrimaryDisplay() const {
  auto iter = display_list_.GetPrimaryDisplayIterator();
  if (iter == display_list_.displays().end()) {
    return display::Display::GetDefaultDisplay();
  }
  return *iter;
}

display::Display HaikuScreen::GetDisplayForAcceleratedWidget(
    gfx::AcceleratedWidget widget) const {
  return GetPrimaryDisplay();
}

gfx::Point HaikuScreen::GetCursorScreenPoint() const {
  return gfx::Point(0, 0);
}

gfx::AcceleratedWidget HaikuScreen::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) const {
  if (window_manager_) {
    return window_manager_->GetAcceleratedWidgetAtScreenPoint(point);
  }
  return gfx::kNullAcceleratedWidget;
}

display::Display HaikuScreen::GetDisplayNearestPoint(
    const gfx::Point& point) const {
  return GetPrimaryDisplay();
}

display::Display HaikuScreen::GetDisplayMatching(
    const gfx::Rect& match_rect) const {
  const display::Display* matching =
      display::FindDisplayWithBiggestIntersection(display_list_.displays(),
                                                  match_rect);
  return matching ? *matching : GetPrimaryDisplay();
}

bool HaikuScreen::IsScreenSaverActive() const {
  return false;
}

base::TimeDelta HaikuScreen::CalculateIdleTime() const {
  return base::Seconds(0);
}

void HaikuScreen::AddObserver(display::DisplayObserver* observer) {
  display_list_.AddObserver(observer);
}

void HaikuScreen::RemoveObserver(display::DisplayObserver* observer) {
  display_list_.RemoveObserver(observer);
}

}  // namespace ui
