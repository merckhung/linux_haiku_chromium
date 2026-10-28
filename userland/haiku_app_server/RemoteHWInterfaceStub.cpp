/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "userland/haiku_compat.h"
#include "RemoteHWInterface.h"

RemoteHWInterface::RemoteHWInterface(const char* target)
    : HWInterface(),
      fTarget(target),
      fInitStatus(B_NOT_SUPPORTED),
      fIsConnected(false),
      fProtocolVersion(0),
      fConnectionSpeed(0),
      fListenPort(0),
      fEventThread(-1),
      fCallbackLocker("remote callback locker")
{
}

RemoteHWInterface::~RemoteHWInterface()
{
}

status_t RemoteHWInterface::Initialize() { return B_NOT_SUPPORTED; }
status_t RemoteHWInterface::Shutdown() { return B_OK; }
DrawingEngine* RemoteHWInterface::CreateDrawingEngine() { return nullptr; }
EventStream* RemoteHWInterface::CreateEventStream() { return nullptr; }
status_t RemoteHWInterface::SetMode(const display_mode& mode) { return B_NOT_SUPPORTED; }
void RemoteHWInterface::GetMode(display_mode* mode) {}
status_t RemoteHWInterface::GetPreferredMode(display_mode* mode) { return B_NOT_SUPPORTED; }
status_t RemoteHWInterface::GetDeviceInfo(accelerant_device_info* info) { return B_NOT_SUPPORTED; }
status_t RemoteHWInterface::GetModeList(display_mode** _modeList, uint32* _count) { return B_NOT_SUPPORTED; }
status_t RemoteHWInterface::GetPixelClockLimits(display_mode* mode, uint32* low, uint32* high) { return B_NOT_SUPPORTED; }
status_t RemoteHWInterface::GetTimingConstraints(display_timing_constraints* constraints) { return B_NOT_SUPPORTED; }
status_t RemoteHWInterface::ProposeMode(display_mode* candidate, const display_mode* low, const display_mode* high) { return B_NOT_SUPPORTED; }
sem_id RemoteHWInterface::RetraceSemaphore() { return B_ERROR; }
status_t RemoteHWInterface::WaitForRetrace(bigtime_t timeout) { return B_NOT_SUPPORTED; }
status_t RemoteHWInterface::SetDPMSMode(uint32 state) { return B_NOT_SUPPORTED; }
uint32 RemoteHWInterface::DPMSMode() { return 0; }
uint32 RemoteHWInterface::DPMSCapabilities() { return 0; }
status_t RemoteHWInterface::SetBrightness(float) { return B_NOT_SUPPORTED; }
status_t RemoteHWInterface::GetBrightness(float*) { return B_NOT_SUPPORTED; }
void RemoteHWInterface::SetCursor(ServerCursor* cursor) {}
void RemoteHWInterface::SetCursorVisible(bool visible) {}
void RemoteHWInterface::MoveCursorTo(float x, float y) {}
void RemoteHWInterface::SetDragBitmap(const ServerBitmap* bitmap, const BPoint& offsetFormCursor) {}
RenderingBuffer* RemoteHWInterface::FrontBuffer() const { return nullptr; }
RenderingBuffer* RemoteHWInterface::BackBuffer() const { return nullptr; }
bool RemoteHWInterface::IsDoubleBuffered() const { return false; }
status_t RemoteHWInterface::InvalidateRegion(const BRegion& region) { return B_OK; }
status_t RemoteHWInterface::Invalidate(const BRect& frame) { return B_OK; }
status_t RemoteHWInterface::CopyBackToFront(const BRect& frame) { return B_OK; }
status_t RemoteHWInterface::AddCallback(uint32 token, CallbackFunction callback, void* cookie) { return B_NOT_SUPPORTED; }
bool RemoteHWInterface::RemoveCallback(uint32 token) { return false; }
