/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * SPDX-License-Identifier: MIT
 */

#include "userland/haiku_libbe/haiku_ozone_capi.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <mutex>
#include <queue>
#include <string>

#include <Application.h>
#include <Bitmap.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Point.h>
#include <Rect.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

namespace {

static pthread_mutex_t gAppInitMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gAppInitCond = PTHREAD_COND_INITIALIZER;
static bool gAppInitDone = false;
static status_t gAppInitStatus = B_NO_INIT;

struct AppInitArgs {
    std::string signature;
};

static void* HaikuAppLooperThread(void* arg)
{
    AppInitArgs* args = static_cast<AppInitArgs*>(arg);
    status_t err = B_OK;
    BApplication* app = new BApplication(args->signature.c_str(), &err);
    delete args;

    pthread_mutex_lock(&gAppInitMutex);
    gAppInitStatus = err;
    gAppInitDone = true;
    pthread_cond_broadcast(&gAppInitCond);
    pthread_mutex_unlock(&gAppInitMutex);

    if (err == B_OK && app != nullptr) {
        app->Run();
    }
    return nullptr;
}

class HaikuOzoneWindowContext;

class ChromiumHaikuView : public BView {
public:
    ChromiumHaikuView(BRect frame, HaikuOzoneWindowContext* ctx);
    ~ChromiumHaikuView() override = default;

    void AttachedToWindow() override;
    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseUp(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage) override;
    void KeyDown(const char* bytes, int32 numBytes) override;
    void KeyUp(const char* bytes, int32 numBytes) override;
    void MessageReceived(BMessage* message) override;

private:
    HaikuOzoneWindowContext* fCtx;
};

class ChromiumHaikuWindow : public BWindow {
public:
    ChromiumHaikuWindow(BRect frame, const char* title, HaikuOzoneWindowContext* ctx);
    ~ChromiumHaikuWindow() override = default;

    void FrameMoved(BPoint newPosition) override;
    void FrameResized(float newWidth, float newHeight) override;
    void WindowActivated(bool active) override;
    bool QuitRequested() override;

    void SetForceQuit(bool force) { fForceQuit = force; }

private:
    HaikuOzoneWindowContext* fCtx;
    bool fForceQuit = false;
};

class HaikuOzoneWindowContext {
public:
    HaikuOzoneWindowContext(int x, int y, int width, int height, const char* title)
        : fX((x <= 60 && y <= 60) ? 340 : x),
          fY((x <= 60 && y <= 60) ? 95 : y),
          fWidth(width > 0 ? width : 860),
          fHeight(height > 0 ? height : 580),
          fVisible(false),
          fLastButtons(0),
          fLastMouseX(static_cast<float>(width > 0 ? width / 2 : 430)),
          fLastMouseY(static_cast<float>(height > 0 ? height / 2 : 290)),
          fEventFd(-1),
          fWindow(nullptr),
          fView(nullptr),
          fBitmap(nullptr)
    {
        fEventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        BRect frame(fX, fY, fX + fWidth - 1, fY + fHeight - 1);
        fWindow = new ChromiumHaikuWindow(
            frame, (title && title[0]) ? title : "Chromium", this);
        fView = new ChromiumHaikuView(fWindow->Bounds(), this);
        fWindow->AddChild(fView);
        fView->MakeFocus(true);
        EnsureBitmapLocked(fWidth, fHeight);
    }

    ~HaikuOzoneWindowContext()
    {
        if (fWindow) {
            fWindow->SetForceQuit(true);
            if (fWindow->Lock()) {
                fWindow->Quit();
            }
            fWindow = nullptr;
            fView = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(fBitmapMutex);
            delete fBitmap;
            fBitmap = nullptr;
        }
        if (fEventFd >= 0) {
            close(fEventFd);
            fEventFd = -1;
        }
    }

    int GetEventFd() const { return fEventFd; }

    void PostEvent(const HaikuOzoneEvent& ev)
    {
        {
            std::lock_guard<std::mutex> lock(fEventMutex);
            fEvents.push(ev);
        }
        if (fEventFd >= 0) {
            uint64_t val = 1;
            ssize_t r = write(fEventFd, &val, sizeof(val));
            (void)r;
        }
    }

