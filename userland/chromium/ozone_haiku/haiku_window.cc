// Copyright 2026 The Chromium Authors
// SPDX-License-Identifier: MIT

#include "ui/ozone/platform/haiku/haiku_window.h"

#include "base/check_op.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/current_thread.h"
#include "base/time/time.h"
#include "ui/base/cursor/platform_cursor.h"
#include "ui/display/screen.h"
#include "ui/display/types/display_constants.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes_posix.h"
#include "ui/events/types/event_type.h"
#include "ui/ozone/platform/haiku/haiku_window_manager.h"

namespace ui {

namespace {

int ConvertHaikuModifiers(uint32_t haiku_mods, uint32_t buttons) {
  int flags = EF_NONE;
  // Haiku modifiers from InterfaceDefs.h:
  // B_SHIFT_KEY = 0x01, B_COMMAND_KEY = 0x02, B_CONTROL_KEY = 0x04,
  // B_CAPS_LOCK = 0x08, B_OPTION_KEY = 0x40
  if (haiku_mods & 0x01) {
    flags |= EF_SHIFT_DOWN;
  }
  if (haiku_mods & 0x06) {
    flags |= EF_CONTROL_DOWN;
  }
  if (haiku_mods & 0x40) {
    flags |= EF_ALT_DOWN;
  }
  if (haiku_mods & 0x08) {
    flags |= EF_CAPS_LOCK_ON;
  }
  if (buttons & 0x01) {
    flags |= EF_LEFT_MOUSE_BUTTON;
  }
  if (buttons & 0x02) {
    flags |= EF_RIGHT_MOUSE_BUTTON;
  }
  if (buttons & 0x04) {
    flags |= EF_MIDDLE_MOUSE_BUTTON;
  }
  return flags;
}

KeyboardCode ConvertHaikuKeyToKeyboardCode(int32_t raw_char,
                                           int32_t haiku_key,
                                           const char* utf8) {
  // Control-modified UTF-8 contains control bytes (Ctrl+L is 0x0c).
  // Shortcuts need the original key; text insertion still uses ev.utf8.
  unsigned char ch = raw_char > 0 && raw_char < 0x80
                         ? static_cast<unsigned char>(raw_char)
                         : (utf8 && utf8[0]
                                ? static_cast<unsigned char>(utf8[0])
                                : 0);
  switch (ch) {
    case 0x08:
      return VKEY_BACK;
    case 0x09:
      return VKEY_TAB;
    case 0x0a:
    case 0x0d:
      return VKEY_RETURN;
    case 0x1b:
      return VKEY_ESCAPE;
    case 0x20:
      return VKEY_SPACE;
    case 0x7f:
      return VKEY_DELETE;
    default:
      break;
  }

  // Haiku navigation key raw_char codes (InterfaceDefs.h):
  // B_HOME=0x01, B_END=0x04, B_INSERT=0x05, B_PAGE_UP=0x0b, B_PAGE_DOWN=0x0c,
  // B_LEFT_ARROW=0x1c, B_RIGHT_ARROW=0x1d, B_UP_ARROW=0x1e, B_DOWN_ARROW=0x1f
  switch (raw_char) {
    case 0x01:
      return VKEY_HOME;
    case 0x04:
      return VKEY_END;
    case 0x05:
      return VKEY_INSERT;
    case 0x0b:
      return VKEY_PRIOR;
    case 0x0c:
      return VKEY_NEXT;
    case 0x1c:
      return VKEY_LEFT;
    case 0x1d:
      return VKEY_RIGHT;
    case 0x1e:
      return VKEY_UP;
    case 0x1f:
      return VKEY_DOWN;
    default:
      break;
  }

  if (ch >= 'a' && ch <= 'z') {
    return static_cast<KeyboardCode>(VKEY_A + (ch - 'a'));
  }
  if (ch >= 'A' && ch <= 'Z') {
    return static_cast<KeyboardCode>(VKEY_A + (ch - 'A'));
  }
  if (ch >= '0' && ch <= '9') {
    return static_cast<KeyboardCode>(VKEY_0 + (ch - '0'));
  }
  switch (ch) {
    case '-':
    case '_':
      return VKEY_OEM_MINUS;
    case '=':
    case '+':
      return VKEY_OEM_PLUS;
    case '[':
    case '{':
      return VKEY_OEM_4;
    case ']':
    case '}':
      return VKEY_OEM_6;
    case '\\':
    case '|':
      return VKEY_OEM_5;
    case ';':
    case ':':
      return VKEY_OEM_1;
    case '\'':
    case '"':
      return VKEY_OEM_7;
    case ',':
    case '<':
      return VKEY_OEM_COMMA;
    case '.':
    case '>':
      return VKEY_OEM_PERIOD;
    case '/':
    case '?':
      return VKEY_OEM_2;
    case '`':
    case '~':
      return VKEY_OEM_3;
    default:
      break;
  }

  return VKEY_UNKNOWN;
}

}  // namespace

HaikuWindow::HaikuWindow(PlatformWindowDelegate* delegate,
                         HaikuWindowManager* manager,
                         const gfx::Rect& bounds, bool is_tooltip)
    : delegate_(delegate),
      manager_(manager),
      bounds_(bounds),
      fd_watch_controller_(FROM_HERE) {
  is_tooltip_ = is_tooltip;
  if (bounds_.width() <= 0 || bounds_.height() <= 0) {
    bounds_.SetRect(60, 40, 1024, 680);
  } else if (bounds_.x() == 0 && bounds_.y() == 0) {
    bounds_.set_origin(gfx::Point(60, 40));
  }
  last_mouse_location_.SetPoint(bounds_.width() / 2.0f,
                                bounds_.height() / 2.0f);

  const auto& capi = HaikuCapi::Get();
  if (capi.available) {
    native_handle_ =
        capi.create_window(bounds_.x(), bounds_.y(), bounds_.width(),
                           bounds_.height(), "Chromium", &event_fd_);
    if (native_handle_)
      CHECK_EQ(capi.set_window_type(native_handle_, is_tooltip_ ? 1 : 0), 0);
  }

  widget_ = manager_->AddWindow(this, native_handle_);

  if (event_fd_ >= 0 && base::CurrentUIThread::IsSet()) {
    base::CurrentUIThread::Get()->WatchFileDescriptor(
        event_fd_, true, base::MessagePumpForUI::WATCH_READ,
        &fd_watch_controller_, this);
  }

  delegate_->OnAcceleratedWidgetAvailable(widget_);
}

HaikuWindow::~HaikuWindow() {
  fd_watch_controller_.StopWatchingFileDescriptor();
  manager_->RemoveWindow(widget_, this);
  const auto& capi = HaikuCapi::Get();
  if (capi.available && native_handle_) {
    capi.destroy_window(native_handle_);
    native_handle_ = nullptr;
  }
}

void HaikuWindow::Show(bool inactive) {
  visible_ = true;
  const auto& capi = HaikuCapi::Get();
  if (capi.available && native_handle_) {
    capi.show_window(native_handle_);
    if (!inactive && !is_tooltip_ && capi.activate_window) {
      capi.activate_window(native_handle_);
    }
  }
}

void HaikuWindow::Hide() {
  visible_ = false;
  const auto& capi = HaikuCapi::Get();
  if (capi.available && native_handle_) {
    capi.hide_window(native_handle_);
  }
}

void HaikuWindow::Close() {
  delegate_->OnClosed();
}

bool HaikuWindow::IsVisible() const {
  return visible_;
}

void HaikuWindow::PrepareForShutdown() {
  fd_watch_controller_.StopWatchingFileDescriptor();
}

void HaikuWindow::SetBoundsInPixels(const gfx::Rect& bounds) {
  const auto& capi = HaikuCapi::Get();
  if (capi.available && native_handle_) {
    capi.set_window_bounds(native_handle_, bounds.x(), bounds.y(),
                           bounds.width(), bounds.height());
  }
  UpdateBounds(bounds);
}

gfx::Rect HaikuWindow::GetBoundsInPixels() const {
  return bounds_;
}

void HaikuWindow::SetBoundsInDIP(const gfx::Rect& bounds) {
  SetBoundsInPixels(delegate_->ConvertRectToPixels(bounds));
}

gfx::Rect HaikuWindow::GetBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(bounds_);
}

