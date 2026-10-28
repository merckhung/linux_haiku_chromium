/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

#include "LinuxEvdevEventStream.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <AppDefs.h>
#include <Autolock.h>
#include <InterfaceDefs.h>
#include <View.h>

namespace {

static bool TranslateLinuxKey(uint16 code, uint32 modifiers, char* outChar,
                              int32* outRawChar, int32* outHaikuKey)
{
    bool shift = (modifiers & B_SHIFT_KEY) != 0;
    bool caps = (modifiers & B_CAPS_LOCK) != 0;
    bool ctrl = (modifiers & B_CONTROL_KEY) != 0;
    bool upper = shift ^ caps;

    char ch = 0;
    int32 raw = 0;
    int32 key = static_cast<int32>(code);

    switch (code) {
        case KEY_A: raw = 'a'; ch = upper ? 'A' : 'a'; break;
        case KEY_B: raw = 'b'; ch = upper ? 'B' : 'b'; break;
        case KEY_C: raw = 'c'; ch = upper ? 'C' : 'c'; break;
        case KEY_D: raw = 'd'; ch = upper ? 'D' : 'd'; break;
        case KEY_E: raw = 'e'; ch = upper ? 'E' : 'e'; break;
        case KEY_F: raw = 'f'; ch = upper ? 'F' : 'f'; break;
        case KEY_G: raw = 'g'; ch = upper ? 'G' : 'g'; break;
        case KEY_H: raw = 'h'; ch = upper ? 'H' : 'h'; break;
        case KEY_I: raw = 'i'; ch = upper ? 'I' : 'i'; break;
        case KEY_J: raw = 'j'; ch = upper ? 'J' : 'j'; break;
        case KEY_K: raw = 'k'; ch = upper ? 'K' : 'k'; break;
        case KEY_L: raw = 'l'; ch = upper ? 'L' : 'l'; break;
        case KEY_M: raw = 'm'; ch = upper ? 'M' : 'm'; break;
        case KEY_N: raw = 'n'; ch = upper ? 'N' : 'n'; break;
        case KEY_O: raw = 'o'; ch = upper ? 'O' : 'o'; break;
        case KEY_P: raw = 'p'; ch = upper ? 'P' : 'p'; break;
        case KEY_Q: raw = 'q'; ch = upper ? 'Q' : 'q'; break;
        case KEY_R: raw = 'r'; ch = upper ? 'R' : 'r'; break;
        case KEY_S: raw = 's'; ch = upper ? 'S' : 's'; break;
        case KEY_T: raw = 't'; ch = upper ? 'T' : 't'; break;
        case KEY_U: raw = 'u'; ch = upper ? 'U' : 'u'; break;
        case KEY_V: raw = 'v'; ch = upper ? 'V' : 'v'; break;
        case KEY_W: raw = 'w'; ch = upper ? 'W' : 'w'; break;
        case KEY_X: raw = 'x'; ch = upper ? 'X' : 'x'; break;
        case KEY_Y: raw = 'y'; ch = upper ? 'Y' : 'y'; break;
        case KEY_Z: raw = 'z'; ch = upper ? 'Z' : 'z'; break;

        case KEY_1: raw = '1'; ch = shift ? '!' : '1'; break;
        case KEY_2: raw = '2'; ch = shift ? '@' : '2'; break;
        case KEY_3: raw = '3'; ch = shift ? '#' : '3'; break;
        case KEY_4: raw = '4'; ch = shift ? '$' : '4'; break;
        case KEY_5: raw = '5'; ch = shift ? '%' : '5'; break;
        case KEY_6: raw = '6'; ch = shift ? '^' : '6'; break;
        case KEY_7: raw = '7'; ch = shift ? '&' : '7'; break;
        case KEY_8: raw = '8'; ch = shift ? '*' : '8'; break;
        case KEY_9: raw = '9'; ch = shift ? '(' : '9'; break;
        case KEY_0: raw = '0'; ch = shift ? ')' : '0'; break;

        case KEY_KP0: raw = '0'; ch = '0'; break;
        case KEY_KP1: raw = '1'; ch = '1'; break;
        case KEY_KP2: raw = '2'; ch = '2'; break;
        case KEY_KP3: raw = '3'; ch = '3'; break;
        case KEY_KP4: raw = '4'; ch = '4'; break;
        case KEY_KP5: raw = '5'; ch = '5'; break;
        case KEY_KP6: raw = '6'; ch = '6'; break;
        case KEY_KP7: raw = '7'; ch = '7'; break;
        case KEY_KP8: raw = '8'; ch = '8'; break;
        case KEY_KP9: raw = '9'; ch = '9'; break;
        case KEY_KPDOT: raw = '.'; ch = '.'; break;
        case KEY_KPPLUS: raw = '+'; ch = '+'; break;
        case KEY_KPMINUS: raw = '-'; ch = '-'; break;
        case KEY_KPASTERISK: raw = '*'; ch = '*'; break;
        case KEY_KPSLASH: raw = '/'; ch = '/'; break;

        case KEY_MINUS: raw = '-'; ch = shift ? '_' : '-'; break;
        case KEY_EQUAL: raw = '='; ch = shift ? '+' : '='; break;
        case KEY_LEFTBRACE: raw = '['; ch = shift ? '{' : '['; break;
        case KEY_RIGHTBRACE: raw = ']'; ch = shift ? '}' : ']'; break;
        case KEY_BACKSLASH: raw = '\\'; ch = shift ? '|' : '\\'; break;
        case KEY_SEMICOLON: raw = ';'; ch = shift ? ':' : ';'; break;
        case KEY_APOSTROPHE: raw = '\''; ch = shift ? '"' : '\''; break;
        case KEY_GRAVE: raw = '`'; ch = shift ? '~' : '`'; key = 0x11; break;
        case KEY_COMMA: raw = ','; ch = shift ? '<' : ','; break;
        case KEY_DOT: raw = '.'; ch = shift ? '>' : '.'; break;
        case KEY_SLASH: raw = '/'; ch = shift ? '?' : '/'; break;
        case KEY_SPACE: raw = B_SPACE; ch = B_SPACE; key = B_SPACE_BAR_KEY; break;

        case KEY_ENTER:
        case KEY_KPENTER:
            raw = B_RETURN;
            ch = B_RETURN;
            key = 0x47;
            break;
        case KEY_BACKSPACE:
            raw = B_BACKSPACE;
            ch = B_BACKSPACE;
            key = 0x1e;
            break;
        case KEY_TAB:
            raw = B_TAB;
            ch = B_TAB;
            key = 0x26;
            break;
        case KEY_ESC:
            raw = B_ESCAPE;
            ch = B_ESCAPE;
            key = 0x01;
            break;
        case KEY_DELETE:
            raw = B_DELETE;
            ch = B_DELETE;
            key = 0x34;
            break;
        case KEY_INSERT:
            raw = B_INSERT;
            ch = B_INSERT;
            key = 0x1f;
            break;
        case KEY_HOME:
            raw = B_HOME;
            ch = B_HOME;
            key = 0x20;
            break;
        case KEY_END:
            raw = B_END;
            ch = B_END;
            key = 0x35;
            break;
        case KEY_PAGEUP:
            raw = B_PAGE_UP;
            ch = B_PAGE_UP;
            key = 0x21;
            break;
        case KEY_PAGEDOWN:
            raw = B_PAGE_DOWN;
            ch = B_PAGE_DOWN;
            key = 0x36;
            break;
        case KEY_UP:
            raw = B_UP_ARROW;
            ch = B_UP_ARROW;
            key = 0x57;
            break;
        case KEY_DOWN:
            raw = B_DOWN_ARROW;
            ch = B_DOWN_ARROW;
            key = 0x62;
            break;
        case KEY_LEFT:
            raw = B_LEFT_ARROW;
            ch = B_LEFT_ARROW;
            key = 0x61;
            break;
        case KEY_RIGHT:
            raw = B_RIGHT_ARROW;
            ch = B_RIGHT_ARROW;
            key = 0x63;
            break;

        case KEY_F1:
        case KEY_F2:
        case KEY_F3:
        case KEY_F4:
        case KEY_F5:
        case KEY_F6:
        case KEY_F7:
        case KEY_F8:
        case KEY_F9:
        case KEY_F10:
            raw = B_FUNCTION_KEY;
            ch = B_FUNCTION_KEY;
            key = B_F1_KEY + (code - KEY_F1);
            break;
        case KEY_F11:
            raw = B_FUNCTION_KEY;
            ch = B_FUNCTION_KEY;
            key = B_F11_KEY;
            break;
        case KEY_F12:
            raw = B_FUNCTION_KEY;
            ch = B_FUNCTION_KEY;
            key = B_F12_KEY;
            break;

        default:
            return false;
    }

    if (ctrl && raw >= 'a' && raw <= 'z') {
        ch = static_cast<char>((raw - 'a') + 1);
    }

    *outChar = ch;
    *outRawChar = raw;
    *outHaikuKey = key;
    return true;
}

}  // namespace

