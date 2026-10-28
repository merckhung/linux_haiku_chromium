/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#ifndef LINUX_FB_HW_INTERFACE_H
#define LINUX_FB_HW_INTERFACE_H

#include "HWInterface.h"
#include "LinuxFBRenderingBuffer.h"

class LinuxFBHWInterface : public HWInterface {
public:
    LinuxFBHWInterface();
    virtual ~LinuxFBHWInterface();

    virtual status_t Initialize();
    virtual status_t Shutdown();

    virtual EventStream* CreateEventStream();

    virtual status_t SetMode(const display_mode& mode);
    virtual void GetMode(display_mode* mode);

    virtual status_t GetDeviceInfo(accelerant_device_info* info);
    virtual status_t GetModeList(display_mode** _modeList, uint32* _count);
    virtual status_t GetPixelClockLimits(display_mode* mode,
                                         uint32* _low, uint32* _high);
    virtual status_t GetTimingConstraints(display_timing_constraints* constraints);
    virtual status_t ProposeMode(display_mode* candidate,
                                 const display_mode* low,
                                 const display_mode* high);

    virtual sem_id RetraceSemaphore();
    virtual status_t WaitForRetrace(bigtime_t timeout = B_INFINITE_TIMEOUT);

    virtual status_t SetDPMSMode(uint32 state);
    virtual uint32 DPMSMode();
    virtual uint32 DPMSCapabilities();

    virtual status_t SetBrightness(float brightness);
    virtual status_t GetBrightness(float* brightness);

    virtual RenderingBuffer* FrontBuffer() const;
    virtual RenderingBuffer* BackBuffer() const;
    virtual bool IsDoubleBuffered() const;

protected:
    virtual void _CopyBackToFront(BRegion& region);

private:
    int fFB_fd;
    void* fFB_mmap;
    size_t fFB_size;

    void* fBackBufferData;
    size_t fBackBufferSize;

    uint32 fWidth;
    uint32 fHeight;
    uint32 fBytesPerRow;
    color_space fColorSpace;

    LinuxFBRenderingBuffer* fFrontBuffer;
    LinuxFBRenderingBuffer* fBackBuffer;
};

#endif // LINUX_FB_HW_INTERFACE_H
