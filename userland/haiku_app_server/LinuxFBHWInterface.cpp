/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "LinuxFBHWInterface.h"
#include "LinuxEvdevEventStream.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

LinuxFBHWInterface::LinuxFBHWInterface()
    : HWInterface(),
      fFB_fd(-1),
      fFB_mmap(MAP_FAILED),
      fFB_size(0),
      fBackBufferData(nullptr),
      fBackBufferSize(0),
      fWidth(1024),
      fHeight(768),
      fBytesPerRow(1024 * 4),
      fColorSpace(B_RGB32),
      fFrontBuffer(nullptr),
      fBackBuffer(nullptr)
{
}

LinuxFBHWInterface::~LinuxFBHWInterface()
{
    Shutdown();
}

status_t LinuxFBHWInterface::Initialize()
{
    // Disable Linux fbcon blinking text cursor so it doesn't overwrite /dev/fb0
    int blink_fd = open("/sys/class/graphics/fbcon/cursor_blink", O_WRONLY);
    if (blink_fd >= 0) {
        write(blink_fd, "0\n", 2);
        close(blink_fd);
    }
    const char* hide_seq = "\033[?25l\033[?1c";
    const char* ttys[] = {"/dev/tty0", "/dev/tty1", "/dev/console"};
    for (size_t i = 0; i < 3; i++) {
        int tty_fd = open(ttys[i], O_RDWR | O_NOCTTY);
        if (tty_fd >= 0) {
            write(tty_fd, hide_seq, strlen(hide_seq));
            ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
            close(tty_fd);
        }
    }

    fFB_fd = open("/dev/fb0", O_RDWR);
    if (fFB_fd >= 0) {
        struct fb_var_screeninfo var_info;
        struct fb_fix_screeninfo fix_info;

        if (ioctl(fFB_fd, FBIOGET_VSCREENINFO, &var_info) == 0 &&
            ioctl(fFB_fd, FBIOGET_FSCREENINFO, &fix_info) == 0) {
            fWidth = var_info.xres;
            fHeight = var_info.yres;
            fBytesPerRow = fix_info.line_length;
            fFB_size = fix_info.smem_len;

            printf("[app_server/LinuxFB] Detected /dev/fb0: %dx%d, stride %u, smem %zu bytes\n",
                   fWidth, fHeight, fBytesPerRow, fFB_size);
        }

        if (fWidth == 0 || fHeight == 0) {
            fWidth = 1024;
            fHeight = 768;
            fBytesPerRow = fWidth * 4;
            fFB_size = fHeight * fBytesPerRow;
        }

        fFB_mmap = mmap(NULL, fFB_size, PROT_READ | PROT_WRITE, MAP_SHARED, fFB_fd, 0);
        if (fFB_mmap == MAP_FAILED) {
            perror("[app_server/LinuxFB] mmap /dev/fb0 failed");
        }
    }

    if (fFB_mmap == MAP_FAILED) {
        printf("[app_server/LinuxFB] Using virtual offscreen framebuffer (1024x768x32)\n");
        fWidth = 1024;
        fHeight = 768;
        fBytesPerRow = fWidth * 4;
        fFB_size = fHeight * fBytesPerRow;
        fFB_mmap = mmap(NULL, fFB_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }

    // Allocate back buffer for double-buffering
    fBackBufferSize = fHeight * fBytesPerRow;
    fBackBufferData = malloc(fBackBufferSize);
    if (fBackBufferData) {
        memset(fBackBufferData, 0x33, fBackBufferSize); // Dark Haiku desktop color
    }
    if (fFB_mmap != MAP_FAILED && fFB_mmap != nullptr) {
        memset(fFB_mmap, 0x33, fFB_size);
    }

    fFrontBuffer = new LinuxFBRenderingBuffer(fFB_mmap, fWidth, fHeight, fBytesPerRow, fColorSpace);
    fBackBuffer = new LinuxFBRenderingBuffer(fBackBufferData ? fBackBufferData : fFB_mmap,
                                           fWidth, fHeight, fBytesPerRow, fColorSpace);

    _NotifyFrameBufferChanged();

    return B_OK;
}

status_t LinuxFBHWInterface::Shutdown()
{
    if (fFrontBuffer) {
        delete fFrontBuffer;
        fFrontBuffer = nullptr;
    }
    if (fBackBuffer) {
        delete fBackBuffer;
        fBackBuffer = nullptr;
    }
    if (fBackBufferData) {
        free(fBackBufferData);
        fBackBufferData = nullptr;
    }
    if (fFB_mmap != MAP_FAILED && fFB_mmap != nullptr) {
        munmap(fFB_mmap, fFB_size);
        fFB_mmap = MAP_FAILED;
    }
    if (fFB_fd >= 0) {
        close(fFB_fd);
        fFB_fd = -1;
    }
    return B_OK;
}

EventStream* LinuxFBHWInterface::CreateEventStream()
{
    return new LinuxEvdevEventStream(fWidth, fHeight);
}

void LinuxFBHWInterface::_CopyBackToFront(BRegion& region)
{
    if (!fBackBufferData || fFB_mmap == MAP_FAILED)
        return;

    int32 count = region.CountRects();
    for (int32 i = 0; i < count; i++) {
        clipping_rect r = region.RectAtInt(i);
        if (r.left < 0) r.left = 0;
        if (r.top < 0) r.top = 0;
        if (r.right >= (int32)fWidth) r.right = fWidth - 1;
        if (r.bottom >= (int32)fHeight) r.bottom = fHeight - 1;
        if (r.left > r.right || r.top > r.bottom)
            continue;

        size_t rowBytes = (r.right - r.left + 1) * 4;
        uint8* src = (uint8*)fBackBufferData + (r.top * fBytesPerRow) + (r.left * 4);
        uint8* dst = (uint8*)fFB_mmap + (r.top * fBytesPerRow) + (r.left * 4);

        for (int32 y = r.top; y <= r.bottom; y++) {
            memcpy(dst, src, rowBytes);
            src += fBytesPerRow;
            dst += fBytesPerRow;
        }
    }
}

RenderingBuffer* LinuxFBHWInterface::FrontBuffer() const { return fFrontBuffer; }
RenderingBuffer* LinuxFBHWInterface::BackBuffer() const { return fBackBuffer; }
bool LinuxFBHWInterface::IsDoubleBuffered() const { return fBackBufferData != nullptr; }

status_t LinuxFBHWInterface::SetMode(const display_mode& mode) { return B_OK; }
void LinuxFBHWInterface::GetMode(display_mode* mode)
{
    if (!mode) return;
    memset(mode, 0, sizeof(*mode));
    mode->virtual_width = fWidth;
    mode->virtual_height = fHeight;
    mode->space = fColorSpace;
}

status_t LinuxFBHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
    if (!info) return B_BAD_VALUE;
    memset(info, 0, sizeof(*info));
    info->version = 1;
    strcpy(info->name, "Linux Framebuffer Device");
    strcpy(info->chipset, "DRM Bochs / fbdev");
    return B_OK;
}