    bool PollEvent(HaikuOzoneEvent* out_event)
    {
        std::lock_guard<std::mutex> lock(fEventMutex);
        if (fEvents.empty()) {
            uint64_t val = 0;
            if (fEventFd >= 0) {
                ssize_t r = read(fEventFd, &val, sizeof(val));
                (void)r;
            }
            return false;
        }
        *out_event = fEvents.front();
        fEvents.pop();
        if (fEvents.empty() && fEventFd >= 0) {
            uint64_t val = 0;
            ssize_t r = read(fEventFd, &val, sizeof(val));
            (void)r;
        }
        return true;
    }

    void EnsureBitmapLocked(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return;
        if (fBitmap != nullptr) {
            if (fBitmap->Bounds().IntegerWidth() + 1 == width &&
                fBitmap->Bounds().IntegerHeight() + 1 == height) {
                return;
            }
            delete fBitmap;
            fBitmap = nullptr;
        }
        fBitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
        if (fBitmap && fBitmap->IsValid() && fBitmap->Bits()) {
            memset(fBitmap->Bits(), 0xFF, fBitmap->BitsLength());
        }
    }

    void DrawView(BRect updateRect)
    {
        std::lock_guard<std::mutex> lock(fBitmapMutex);
        if (fBitmap && fBitmap->IsValid() && fView) {
            fView->DrawBitmap(fBitmap, updateRect, updateRect);
        }
    }

    int PresentPixels(const uint8_t* bgra_pixels, int src_row_bytes,
                      int width, int height,
                      int damage_x, int damage_y, int damage_w, int damage_h)
    {
        if (!bgra_pixels || width <= 0 || height <= 0 || !fWindow)
            return -1;

        if (src_row_bytes <= 0)
            src_row_bytes = width * 4;

        {
            std::lock_guard<std::mutex> lock(fBitmapMutex);
            EnsureBitmapLocked(width, height);
            if (!fBitmap || !fBitmap->IsValid() || !fBitmap->Bits())
                return -1;

            uint8_t* dst_base = static_cast<uint8_t*>(fBitmap->Bits());
            int dst_bpr = fBitmap->BytesPerRow();
            int copy_x = damage_x > 0 ? damage_x : 0;
            int copy_y = damage_y > 0 ? damage_y : 0;
            int copy_w = (damage_w > 0 && copy_x + damage_w <= width)
                             ? damage_w
                             : (width - copy_x);
            int copy_h = (damage_h > 0 && copy_y + damage_h <= height)
                             ? damage_h
                             : (height - copy_y);

            if (copy_w <= 0 || copy_h <= 0) {
                copy_x = 0;
                copy_y = 0;
                copy_w = width;
                copy_h = height;
            }

            for (int row = copy_y; row < copy_y + copy_h; ++row) {
                const uint8_t* src = bgra_pixels + row * src_row_bytes + copy_x * 4;
                uint8_t* dst = dst_base + row * dst_bpr + copy_x * 4;
                memcpy(dst, src, copy_w * 4);
            }
        }

        if (fWindow->LockWithTimeout(100000) == B_OK) {
            if (fView && fBitmap && fBitmap->IsValid()) {
                BRect damageRect(0, 0, width - 1, height - 1);
                if (damage_w > 0 && damage_h > 0) {
                    damageRect.Set(damage_x, damage_y,
                                   damage_x + damage_w - 1,
                                   damage_y + damage_h - 1);
                }
                {
                    std::lock_guard<std::mutex> lock(fBitmapMutex);
                    fView->DrawBitmap(fBitmap, damageRect, damageRect);
                }
                fView->Flush();
            }
            fWindow->Unlock();
        }
        return 0;
    }