void HaikuWindow::SetTitle(const std::u16string& title) {
  const auto& capi = HaikuCapi::Get();
  if (capi.available && native_handle_) {
    std::string utf8_title = base::UTF16ToUTF8(title);
    capi.set_window_title(native_handle_, utf8_title.c_str());
  }
}

void HaikuWindow::SetCapture() {
  has_capture_ = true;
}

void HaikuWindow::ReleaseCapture() {
  has_capture_ = false;
}

bool HaikuWindow::HasCapture() const {
  return has_capture_;
}

void HaikuWindow::SetFullscreen(bool fullscreen, int64_t target_display_id) {
  auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
  if (fullscreen) {
    if (window_state_ != PlatformWindowState::kMaximized &&
        window_state_ != PlatformWindowState::kFullScreen) {
      restored_bounds_ = bounds_;
    }
    ZoomWindowBounds();
    if (!weak_ptr) {
      return;
    }
    UpdateWindowState(PlatformWindowState::kFullScreen);
  } else {
    if (window_state_ != PlatformWindowState::kFullScreen) {
      return;
    }
    RestoreWindowBounds();
    if (!weak_ptr) {
      return;
    }
    UpdateWindowState(PlatformWindowState::kNormal);
  }
}

void HaikuWindow::Maximize() {
  if (window_state_ != PlatformWindowState::kMaximized &&
      window_state_ != PlatformWindowState::kFullScreen) {
    auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
    restored_bounds_ = bounds_;
    ZoomWindowBounds();
    if (!weak_ptr) {
      return;
    }
    UpdateWindowState(PlatformWindowState::kMaximized);
  }
}

