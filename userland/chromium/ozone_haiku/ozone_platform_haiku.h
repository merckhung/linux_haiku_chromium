// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#ifndef UI_OZONE_PLATFORM_HAIKU_OZONE_PLATFORM_HAIKU_H_
#define UI_OZONE_PLATFORM_HAIKU_OZONE_PLATFORM_HAIKU_H_

#include "ui/ozone/public/ozone_platform.h"

namespace ui {

// Constructor hook for use in ozone_platform_list.cc
OzonePlatform* CreateOzonePlatformHaiku();

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_OZONE_PLATFORM_HAIKU_H_
