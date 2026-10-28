// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#include "ui/ozone/platform/haiku/client_native_pixmap_factory_haiku.h"

#include "ui/ozone/common/stub_client_native_pixmap_factory.h"

namespace ui {

gfx::ClientNativePixmapFactory* CreateClientNativePixmapFactoryHaiku() {
  return CreateStubClientNativePixmapFactory();
}

}  // namespace ui