    int fX;
    int fY;
    int fWidth;
    int fHeight;
    bool fVisible;
    uint32_t fLastButtons;
    float fLastMouseX;
    float fLastMouseY;
    int fEventFd;
    ChromiumHaikuWindow* fWindow;
    ChromiumHaikuView* fView;
    BBitmap* fBitmap;
    std::mutex fBitmapMutex;
    std::mutex fEventMutex;
    std::queue<HaikuOzoneEvent> fEvents;
};

ChromiumHaikuView::ChromiumHaikuView(BRect frame, HaikuOzoneWindowContext* ctx)
    : BView(frame, "ChromiumOzoneView", B_FOLLOW_ALL_SIDES,
            B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE),
      fCtx(ctx)
{
}

void ChromiumHaikuView::AttachedToWindow()
{
    BView::AttachedToWindow();
    SetViewColor(B_TRANSPARENT_COLOR);
}

void ChromiumHaikuView::Draw(BRect updateRect)
{
    if (fCtx) {
        fCtx->DrawView(updateRect);
    }
}

void ChromiumHaikuView::MouseDown(BPoint where)
{
    MakeFocus(true);
    SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);

    uint32 buttons = B_PRIMARY_MOUSE_BUTTON;
    uint32 mods = modifiers();
    int32 clicks = 1;
    if (Looper() && Looper()->CurrentMessage()) {
        Looper()->CurrentMessage()->FindInt32("buttons", reinterpret_cast<int32*>(&buttons));
        Looper()->CurrentMessage()->FindInt32("modifiers", reinterpret_cast<int32*>(&mods));
        Looper()->CurrentMessage()->FindInt32("clicks", &clicks);
    }
    if (buttons == 0)
        buttons = B_PRIMARY_MOUSE_BUTTON;

    if (fCtx) {
        fCtx->fLastButtons = buttons;
        fCtx->fLastMouseX = where.x;
        fCtx->fLastMouseY = where.y;
        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_MOUSE_DOWN;
        ev.x = where.x;
        ev.y = where.y;
        ev.buttons = buttons;
        ev.modifiers = mods;
        ev.clicks = clicks > 0 ? clicks : 1;
        fCtx->PostEvent(ev);
    }
}

void ChromiumHaikuView::MouseUp(BPoint where)
{
    uint32 buttons = 0;
    uint32 mods = modifiers();
    if (Looper() && Looper()->CurrentMessage()) {
        Looper()->CurrentMessage()->FindInt32("buttons", reinterpret_cast<int32*>(&buttons));
        Looper()->CurrentMessage()->FindInt32("modifiers", reinterpret_cast<int32*>(&mods));
    }

    if (fCtx) {
        uint32 released = fCtx->fLastButtons & ~buttons;
        if (released == 0)
            released = B_PRIMARY_MOUSE_BUTTON;
        fCtx->fLastButtons = buttons;
        fCtx->fLastMouseX = where.x;
        fCtx->fLastMouseY = where.y;

        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_MOUSE_UP;
        ev.x = where.x;
        ev.y = where.y;
        ev.buttons = released;
        ev.modifiers = mods;
        ev.clicks = 1;
        fCtx->PostEvent(ev);
    }
}

void ChromiumHaikuView::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
    (void)transit;
    (void)dragMessage;
    uint32 buttons = 0;
    uint32 mods = modifiers();
    if (Looper() && Looper()->CurrentMessage()) {
        Looper()->CurrentMessage()->FindInt32("buttons", reinterpret_cast<int32*>(&buttons));
        Looper()->CurrentMessage()->FindInt32("modifiers", reinterpret_cast<int32*>(&mods));
    }

    if (fCtx) {
        fCtx->fLastButtons = buttons;
        fCtx->fLastMouseX = where.x;
        fCtx->fLastMouseY = where.y;
        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_MOUSE_MOVE;
        ev.x = where.x;
        ev.y = where.y;
        ev.buttons = buttons;
        ev.modifiers = mods;
        fCtx->PostEvent(ev);
    }
}

void ChromiumHaikuView::KeyDown(const char* bytes, int32 numBytes)
{
    int32 key = 0;
    int32 raw_char = 0;
    uint32 mods = modifiers();
    if (Looper() && Looper()->CurrentMessage()) {
        Looper()->CurrentMessage()->FindInt32("key", &key);
        Looper()->CurrentMessage()->FindInt32("raw_char", &raw_char);
        Looper()->CurrentMessage()->FindInt32("modifiers", reinterpret_cast<int32*>(&mods));
    }

    if (fCtx) {
        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_KEY_DOWN;
        ev.key_code = key;
        ev.raw_char = raw_char;
        ev.modifiers = mods;
        if (bytes && numBytes > 0) {
            size_t len = static_cast<size_t>(numBytes) < sizeof(ev.utf8) - 1
                             ? static_cast<size_t>(numBytes)
                             : sizeof(ev.utf8) - 1;
            memcpy(ev.utf8, bytes, len);
            ev.utf8[len] = '\0';
        }
        fCtx->PostEvent(ev);
    }
}