LinuxEvdevEventStream::LinuxEvdevEventStream(uint32 width, uint32 height)
    : EventStream(),
      fQuitting(false),
      fScreenBounds(0, 0, width > 0 ? width - 1 : 1279,
                    height > 0 ? height - 1 : 799),
      fCursorPos(width > 0 ? width / 2 : 640,
                 height > 0 ? height / 2 : 400),
      fButtons(0),
      fModifiers(0),
      fLastClickTime(0),
      fLastClickButton(0),
      fClickCount(0),
      fLastScanTime(0),
      fLastKeyPressTime(0),
      fEpollFd(-1),
      fWakeFd(-1),
      fDeviceCount(0),
      fOpenedMask(0),
      fLock("LinuxEvdevLock"),
      fLatestMouseMoved(nullptr)
{
    memset(fKeyDownState, 0, sizeof(fKeyDownState));
    for (int i = 0; i < 16; i++) {
        fDeviceFds[i] = -1;
        fAbsMinX[i] = 0;
        fAbsMaxX[i] = 32767;
        fAbsMinY[i] = 0;
        fAbsMaxY[i] = 32767;
    }

    fEpollFd = epoll_create1(EPOLL_CLOEXEC);
    fWakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fEpollFd >= 0 && fWakeFd >= 0) {
        struct epoll_event ev = {};
        ev.events = EPOLLIN;
        ev.data.fd = fWakeFd;
        epoll_ctl(fEpollFd, EPOLL_CTL_ADD, fWakeFd, &ev);
    }
    _ScanDevices();
}

