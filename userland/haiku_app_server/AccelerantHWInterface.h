/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#ifndef _ACCELERANT_HW_INTERFACE_H
#define _ACCELERANT_HW_INTERFACE_H

#include "LinuxFBHWInterface.h"

// Seamlessly alias AccelerantHWInterface to LinuxFBHWInterface
// so upstream ScreenManager instantiates our Linux framebuffer backend directly.
typedef LinuxFBHWInterface AccelerantHWInterface;

#endif // _ACCELERANT_HW_INTERFACE_H
