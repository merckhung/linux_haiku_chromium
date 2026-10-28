// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_SURFACE_FACTORY_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_SURFACE_FACTORY_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/ozone/public/gl_ozone.h"
#include "ui/ozone/public/surface_factory_ozone.h"

namespace ui {

class HaikuWindowManager;

class HaikuSurfaceFactory : public SurfaceFactoryOzone {
 public:
  explicit HaikuSurfaceFactory(HaikuWindowManager* window_manager);

  HaikuSurfaceFactory(const HaikuSurfaceFactory&) = delete;
  HaikuSurfaceFactory& operator=(const HaikuSurfaceFactory&) = delete;

  ~HaikuSurfaceFactory() override;

  // SurfaceFactoryOzone:
  std::vector<gl::GLImplementationParts> GetAllowedGLImplementations() override;
  GLOzone* GetGLOzone(const gl::GLImplementationParts& implementation) override;
  std::unique_ptr<SurfaceOzoneCanvas> CreateCanvasForWidget(
      gfx::AcceleratedWidget widget) override;
  scoped_refptr<gfx::NativePixmap> CreateNativePixmap(
      gfx::AcceleratedWidget widget,
      gpu::VulkanDeviceQueue* device_queue,
      gfx::Size size,
      viz::SharedImageFormat format,
      gfx::BufferUsage usage,
      std::optional<gfx::Size> framebuffer_size) override;

 private:
  raw_ptr<HaikuWindowManager> window_manager_;
  std::unique_ptr<GLOzone> gl_ozone_egl_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_SURFACE_FACTORY_H_
