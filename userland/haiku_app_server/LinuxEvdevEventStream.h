/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#ifndef LINUX_EVDEV_EVENT_STREAM_H
#define LINUX_EVDEV_EVENT_STREAM_H

#include "EventStream.h"
#include <MessageQueue.h>
#include <Locker.h>

class LinuxEvdevEventStream : public EventStream {
public:
    LinuxEvdevEventStream(uint32 width = 1280, uint32 height = 800);
    virtual ~LinuxEvdevEventStream();

    virtual bool IsValid();
    virtual void SendQuit();

    virtual void UpdateScreenBounds(BRect bounds);

    virtual bool GetNextEvent(BMessage** _event);
    virtual status_t GetNextCursorPosition(BPoint& where,
                                          bigtime_t timeout = B_INFINITE_TIMEOUT);

    virtual status_t InsertEvent(BMessage* event);
    virtual BMessage* PeekLatestMouseMoved();

private:
    void _ScanDevices();
    void _PollEvents(int timeoutMs);
    void _FlushMouseMoved();

    bool fQuitting;
    BRect fScreenBounds;
    BPoint fCursorPos;
    uint32 fButtons;
    uint32 fModifiers;

    bigtime_t fLastClickTime;
    uint32 fLastClickButton;
    int32 fClickCount;
    bigtime_t fLastScanTime;
    bigtime_t fLastKeyPressTime;
    bool fKeyDownState[256];

    int fEpollFd;
    int fWakeFd;
    int fDeviceFds[16];
    int fAbsMinX[16];
    int fAbsMaxX[16];
    int fAbsMinY[16];
    int fAbsMaxY[16];
    int fDeviceCount;
    uint32 fOpenedMask;

    BLocker fLock;
    BMessageQueue fEventQueue;
    BMessage* fLatestMouseMoved;
};

#endif // LINUX_EVDEV_EVENT_STREAM_H
