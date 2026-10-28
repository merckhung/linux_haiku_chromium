// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#include "ui/ozone/platform/haiku/haiku_surface_factory.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "skia/ext/legacy_display_globals.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/core/SkSurfaceProps.h"
#include "ui/gfx/native_pixmap.h"
#include "ui/gfx/vsync_provider.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/ozone/common/egl_util.h"
#include "ui/ozone/common/gl_ozone_egl.h"
#include "ui/ozone/common/gl_surface_egl_readback.h"
#include "ui/ozone/platform/haiku/haiku_capi.h"
#include "ui/ozone/platform/haiku/haiku_window_manager.h"
#include "ui/ozone/public/surface_ozone_canvas.h"

namespace ui {

namespace {

class HaikuCanvasSurface : public SurfaceOzoneCanvas {
 public:
  HaikuCanvasSurface(HaikuWindowManager* window_manager,
                     gfx::AcceleratedWidget widget)
      : window_manager_(window_manager), widget_(widget) {}

  ~HaikuCanvasSurface() override = default;

  // SurfaceOzoneCanvas:
  void ResizeCanvas(const gfx::Size& viewport_size, float scale) override {
    SkSurfaceProps props = skia::LegacyDisplayGlobals::GetSkSurfaceProps();
    surface_ = SkSurfaces::Raster(
        SkImageInfo::Make(viewport_size.width(), viewport_size.height(),
                          kBGRA_8888_SkColorType, kPremul_SkAlphaType),
        &props);
  }

  SkCanvas* GetCanvas() override {
    return surface_ ? surface_->getCanvas() : nullptr;
  }

  void PresentCanvas(const gfx::Rect& damage) override {
    if (!surface_ || !window_manager_) {
      return;
    }
    void* native_handle = window_manager_->GetNativeWindowHandle(widget_);
    const auto& capi = HaikuCapi::Get();
    if (!native_handle || !capi.available || !capi.present_pixels) {
      return;
    }

    SkPixmap pixmap;
    if (surface_->peekPixels(&pixmap) && pixmap.addr()) {
      capi.present_pixels(native_handle,
                           static_cast<const uint8_t*>(pixmap.addr()),
                           static_cast<int>(pixmap.rowBytes()), pixmap.width(),
                          pixmap.height(), damage.x(), damage.y(),
                          damage.width(), damage.height());
    }
  }

  std::unique_ptr<gfx::VSyncProvider> CreateVSyncProvider() override {
    return nullptr;
  }

 private:
  raw_ptr<HaikuWindowManager> window_manager_;
  gfx::AcceleratedWidget widget_;
  sk_sp<SkSurface> surface_;
};

class GLSurfaceEglReadbackHaiku : public GLSurfaceEglReadback {
 public:
  GLSurfaceEglReadbackHaiku(gl::GLDisplayEGL* display,
                            HaikuWindowManager* window_manager,
                            gfx::AcceleratedWidget widget)
      : GLSurfaceEglReadback(display),
        window_manager_(window_manager),
        widget_(widget) {}

  GLSurfaceEglReadbackHaiku(const GLSurfaceEglReadbackHaiku&) = delete;
  GLSurfaceEglReadbackHaiku& operator=(const GLSurfaceEglReadbackHaiku&) =
      delete;

 private:
  ~GLSurfaceEglReadbackHaiku() override = default;

  // GLSurfaceEglReadback:
  bool HandlePixels(uint8_t* pixels) override {
    if (!pixels || !window_manager_) {
      return true;
    }
    void* native_handle = window_manager_->GetNativeWindowHandle(widget_);
    const auto& capi = HaikuCapi::Get();
    if (!native_handle || !capi.available || !capi.present_pixels) {
      return true;
    }
    const gfx::Size size = GetSize();
    capi.present_pixels(native_handle, pixels, size.width() * 4, size.width(),
                        size.height(), 0, 0, size.width(), size.height());
    return true;
  }

  raw_ptr<HaikuWindowManager> window_manager_;
  gfx::AcceleratedWidget widget_;
};

class GLOzoneEGLHaiku : public GLOzoneEGL {
 public:
  explicit GLOzoneEGLHaiku(HaikuWindowManager* window_manager)
      : window_manager_(window_manager) {}

  GLOzoneEGLHaiku(const GLOzoneEGLHaiku&) = delete;
  GLOzoneEGLHaiku& operator=(const GLOzoneEGLHaiku&) = delete;

  ~GLOzoneEGLHaiku() override = default;

  // GLOzone:
  scoped_refptr<gl::GLSurface> CreateViewGLSurface(
      gl::GLDisplay* display,
      gfx::AcceleratedWidget window) override {
    return gl::InitializeGLSurface(
        base::MakeRefCounted<GLSurfaceEglReadbackHaiku>(
            display->GetAs<gl::GLDisplayEGL>(), window_manager_, window));
  }

  scoped_refptr<gl::GLSurface> CreateOffscreenGLSurface(
      gl::GLDisplay* display,
      const gfx::Size& size) override {
    return gl::InitializeGLSurface(
        base::MakeRefCounted<gl::PbufferGLSurfaceEGL>(
            display->GetAs<gl::GLDisplayEGL>(), size));
  }

 protected:
  // GLOzoneEGL:
  gl::EGLDisplayPlatform GetNativeDisplay() override {
    return gl::EGLDisplayPlatform(EGL_DEFAULT_DISPLAY);
  }

  bool LoadGLES2Bindings(
      const gl::GLImplementationParts& implementation) override {
    return LoadDefaultEGLGLES2Bindings(implementation);
  }

 private:
  raw_ptr<HaikuWindowManager> window_manager_;
};

}  // namespace

HaikuSurfaceFactory::HaikuSurfaceFactory(HaikuWindowManager* window_manager)
    : window_manager_(window_manager),
      gl_ozone_egl_(std::make_unique<GLOzoneEGLHaiku>(window_manager)) {}

HaikuSurfaceFactory::~HaikuSurfaceFactory() = default;

std::vector<gl::GLImplementationParts>
HaikuSurfaceFactory::GetAllowedGLImplementations() {
  return std::vector<gl::GLImplementationParts>{
      gl::GLImplementationParts(gl::kGLImplementationEGLANGLE),
      gl::GLImplementationParts(gl::kGLImplementationEGLGLES2),
  };
}

GLOzone* HaikuSurfaceFactory::GetGLOzone(
    const gl::GLImplementationParts& implementation) {
  switch (implementation.gl) {
    case gl::kGLImplementationEGLGLES2:
    case gl::kGLImplementationEGLANGLE:
      return gl_ozone_egl_.get();
    default:
      return nullptr;
  }
}

std::unique_ptr<SurfaceOzoneCanvas> HaikuSurfaceFactory::CreateCanvasForWidget(
    gfx::AcceleratedWidget widget) {
  return std::make_unique<HaikuCanvasSurface>(window_manager_, widget);
}

scoped_refptr<gfx::NativePixmap> HaikuSurfaceFactory::CreateNativePixmap(
    gfx::AcceleratedWidget widget,
    gpu::VulkanDeviceQueue* device_queue,
    gfx::Size size,
    viz::SharedImageFormat format,
    gfx::BufferUsage usage,
    std::optional<gfx::Size> framebuffer_size) {
  return nullptr;
}

}  // namespace ui