LinuxEvdevEventStream::~LinuxEvdevEventStream()
{
    SendQuit();
    for (int i = 0; i < fDeviceCount; i++) {
        if (fDeviceFds[i] >= 0) {
            close(fDeviceFds[i]);
            fDeviceFds[i] = -1;
        }
    }
    if (fWakeFd >= 0) {
        close(fWakeFd);
        fWakeFd = -1;
    }
    if (fEpollFd >= 0) {
        close(fEpollFd);
        fEpollFd = -1;
    }
}

void LinuxEvdevEventStream::_ScanDevices()
{
    fLastScanTime = system_time();
    DIR* dir = opendir("/dev/input");
    if (!dir)
        return;

    struct _glibc_dirent* entry;
    while ((entry = readdir(dir)) != nullptr && fDeviceCount < 16) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            int idx = atoi(entry->d_name + 5);
            if (idx < 0 || idx >= 32)
                continue;
            if ((fOpenedMask & (1U << idx)) != 0)
                continue;

            char path[64];
            snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
            int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0) {
                struct epoll_event ev = {};
                ev.events = EPOLLIN;
                ev.data.fd = fd;
                if (epoll_ctl(fEpollFd, EPOLL_CTL_ADD, fd, &ev) == 0) {
                    int slot = fDeviceCount++;
                    fDeviceFds[slot] = fd;
                    fOpenedMask |= (1U << idx);

                    struct input_absinfo absInfo = {};
                    if (ioctl(fd, EVIOCGABS(ABS_X), &absInfo) == 0 &&
                        absInfo.maximum > absInfo.minimum) {
                        fAbsMinX[slot] = absInfo.minimum;
                        fAbsMaxX[slot] = absInfo.maximum;
                    }
                    if (ioctl(fd, EVIOCGABS(ABS_Y), &absInfo) == 0 &&
                        absInfo.maximum > absInfo.minimum) {
                        fAbsMinY[slot] = absInfo.minimum;
                        fAbsMaxY[slot] = absInfo.maximum;
                    }

                    char name[128] = "Unknown";
                    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
                    printf("[app_server/Evdev] Added input device %s (%s)\n",
                           path, name);
                    fflush(stdout);
                } else {
                    close(fd);
                }
            }
        }
    }
    closedir(dir);
}

