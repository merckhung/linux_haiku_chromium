// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#ifndef UI_OZONE_PLATFORM_HAIKU_CLIENT_NATIVE_PIXMAP_FACTORY_HAIKU_H_
#define UI_OZONE_PLATFORM_HAIKU_CLIENT_NATIVE_PIXMAP_FACTORY_HAIKU_H_

namespace gfx {
class ClientNativePixmapFactory;
}

namespace ui {

// Constructor hook for use in constructor_list.cc
gfx::ClientNativePixmapFactory* CreateClientNativePixmapFactoryHaiku();

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_CLIENT_NATIVE_PIXMAP_FACTORY_HAIKU_H_
