// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#include "ui/ozone/platform/haiku/ozone_platform_haiku.h"

#include <memory>

#include "base/command_line.h"
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "build/chromecast_buildflags.h"
#include "ui/base/cursor/cursor_factory.h"
#include "ui/base/ime/input_method_minimal.h"
#include "ui/display/types/native_display_delegate.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/events/ozone/layout/stub/stub_keyboard_layout_engine.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/ozone/common/bitmap_cursor_factory.h"
#include "ui/ozone/common/stub_overlay_manager.h"
#include "ui/ozone/platform/haiku/haiku_capi.h"
#include "ui/ozone/platform/haiku/haiku_screen.h"
#include "ui/ozone/platform/haiku/haiku_surface_factory.h"
#include "ui/ozone/platform/haiku/haiku_window.h"
#include "ui/ozone/platform/haiku/haiku_window_manager.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/ozone/public/input_controller.h"
#include "ui/ozone/public/stub_input_controller.h"
#include "ui/ozone/public/system_input_injector.h"
#include "ui/platform_window/platform_window_init_properties.h"

namespace ui {

namespace {

class HaikuPlatformEventSource : public PlatformEventSource {
 public:
  HaikuPlatformEventSource() = default;
  HaikuPlatformEventSource(const HaikuPlatformEventSource&) = delete;
  HaikuPlatformEventSource& operator=(const HaikuPlatformEventSource&) = delete;
  ~HaikuPlatformEventSource() override = default;
};

class OzonePlatformHaikuImpl : public OzonePlatform {
 public:
  OzonePlatformHaikuImpl() = default;

  OzonePlatformHaikuImpl(const OzonePlatformHaikuImpl&) = delete;
  OzonePlatformHaikuImpl& operator=(const OzonePlatformHaikuImpl&) = delete;

  ~OzonePlatformHaikuImpl() override = default;

  // OzonePlatform:
  ui::SurfaceFactoryOzone* GetSurfaceFactoryOzone() override {
    return surface_factory_.get();
  }
  OverlayManagerOzone* GetOverlayManager() override {
    return overlay_manager_.get();
  }
  CursorFactory* GetCursorFactory() override { return cursor_factory_.get(); }
  InputController* GetInputController() override {
    return input_controller_.get();
  }
  GpuPlatformSupportHost* GetGpuPlatformSupportHost() override {
    return gpu_platform_support_host_.get();
  }
  std::unique_ptr<SystemInputInjector> CreateSystemInputInjector() override {
    return nullptr;
  }
  std::unique_ptr<PlatformWindow> CreatePlatformWindow(
      PlatformWindowDelegate* delegate,
      PlatformWindowInitProperties properties) override {
    return std::make_unique<HaikuWindow>(delegate, window_manager_.get(),
                                         properties.bounds,
                                         properties.type == PlatformWindowType::kTooltip);
  }
  bool IsWindowCompositingSupported() const override { return false; }
  std::unique_ptr<display::NativeDisplayDelegate> CreateNativeDisplayDelegate()
      override {
    return nullptr;
  }
  std::unique_ptr<PlatformScreen> CreateScreen() override {
    return std::make_unique<HaikuScreen>(window_manager_.get());
  }
  void InitScreen(PlatformScreen* screen) override {}
  std::unique_ptr<InputMethod> CreateInputMethod(
      ImeKeyEventDispatcher* ime_key_event_dispatcher,
      gfx::AcceleratedWidget widget) override {
    return std::make_unique<InputMethodMinimal>(ime_key_event_dispatcher);
  }

#if BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS)
  const PlatformProperties& GetPlatformProperties() override {
    static base::NoDestructor<OzonePlatform::PlatformProperties> properties;
    static bool initialized = false;
    if (!initialized) {
      // Use Haiku's native BWindow frame (DefaultDecorator yellow tab)
      properties->custom_frame_pref_default = false;
      initialized = true;
    }
    return *properties;
  }
#endif

  const PlatformRuntimeProperties& GetPlatformRuntimeProperties() override {
    static OzonePlatform::PlatformRuntimeProperties properties;
    properties.supports_server_side_window_decorations = true;
    return properties;
  }

  bool InitializeUI(const InitParams& params) override {
    base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
    if (cmd && !cmd->HasSwitch("in-process-gpu")) {
      cmd->AppendSwitch("in-process-gpu");
    }

    const auto& capi = HaikuCapi::Get();
    if (!capi.available || !capi.init_app ||
        capi.init_app("application/x-vnd.chromium-browser") != 0) {
      return false;
    }

    window_manager_ = std::make_unique<HaikuWindowManager>();
    surface_factory_ =
        std::make_unique<HaikuSurfaceFactory>(window_manager_.get());
    if (!PlatformEventSource::GetInstance()) {
      platform_event_source_ = std::make_unique<HaikuPlatformEventSource>();
    }
    keyboard_layout_engine_ = std::make_unique<StubKeyboardLayoutEngine>();
    KeyboardLayoutEngineManager::SetKeyboardLayoutEngine(
        keyboard_layout_engine_.get());

    overlay_manager_ = std::make_unique<StubOverlayManager>();
    input_controller_ = std::make_unique<StubInputController>();
    cursor_factory_ = std::make_unique<BitmapCursorFactory>();
    gpu_platform_support_host_.reset(CreateStubGpuPlatformSupportHost());

    return true;
  }

  void InitializeGPU(const InitParams& params) override {
    if (!window_manager_) {
      window_manager_ = std::make_unique<HaikuWindowManager>();
    }
    if (!surface_factory_) {
      surface_factory_ =
          std::make_unique<HaikuSurfaceFactory>(window_manager_.get());
    }
  }

 private:
  std::unique_ptr<KeyboardLayoutEngine> keyboard_layout_engine_;
  std::unique_ptr<HaikuWindowManager> window_manager_;
  std::unique_ptr<HaikuSurfaceFactory> surface_factory_;
  std::unique_ptr<PlatformEventSource> platform_event_source_;
  std::unique_ptr<CursorFactory> cursor_factory_;
  std::unique_ptr<InputController> input_controller_;
  std::unique_ptr<GpuPlatformSupportHost> gpu_platform_support_host_;
  std::unique_ptr<OverlayManagerOzone> overlay_manager_;
};

}  // namespace

OzonePlatform* CreateOzonePlatformHaiku() {
  return new OzonePlatformHaikuImpl();
}

}  // namespace ui