void HaikuWindow::Minimize() {
  if (window_state_ != PlatformWindowState::kMinimized) {
    const auto& capi = HaikuCapi::Get();
    if (capi.available && native_handle_ && capi.minimize_window) {
      capi.minimize_window(native_handle_, 1);
    }
    UpdateWindowState(PlatformWindowState::kMinimized);
  }
}

void HaikuWindow::Restore() {
  if (window_state_ != PlatformWindowState::kNormal) {
    const auto& capi = HaikuCapi::Get();
    if (capi.available && native_handle_ && capi.minimize_window) {
      capi.minimize_window(native_handle_, 0);
    }
    auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
    RestoreWindowBounds();
    if (!weak_ptr) {
      return;
    }
    UpdateWindowState(PlatformWindowState::kNormal);
  }
}

PlatformWindowState HaikuWindow::GetPlatformWindowState() const {
  return window_state_;
}

void HaikuWindow::Activate() {
  if (is_tooltip_)
    return;
  const auto& capi = HaikuCapi::Get();
  if (capi.available && native_handle_ && capi.activate_window) {
    capi.activate_window(native_handle_);
  }
  delegate_->OnActivationChanged(true);
}

void HaikuWindow::Deactivate() {
  delegate_->OnActivationChanged(false);
}

void HaikuWindow::SetUseNativeFrame(bool use_native_frame) {
  use_native_frame_ = true;
}

bool HaikuWindow::ShouldUseNativeFrame() const {
  return true;
}

void HaikuWindow::SetCursor(scoped_refptr<PlatformCursor> cursor) {}

void HaikuWindow::MoveCursorTo(const gfx::Point& location) {}

void HaikuWindow::ConfineCursorToBounds(const gfx::Rect& bounds) {}

void HaikuWindow::SetRestoredBoundsInDIP(const gfx::Rect& bounds) {
  restored_bounds_ = delegate_->ConvertRectToPixels(bounds);
}

gfx::Rect HaikuWindow::GetRestoredBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(restored_bounds_.value_or(bounds_));
}