status_t LinuxFBHWInterface::GetModeList(display_mode** _modeList, uint32* _count)
{
    if (!_modeList || !_count) return B_BAD_VALUE;
    display_mode* list = new display_mode[1];
    GetMode(&list[0]);
    *_modeList = list;
    *_count = 1;
    return B_OK;
}

status_t LinuxFBHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low, uint32* _high)
{
    if (_low) *_low = 0;
    if (_high) *_high = 100000000;
    return B_OK;
}

status_t LinuxFBHWInterface::GetTimingConstraints(display_timing_constraints* constraints)
{
    return B_ERROR;
}

status_t LinuxFBHWInterface::ProposeMode(display_mode* candidate, const display_mode* low, const display_mode* high)
{
    return B_OK;
}

sem_id LinuxFBHWInterface::RetraceSemaphore() { return -1; }
status_t LinuxFBHWInterface::WaitForRetrace(bigtime_t timeout) { return snooze(16666); }

status_t LinuxFBHWInterface::SetDPMSMode(uint32 state) { return B_OK; }
uint32 LinuxFBHWInterface::DPMSMode() { return B_DPMS_ON; }
uint32 LinuxFBHWInterface::DPMSCapabilities() { return B_DPMS_ON; }

status_t LinuxFBHWInterface::SetBrightness(float brightness) { return B_OK; }
status_t LinuxFBHWInterface::GetBrightness(float* brightness)
{
    if (brightness) *brightness = 1.0f;
    return B_OK;
}