void ChromiumHaikuView::KeyUp(const char* bytes, int32 numBytes)
{
    int32 key = 0;
    int32 raw_char = 0;
    uint32 mods = modifiers();
    if (Looper() && Looper()->CurrentMessage()) {
        Looper()->CurrentMessage()->FindInt32("key", &key);
        Looper()->CurrentMessage()->FindInt32("raw_char", &raw_char);
        Looper()->CurrentMessage()->FindInt32("modifiers", reinterpret_cast<int32*>(&mods));
    }

    if (fCtx) {
        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_KEY_UP;
        ev.key_code = key;
        ev.raw_char = raw_char;
        ev.modifiers = mods;
        if (bytes && numBytes > 0) {
            size_t len = static_cast<size_t>(numBytes) < sizeof(ev.utf8) - 1
                             ? static_cast<size_t>(numBytes)
                             : sizeof(ev.utf8) - 1;
            memcpy(ev.utf8, bytes, len);
            ev.utf8[len] = '\0';
        }
        fCtx->PostEvent(ev);
    }
}

void ChromiumHaikuView::MessageReceived(BMessage* message)
{
    if (message && message->what == B_MOUSE_WHEEL_CHANGED && fCtx) {
        float dx = 0.0f;
        float dy = 0.0f;
        message->FindFloat("be:wheel_delta_x", &dx);
        message->FindFloat("be:wheel_delta_y", &dy);
        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_MOUSE_WHEEL;
        ev.x = fCtx->fLastMouseX;
        ev.y = fCtx->fLastMouseY;
        ev.buttons = fCtx->fLastButtons;
        // Chromium MouseWheelEvent::kWheelDelta is 120; each discrete wheel
        // notch (dx/dy == +/-1.0) must be at least 120 units so integer
        // division by kWheelDelta in MouseWheelEvent does not truncate to 0.
        ev.delta_x = -dx * 120.0f;
        ev.delta_y = -dy * 120.0f;
        ev.modifiers = modifiers();
        fCtx->PostEvent(ev);
        return;
    }
    BView::MessageReceived(message);
}

ChromiumHaikuWindow::ChromiumHaikuWindow(BRect frame, const char* title, HaikuOzoneWindowContext* ctx)
    : BWindow(frame, title, B_DOCUMENT_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_ASYNCHRONOUS_CONTROLS),
      fCtx(ctx)
{
}

void ChromiumHaikuWindow::FrameMoved(BPoint newPosition)
{
    BWindow::FrameMoved(newPosition);
    if (fCtx) {
        fCtx->fX = static_cast<int>(newPosition.x);
        fCtx->fY = static_cast<int>(newPosition.y);
        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_WINDOW_MOVED;
        ev.x = newPosition.x;
        ev.y = newPosition.y;
        ev.width = fCtx->fWidth;
        ev.height = fCtx->fHeight;
        fCtx->PostEvent(ev);
    }
}

void ChromiumHaikuWindow::FrameResized(float newWidth, float newHeight)
{
    BWindow::FrameResized(newWidth, newHeight);
    if (fCtx) {
        int w = static_cast<int>(newWidth) + 1;
        int h = static_cast<int>(newHeight) + 1;
        fCtx->fWidth = w;
        fCtx->fHeight = h;
        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_WINDOW_RESIZED;
        ev.x = static_cast<float>(fCtx->fX);
        ev.y = static_cast<float>(fCtx->fY);
        ev.width = w;
        ev.height = h;
        fCtx->PostEvent(ev);
    }
}

void ChromiumHaikuWindow::WindowActivated(bool active)
{
    BWindow::WindowActivated(active);
    if (fCtx) {
        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_WINDOW_ACTIVATED;
        ev.active = active ? 1 : 0;
        fCtx->PostEvent(ev);
    }
}