bool LinuxEvdevEventStream::IsValid()
{
    return !fQuitting;
}

void LinuxEvdevEventStream::SendQuit()
{
    fQuitting = true;
    if (fWakeFd >= 0) {
        uint64_t val = 1;
        ssize_t r = write(fWakeFd, &val, sizeof(val));
        (void)r;
    }
}

void LinuxEvdevEventStream::UpdateScreenBounds(BRect bounds)
{
    BAutolock locker(fLock);
    fScreenBounds = bounds;
    if (fCursorPos.x < fScreenBounds.left) fCursorPos.x = fScreenBounds.left;
    if (fCursorPos.x > fScreenBounds.right) fCursorPos.x = fScreenBounds.right;
    if (fCursorPos.y < fScreenBounds.top) fCursorPos.y = fScreenBounds.top;
    if (fCursorPos.y > fScreenBounds.bottom) fCursorPos.y = fScreenBounds.bottom;
}

void LinuxEvdevEventStream::_FlushMouseMoved()
{
    BAutolock locker(fLock);
    if (fCursorPos.x < fScreenBounds.left) fCursorPos.x = fScreenBounds.left;
    if (fCursorPos.x > fScreenBounds.right) fCursorPos.x = fScreenBounds.right;
    if (fCursorPos.y < fScreenBounds.top) fCursorPos.y = fScreenBounds.top;
    if (fCursorPos.y > fScreenBounds.bottom) fCursorPos.y = fScreenBounds.bottom;

    BMessage* moveMsg = new BMessage(B_MOUSE_MOVED);
    moveMsg->AddInt64("when", system_time());
    moveMsg->AddPoint("where", fCursorPos);
    moveMsg->AddInt32("buttons", fButtons);
    moveMsg->AddInt32("modifiers", fModifiers);
    moveMsg->AddInt32("transit", B_INSIDE_VIEW);

    fLatestMouseMoved = moveMsg;
    fEventQueue.AddMessage(moveMsg);
}