void HaikuWindow::SetWindowIcons(const gfx::ImageSkia& window_icon,
                                 const gfx::ImageSkia& app_icon) {}

void HaikuWindow::SizeConstraintsChanged() {}

void HaikuWindow::OnFileCanReadWithoutBlocking(int fd) {
  const auto& capi = HaikuCapi::Get();
  if (!capi.available || !native_handle_) {
    return;
  }

  auto weak_ptr = weak_ptr_factory_.GetWeakPtr();
  HaikuOzoneEvent ev = {};
  while (capi.poll_event(native_handle_, &ev)) {
    DispatchHaikuEvent(ev);
    if (!weak_ptr) {
      return;
    }
  }
}

void HaikuWindow::OnFileCanWriteWithoutBlocking(int fd) {}

void HaikuWindow::DispatchHaikuEvent(const HaikuOzoneEvent& ev) {
  base::TimeTicks now = base::TimeTicks::Now();

  switch (ev.type) {
    case HAIKU_OZONE_EVENT_MOUSE_DOWN:
    case HAIKU_OZONE_EVENT_MOUSE_UP:
    case HAIKU_OZONE_EVENT_MOUSE_MOVE: {
      EventType type = EventType::kMouseMoved;
      if (ev.type == HAIKU_OZONE_EVENT_MOUSE_DOWN) {
        type = EventType::kMousePressed;
      } else if (ev.type == HAIKU_OZONE_EVENT_MOUSE_UP) {
        type = EventType::kMouseReleased;
      } else if (ev.buttons != 0) {
        type = EventType::kMouseDragged;
      }

      int flags = ConvertHaikuModifiers(ev.modifiers, ev.buttons);
      int changed_button_flags = EF_NONE;
      if (ev.type == HAIKU_OZONE_EVENT_MOUSE_DOWN ||
          ev.type == HAIKU_OZONE_EVENT_MOUSE_UP) {
        if (ev.buttons & 0x01) {
          changed_button_flags = EF_LEFT_MOUSE_BUTTON;
        } else if (ev.buttons & 0x02) {
          changed_button_flags = EF_RIGHT_MOUSE_BUTTON;
        } else if (ev.buttons & 0x04) {
          changed_button_flags = EF_MIDDLE_MOUSE_BUTTON;
        } else {
          changed_button_flags = EF_LEFT_MOUSE_BUTTON;
        }
        flags |= changed_button_flags;
      }

      gfx::PointF loc(ev.x, ev.y);
      last_mouse_location_ = loc;
      gfx::PointF root_loc(bounds_.x() + ev.x, bounds_.y() + ev.y);
      MouseEvent mouse_ev(type, loc, root_loc, now, flags,
                          changed_button_flags);
      if (ev.type == HAIKU_OZONE_EVENT_MOUSE_DOWN ||
          ev.type == HAIKU_OZONE_EVENT_MOUSE_UP) {
        mouse_ev.SetClickCount(ev.clicks > 0 ? ev.clicks : 1);
      }
      delegate_->DispatchEvent(&mouse_ev);
      break;
    }

    case HAIKU_OZONE_EVENT_MOUSE_WHEEL: {
      int flags = ConvertHaikuModifiers(ev.modifiers, ev.buttons);
      gfx::PointF loc(ev.x, ev.y);
      if (loc.x() <= 0.0f && loc.y() <= 0.0f) {
        loc = last_mouse_location_;
      } else {
        last_mouse_location_ = loc;
      }
      gfx::PointF root_loc(bounds_.x() + loc.x(), bounds_.y() + loc.y());
      int dx = static_cast<int>(ev.delta_x);
      int dy = static_cast<int>(ev.delta_y);
      if (dx != 0 && dx > -MouseWheelEvent::kWheelDelta &&
          dx < MouseWheelEvent::kWheelDelta) {
        dx = (dx > 0) ? MouseWheelEvent::kWheelDelta
                      : -MouseWheelEvent::kWheelDelta;
      }
      if (dy != 0 && dy > -MouseWheelEvent::kWheelDelta &&
          dy < MouseWheelEvent::kWheelDelta) {
        dy = (dy > 0) ? MouseWheelEvent::kWheelDelta
                      : -MouseWheelEvent::kWheelDelta;
      }
      gfx::Vector2d offset(dx, dy);
      gfx::Vector2d tick_120ths(
          (dx * 120) / MouseWheelEvent::kWheelDelta,
          (dy * 120) / MouseWheelEvent::kWheelDelta);
      MouseWheelEvent wheel_ev(offset, loc, root_loc, now, flags, EF_NONE,
                               tick_120ths);
      delegate_->DispatchEvent(&wheel_ev);
      break;
    }

    case HAIKU_OZONE_EVENT_KEY_DOWN:
    case HAIKU_OZONE_EVENT_KEY_UP: {
      EventType type = (ev.type == HAIKU_OZONE_EVENT_KEY_DOWN)
                           ? EventType::kKeyPressed
                           : EventType::kKeyReleased;
      int flags = ConvertHaikuModifiers(ev.modifiers, 0);
      KeyboardCode key_code =
          ConvertHaikuKeyToKeyboardCode(ev.raw_char, ev.key_code, ev.utf8);
      if (key_code != VKEY_UNKNOWN) {
        if (type == EventType::kKeyPressed && ev.utf8[0] >= 0x20 &&
            ev.utf8[0] != 0x7f && (flags & EF_CONTROL_DOWN) == 0) {
          std::u16string u16 = base::UTF8ToUTF16(std::string_view(ev.utf8));
          if (!u16.empty()) {
            KeyEvent key_ev(type, key_code, DomCode::NONE, flags,
                            DomKey::FromCharacter(u16[0]), now);
            delegate_->DispatchEvent(&key_ev);
            break;
          }
        }
        KeyEvent key_ev(type, key_code, flags, now);
        delegate_->DispatchEvent(&key_ev);
      }
      break;
    }

    case HAIKU_OZONE_EVENT_WINDOW_MOVED: {
      gfx::Rect new_bounds(static_cast<int>(ev.x), static_cast<int>(ev.y),
                           bounds_.width(), bounds_.height());
      UpdateBounds(new_bounds);
      break;
    }

    case HAIKU_OZONE_EVENT_WINDOW_RESIZED: {
      if (ev.width > 0 && ev.height > 0) {
        gfx::Rect new_bounds(bounds_.x(), bounds_.y(), ev.width, ev.height);
        UpdateBounds(new_bounds);
      }
      break;
    }

    case HAIKU_OZONE_EVENT_WINDOW_ACTIVATED: {
      delegate_->OnActivationChanged(ev.active != 0);
      break;
    }

    case HAIKU_OZONE_EVENT_WINDOW_CLOSE_REQUEST: {
      delegate_->OnCloseRequest();
      break;
    }

    default:
      break;
  }
}

void HaikuWindow::ZoomWindowBounds() {
  if (auto* screen = display::Screen::Get()) {
    gfx::Rect zoomed_bounds =
        screen->GetDisplayMatching(bounds_).work_area();
    SetBoundsInPixels(delegate_->ConvertRectToPixels(zoomed_bounds));
  }
}

void HaikuWindow::RestoreWindowBounds() {
  if (restored_bounds_) {
    gfx::Rect restored = *restored_bounds_;
    restored_bounds_.reset();
    SetBoundsInPixels(restored);
  }
}

void HaikuWindow::UpdateBounds(const gfx::Rect& bounds) {
  bool origin_changed = bounds_.origin() != bounds.origin();
  bounds_ = bounds;
  delegate_->OnBoundsChanged({origin_changed});
}

void HaikuWindow::UpdateWindowState(PlatformWindowState new_window_state) {
  if (window_state_ == new_window_state) {
    return;
  }
  auto old_window_state = window_state_;
  window_state_ = new_window_state;
  delegate_->OnWindowStateChanged(old_window_state, new_window_state);
}

}  // namespace ui