bool ChromiumHaikuWindow::QuitRequested()
{
    if (fForceQuit)
        return true;
    if (fCtx) {
        HaikuOzoneEvent ev = {};
        ev.type = HAIKU_OZONE_EVENT_WINDOW_CLOSE_REQUEST;
        fCtx->PostEvent(ev);
    }
    return false;
}

}  // namespace

extern "C" {

int haiku_ozone_init_app(const char* signature)
{
    if (be_app != nullptr)
        return 0;

    pthread_mutex_lock(&gAppInitMutex);
    if (gAppInitDone) {
        status_t st = gAppInitStatus;
        pthread_mutex_unlock(&gAppInitMutex);
        return st == B_OK ? 0 : -1;
    }

    AppInitArgs* args = new AppInitArgs();
    args->signature = (signature && signature[0])
                          ? signature
                          : "application/x-vnd.chromium-browser";

    pthread_t tid;
    int rc = pthread_create(&tid, nullptr, HaikuAppLooperThread, args);
    if (rc != 0) {
        delete args;
        pthread_mutex_unlock(&gAppInitMutex);
        return -1;
    }
    pthread_detach(tid);

    while (!gAppInitDone) {
        pthread_cond_wait(&gAppInitCond, &gAppInitMutex);
    }
    status_t st = gAppInitStatus;
    pthread_mutex_unlock(&gAppInitMutex);
    if (st == B_OK) {
        printf("[Chromium/OzoneHaiku] Initialized BApplication %s\n",
               (signature && signature[0]) ? signature : "application/x-vnd.chromium-browser");
        fflush(stdout);
    }
    return st == B_OK ? 0 : -1;
}

void* haiku_ozone_create_window(int x, int y, int width, int height,
                                const char* title, int* out_event_fd)
{
    haiku_ozone_init_app("application/x-vnd.chromium-browser");
    HaikuOzoneWindowContext* ctx =
        new HaikuOzoneWindowContext(x, y, width, height, title);
    if (out_event_fd)
        *out_event_fd = ctx->GetEventFd();
    printf("[Chromium/OzoneHaiku] Window created: %dx%d at (%d, %d)\n",
           ctx->fWidth, ctx->fHeight, ctx->fX, ctx->fY);
    fflush(stdout);
    return ctx;
}

int haiku_ozone_set_window_type(void* win_handle, int type)
{
    if (!win_handle || (type != 0 && type != 1))
        return -1;
    if (type == 0)
        return 0;
    auto* ctx = static_cast<HaikuOzoneWindowContext*>(win_handle);
    if (!ctx->fWindow || ctx->fWindow->LockWithTimeout(100000) != B_OK)
        return -1;
    status_t status = ctx->fWindow->SetLook(B_NO_BORDER_WINDOW_LOOK);
    if (status == B_OK)
        status = ctx->fWindow->SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
    if (status == B_OK) {
        status = ctx->fWindow->SetFlags(ctx->fWindow->Flags()
            | B_AVOID_FOCUS | B_AVOID_FRONT | B_NOT_MOVABLE | B_NOT_RESIZABLE
            | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE);
    }
    ctx->fWindow->Unlock();
    if (status == B_OK) {
        printf("[Chromium/OzoneHaiku] Tooltip configured without focus\n");
        fflush(stdout);
    }
    return status == B_OK ? 0 : -1;
}

void haiku_ozone_destroy_window(void* win_handle)
{
    if (!win_handle)
        return;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    delete ctx;
}

void haiku_ozone_show_window(void* win_handle)
{
    if (!win_handle)
        return;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    if (ctx->fWindow && !ctx->fVisible) {
        ctx->fWindow->Show();
        ctx->fVisible = true;
    }
}

void haiku_ozone_hide_window(void* win_handle)
{
    if (!win_handle)
        return;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    if (ctx->fWindow && ctx->fVisible) {
        if (ctx->fWindow->LockWithTimeout(100000) == B_OK) {
            ctx->fWindow->Hide();
            ctx->fVisible = false;
            ctx->fWindow->Unlock();
        }
    }
}

void haiku_ozone_set_window_bounds(void* win_handle, int x, int y, int width, int height)
{
    if (!win_handle)
        return;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    if (x <= 60 && y <= 60) {
        x = 340;
        y = 95;
    } else if (y < 28) {
        y = 28;
    }
    ctx->fX = x;
    ctx->fY = y;
    ctx->fWidth = width;
    ctx->fHeight = height;
    if (ctx->fWindow && ctx->fWindow->LockWithTimeout(100000) == B_OK) {
        ctx->fWindow->MoveTo(x, y);
        ctx->fWindow->ResizeTo(width - 1, height - 1);
        ctx->fWindow->Unlock();
    }
}

void haiku_ozone_get_window_bounds(void* win_handle, int* x, int* y, int* width, int* height)
{
    if (!win_handle)
        return;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    if (x) *x = ctx->fX;
    if (y) *y = ctx->fY;
    if (width) *width = ctx->fWidth;
    if (height) *height = ctx->fHeight;
}

void haiku_ozone_set_window_title(void* win_handle, const char* title)
{
    if (!win_handle || !title)
        return;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    // Logged to the serial console so automated tests can detect page loads
    // (Chromium sets the window title to "<page title> - Chromium").
    printf("[Chromium/OzoneHaiku] Window title: %s\n", title);
    if (ctx->fWindow && ctx->fWindow->LockWithTimeout(100000) == B_OK) {
        ctx->fWindow->SetTitle(title);
        ctx->fWindow->Unlock();
    }
}

void haiku_ozone_activate_window(void* win_handle)
{
    if (!win_handle)
        return;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    if (ctx->fWindow) {
        ctx->fWindow->Activate(true);
    }
}

void haiku_ozone_minimize_window(void* win_handle, int minimize)
{
    if (!win_handle)
        return;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    if (ctx->fWindow) {
        ctx->fWindow->Minimize(minimize != 0);
    }
}

int haiku_ozone_poll_event(void* win_handle, HaikuOzoneEvent* out_event)
{
    if (!win_handle || !out_event)
        return 0;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    return ctx->PollEvent(out_event) ? 1 : 0;
}

int haiku_ozone_present_pixels(void* win_handle,
                               const uint8_t* bgra_pixels,
                               int src_row_bytes,
                               int width,
                               int height,
                               int damage_x,
                               int damage_y,
                               int damage_w,
                               int damage_h)
{
    if (!win_handle)
        return -1;
    HaikuOzoneWindowContext* ctx =
        static_cast<HaikuOzoneWindowContext*>(win_handle);
    int rc = ctx->PresentPixels(bgra_pixels, src_row_bytes, width, height,
                                damage_x, damage_y, damage_w, damage_h);
    if (rc == 0) {
        static int sPresentCount = 0;
        ++sPresentCount;
        if (sPresentCount == 1 || sPresentCount == 5) {
            printf("[Chromium/OzoneHaiku] Surface presented successfully (frame %d, %dx%d).\n",
                   sPresentCount, width, height);
            fflush(stdout);
        }
    }
    return rc;
}

int haiku_ozone_get_screen_size(int* out_width, int* out_height)
{
    haiku_ozone_init_app("application/x-vnd.chromium-browser");
    BScreen screen(B_MAIN_SCREEN_ID);
    if (!screen.IsValid()) {
        if (out_width) *out_width = 1280;
        if (out_height) *out_height = 800;
        return -1;
    }
    BRect frame = screen.Frame();
    if (out_width) *out_width = frame.IntegerWidth() + 1;
    if (out_height) *out_height = frame.IntegerHeight() + 1;
    return 0;
}

int haiku_ozone_get_ui_color(int which, uint32_t* out_argb)
{
    if (!out_argb)
        return -1;
    rgb_color c = ui_color(static_cast<color_which>(which));
    *out_argb = (static_cast<uint32_t>(c.alpha) << 24) |
                (static_cast<uint32_t>(c.red) << 16) |
                (static_cast<uint32_t>(c.green) << 8) |
                static_cast<uint32_t>(c.blue);
    return 0;
}

}  // extern "C"