void LinuxEvdevEventStream::_PollEvents(int timeoutMs)
{
    if (fEpollFd < 0)
        return;

    if (fDeviceCount == 0 || (system_time() - fLastScanTime) > 2000000) {
        _ScanDevices();
    }

    struct epoll_event events[16];
    int n = epoll_wait(fEpollFd, events, 16, timeoutMs);
    if (n <= 0)
        return;

    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        if (fd == fWakeFd) {
            uint64_t val = 0;
            ssize_t r = read(fWakeFd, &val, sizeof(val));
            (void)r;
            continue;
        }

        int slot = 0;
        for (int s = 0; s < fDeviceCount; s++) {
            if (fDeviceFds[s] == fd) {
                slot = s;
                break;
            }
        }

        struct input_event evs[64];
        ssize_t bytes = read(fd, evs, sizeof(evs));
        if (bytes <= 0)
            continue;

        size_t count = bytes / sizeof(struct input_event);
        bool moved = false;
        bool hasStandardWheel = false;
        for (size_t j = 0; j < count; j++) {
            if (evs[j].type == EV_REL &&
                (evs[j].code == REL_WHEEL || evs[j].code == REL_HWHEEL)) {
                hasStandardWheel = true;
                break;
            }
        }

        for (size_t j = 0; j < count; j++) {
            struct input_event* ev = &evs[j];
            if (ev->type == EV_REL) {
                if (ev->code == REL_X) {
                    fCursorPos.x += ev->value;
                    moved = true;
                } else if (ev->code == REL_Y) {
                    fCursorPos.y += ev->value;
                    moved = true;
                } else if (ev->code == REL_WHEEL && ev->value != 0) {
                    _FlushMouseMoved();
                    moved = false;
                    BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
                    msg->AddInt64("when", system_time());
                    msg->AddPoint("where", fCursorPos);
                    msg->AddInt32("buttons", fButtons);
                    msg->AddInt32("modifiers", fModifiers);
                    msg->AddFloat("be:wheel_delta_x", 0.0f);
                    msg->AddFloat("be:wheel_delta_y", static_cast<float>(-ev->value));
                    BAutolock locker(fLock);
                    fEventQueue.AddMessage(msg);
                } else if (ev->code == REL_HWHEEL && ev->value != 0) {
                    _FlushMouseMoved();
                    moved = false;
                    BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
                    msg->AddInt64("when", system_time());
                    msg->AddPoint("where", fCursorPos);
                    msg->AddInt32("buttons", fButtons);
                    msg->AddInt32("modifiers", fModifiers);
                    msg->AddFloat("be:wheel_delta_x", static_cast<float>(ev->value));
                    msg->AddFloat("be:wheel_delta_y", 0.0f);
                    BAutolock locker(fLock);
                    fEventQueue.AddMessage(msg);
#ifdef REL_WHEEL_HI_RES
                } else if (!hasStandardWheel && ev->code == REL_WHEEL_HI_RES &&
                           ev->value != 0) {
                    _FlushMouseMoved();
                    moved = false;
                    float dy = -static_cast<float>(ev->value) / 120.0f;
                    if (dy > -1.0f && dy < 0.0f) dy = -1.0f;
                    if (dy < 1.0f && dy > 0.0f) dy = 1.0f;
                    BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
                    msg->AddInt64("when", system_time());
                    msg->AddPoint("where", fCursorPos);
                    msg->AddInt32("buttons", fButtons);
                    msg->AddInt32("modifiers", fModifiers);
                    msg->AddFloat("be:wheel_delta_x", 0.0f);
                    msg->AddFloat("be:wheel_delta_y", dy);
                    BAutolock locker(fLock);
                    fEventQueue.AddMessage(msg);
#endif
#ifdef REL_HWHEEL_HI_RES
                } else if (!hasStandardWheel && ev->code == REL_HWHEEL_HI_RES &&
                           ev->value != 0) {
                    _FlushMouseMoved();
                    moved = false;
                    float dx = static_cast<float>(ev->value) / 120.0f;
                    if (dx > -1.0f && dx < 0.0f) dx = -1.0f;
                    if (dx < 1.0f && dx > 0.0f) dx = 1.0f;
                    BMessage* msg = new BMessage(B_MOUSE_WHEEL_CHANGED);
                    msg->AddInt64("when", system_time());
                    msg->AddPoint("where", fCursorPos);
                    msg->AddInt32("buttons", fButtons);
                    msg->AddInt32("modifiers", fModifiers);
                    msg->AddFloat("be:wheel_delta_x", dx);
                    msg->AddFloat("be:wheel_delta_y", 0.0f);
                    BAutolock locker(fLock);
                    fEventQueue.AddMessage(msg);
#endif
                }
            } else if (ev->type == EV_ABS) {
                if (ev->code == ABS_X) {
                    int minX = fAbsMinX[slot];
                    int maxX = fAbsMaxX[slot];
                    if (maxX > minX) {
                        fCursorPos.x = fScreenBounds.left +
                            static_cast<float>(ev->value - minX) *
                            fScreenBounds.Width() /
                            static_cast<float>(maxX - minX);
                        moved = true;
                    }
                } else if (ev->code == ABS_Y) {
                    int minY = fAbsMinY[slot];
                    int maxY = fAbsMaxY[slot];
                    if (maxY > minY) {
                        fCursorPos.y = fScreenBounds.top +
                            static_cast<float>(ev->value - minY) *
                            fScreenBounds.Height() /
                            static_cast<float>(maxY - minY);
                        moved = true;
                    }
                }
            } else if (ev->type == EV_KEY) {
                if (ev->code == BTN_LEFT || ev->code == BTN_RIGHT ||
                    ev->code == BTN_MIDDLE) {
                    if (moved) {
                        _FlushMouseMoved();
                        moved = false;
                    }
                    uint32 btn = B_PRIMARY_MOUSE_BUTTON;
                    if (ev->code == BTN_RIGHT)
                        btn = B_SECONDARY_MOUSE_BUTTON;
                    else if (ev->code == BTN_MIDDLE)
                        btn = B_TERTIARY_MOUSE_BUTTON;

                    bigtime_t now = system_time();
                    if (ev->value == 1) {
                        if (fLastClickButton == btn &&
                            (now - fLastClickTime) <= 500000) {
                            fClickCount++;
                        } else {
                            fClickCount = 1;
                        }
                        fLastClickTime = now;
                        fLastClickButton = btn;

                        fButtons |= btn;
                        BMessage* msg = new BMessage(B_MOUSE_DOWN);
                        msg->AddInt64("when", now);
                        msg->AddPoint("where", fCursorPos);
                        msg->AddInt32("buttons", fButtons);
                        msg->AddInt32("modifiers", fModifiers);
                        msg->AddInt32("clicks", fClickCount);
                        BAutolock locker(fLock);
                        fEventQueue.AddMessage(msg);
                    } else if (ev->value == 0) {
                        fButtons &= ~btn;
                        BMessage* msg = new BMessage(B_MOUSE_UP);
                        msg->AddInt64("when", now);
                        msg->AddPoint("where", fCursorPos);
                        msg->AddInt32("buttons", fButtons);
                        msg->AddInt32("modifiers", fModifiers);
                        BAutolock locker(fLock);
                        fEventQueue.AddMessage(msg);
                    }
                    continue;
                }

                // Modifier keys
                uint32 oldMods = fModifiers;
                bool isModifier = true;
                switch (ev->code) {
                    case KEY_LEFTSHIFT:
                        if (ev->value)
                            fModifiers |= (B_SHIFT_KEY | B_LEFT_SHIFT_KEY);
                        else
                            fModifiers &= ~(B_SHIFT_KEY | B_LEFT_SHIFT_KEY);
                        break;
                    case KEY_RIGHTSHIFT:
                        if (ev->value)
                            fModifiers |= (B_SHIFT_KEY | B_RIGHT_SHIFT_KEY);
                        else
                            fModifiers &= ~(B_SHIFT_KEY | B_RIGHT_SHIFT_KEY);
                        break;
                    case KEY_LEFTCTRL:
                        if (ev->value)
                            fModifiers |= (B_CONTROL_KEY | B_LEFT_CONTROL_KEY);
                        else
                            fModifiers &= ~(B_CONTROL_KEY | B_LEFT_CONTROL_KEY);
                        break;
                    case KEY_RIGHTCTRL:
                        if (ev->value)
                            fModifiers |= (B_CONTROL_KEY | B_RIGHT_CONTROL_KEY);
                        else
                            fModifiers &= ~(B_CONTROL_KEY | B_RIGHT_CONTROL_KEY);
                        break;
                    case KEY_LEFTALT:
                        if (ev->value) {
                            fModifiers |= (B_COMMAND_KEY | B_LEFT_COMMAND_KEY |
                                           B_OPTION_KEY | B_LEFT_OPTION_KEY);
                        } else {
                            fModifiers &= ~(B_COMMAND_KEY | B_LEFT_COMMAND_KEY |
                                            B_OPTION_KEY | B_LEFT_OPTION_KEY);
                        }
                        break;
                    case KEY_RIGHTALT:
                        if (ev->value) {
                            fModifiers |= (B_COMMAND_KEY | B_RIGHT_COMMAND_KEY |
                                           B_OPTION_KEY | B_RIGHT_OPTION_KEY);
                        } else {
                            fModifiers &= ~(B_COMMAND_KEY | B_RIGHT_COMMAND_KEY |
                                            B_OPTION_KEY | B_RIGHT_OPTION_KEY);
                        }
                        break;
                    case KEY_CAPSLOCK:
                        if (ev->value == 1)
                            fModifiers ^= B_CAPS_LOCK;
                        break;
                    default:
                        isModifier = false;
                        break;
                }

                if ((fModifiers & (B_LEFT_SHIFT_KEY | B_RIGHT_SHIFT_KEY)) != 0)
                    fModifiers |= B_SHIFT_KEY;
                if ((fModifiers & (B_LEFT_CONTROL_KEY | B_RIGHT_CONTROL_KEY)) != 0)
                    fModifiers |= B_CONTROL_KEY;
                if ((fModifiers & (B_LEFT_COMMAND_KEY | B_RIGHT_COMMAND_KEY)) != 0)
                    fModifiers |= B_COMMAND_KEY;
                if ((fModifiers & (B_LEFT_OPTION_KEY | B_RIGHT_OPTION_KEY)) != 0)
                    fModifiers |= B_OPTION_KEY;

                if (isModifier) {
                    if (fModifiers != oldMods) {
                        BMessage* modMsg = new BMessage(B_MODIFIERS_CHANGED);
                        modMsg->AddInt64("when", system_time());
                        modMsg->AddInt32("modifiers", fModifiers);
                        modMsg->AddInt32("be:old_modifiers", oldMods);
                        BAutolock locker(fLock);
                        fEventQueue.AddMessage(modMsg);
                    }
                    continue;
                }

                if (ev->code >= 256)
                    continue;

                bigtime_t nowKey = system_time();
                if (ev->value == 1) {
                    if (fKeyDownState[ev->code])
                        continue;
                    fKeyDownState[ev->code] = true;
                    fLastKeyPressTime = nowKey;
                } else if (ev->value == 0) {
                    if (!fKeyDownState[ev->code])
                        continue;
                    fKeyDownState[ev->code] = false;
                } else if (ev->value == 2) {
                    if (!fKeyDownState[ev->code] ||
                        (nowKey - fLastKeyPressTime) < 400000) {
                        continue;
                    }
                }

                char ch = 0;
                int32 rawChar = 0;
                int32 haikuKey = 0;
                if (!TranslateLinuxKey(ev->code, fModifiers, &ch, &rawChar,
                                       &haikuKey)) {
                    continue;
                }

                uint32 what = (ev->value != 0) ? B_KEY_DOWN : B_KEY_UP;
                char bytesStr[2] = {ch, '\0'};

                BMessage* msg = new BMessage(what);
                msg->AddInt64("when", system_time());
                msg->AddInt32("key", haikuKey);
                msg->AddInt32("modifiers", fModifiers);
                msg->AddInt32("raw_char", rawChar);
                msg->AddInt8("byte", static_cast<int8>(ch));
                msg->AddString("bytes", bytesStr);
                if (ev->value == 2) {
                    msg->AddInt32("be:key_repeat", 1);
                }

                BAutolock locker(fLock);
                fEventQueue.AddMessage(msg);
            }
        }

        if (moved) {
            _FlushMouseMoved();
        }
    }
}

