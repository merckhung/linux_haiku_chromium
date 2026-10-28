/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#ifndef LINUX_FB_RENDERING_BUFFER_H
#define LINUX_FB_RENDERING_BUFFER_H

#include "RenderingBuffer.h"

class LinuxFBRenderingBuffer : public RenderingBuffer {
public:
    LinuxFBRenderingBuffer(void* bits, uint32 width, uint32 height,
                          uint32 bytesPerRow, color_space cs)
        : fBits(bits),
          fWidth(width),
          fHeight(height),
          fBytesPerRow(bytesPerRow),
          fColorSpace(cs)
    {
    }

    virtual ~LinuxFBRenderingBuffer() {}

    virtual status_t InitCheck() const {
        return fBits != nullptr ? B_OK : B_NO_INIT;
    }

    virtual color_space ColorSpace() const { return fColorSpace; }
    virtual void* Bits() const { return fBits; }
    virtual uint32 BytesPerRow() const { return fBytesPerRow; }
    virtual uint32 Width() const { return fWidth; }
    virtual uint32 Height() const { return fHeight; }

    void SetBits(void* bits) { fBits = bits; }

private:
    void* fBits;
    uint32 fWidth;
    uint32 fHeight;
    uint32 fBytesPerRow;
    color_space fColorSpace;
};

#endif // LINUX_FB_RENDERING_BUFFER_H