bool LinuxEvdevEventStream::GetNextEvent(BMessage** _event)
{
    if (!_event)
        return false;

    while (!fQuitting) {
        {
            BAutolock locker(fLock);
            if (!fEventQueue.IsEmpty()) {
                *_event = fEventQueue.NextMessage();
                return true;
            }
        }
        _PollEvents(50);
    }

    *_event = nullptr;
    return false;
}

status_t LinuxEvdevEventStream::GetNextCursorPosition(BPoint& where,
                                                      bigtime_t timeout)
{
    (void)timeout;
    BAutolock locker(fLock);
    where = fCursorPos;
    return B_OK;
}

status_t LinuxEvdevEventStream::InsertEvent(BMessage* event)
{
    if (!event)
        return B_BAD_VALUE;

    {
        BAutolock locker(fLock);
        if (event->what == B_MOUSE_MOVED)
            fLatestMouseMoved = event;
        fEventQueue.AddMessage(event);
    }

    if (fWakeFd >= 0) {
        uint64_t val = 1;
        ssize_t r = write(fWakeFd, &val, sizeof(val));
        (void)r;
    }
    return B_OK;
}

BMessage* LinuxEvdevEventStream::PeekLatestMouseMoved()
{
    BAutolock locker(fLock);
    return fLatestMouseMoved;
}
