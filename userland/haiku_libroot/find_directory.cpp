/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include <FindDirectory.h>
#include <OS.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" status_t find_directory(directory_which which, dev_t device,
                                  bool create_it, char *path, int32 path_length)
{
    if (!path || path_length <= 0)
        return B_BAD_VALUE;

    const char* subpath = nullptr;

    switch (which) {
        case B_DESKTOP_DIRECTORY:
            subpath = "/boot/home/Desktop";
            break;
        case B_TRASH_DIRECTORY:
            subpath = "/boot/home/Trash";
            break;
        case B_SYSTEM_DIRECTORY:
            subpath = "/boot/system";
            break;
        case B_SYSTEM_FONTS_DIRECTORY:
        case B_SYSTEM_NONPACKAGED_FONTS_DIRECTORY:
            {
                const char* envFonts = getenv("HAIKU_FONTS_DIR");
                subpath = (envFonts && envFonts[0]) ? envFonts : "/boot/system/data/fonts";
            }
            break;
        case B_SYSTEM_DATA_DIRECTORY:
        case B_SYSTEM_NONPACKAGED_DATA_DIRECTORY:
            subpath = "/boot/system/data";
            break;
        case B_SYSTEM_SETTINGS_DIRECTORY:
            subpath = "/boot/system/settings";
            break;
        case B_SYSTEM_SERVERS_DIRECTORY:
            subpath = "/boot/system/servers";
            break;
        case B_SYSTEM_APPS_DIRECTORY:
            subpath = "/boot/system/apps";
            break;
        case B_SYSTEM_BIN_DIRECTORY:
        case B_SYSTEM_NONPACKAGED_BIN_DIRECTORY:
            subpath = "/boot/system/bin";
            break;
        case B_SYSTEM_LIB_DIRECTORY:
        case B_SYSTEM_NONPACKAGED_LIB_DIRECTORY:
            subpath = "/boot/system/lib";
            break;
        case B_SYSTEM_ADDONS_DIRECTORY:
        case B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY:
            subpath = "/boot/system/add-ons";
            break;
        case B_SYSTEM_TEMP_DIRECTORY:
            subpath = "/tmp";
            break;
        case B_USER_DIRECTORY:
            subpath = "/boot/home";
            break;
        case B_USER_CONFIG_DIRECTORY:
            subpath = "/boot/home/config";
            break;
        case B_USER_SETTINGS_DIRECTORY:
            subpath = "/boot/home/config/settings";
            break;
        case B_USER_DESKBAR_DIRECTORY:
            subpath = "/boot/home/config/settings/deskbar";
            break;
        default:
            subpath = "/boot/system";
            break;
    }

    if ((int32)strlen(subpath) >= path_length)
        return B_NAME_TOO_LONG;

    strncpy(path, subpath, path_length);
    path[path_length - 1] = '\0';

    if (create_it) {
        mkdir(path, 0755);
    }

    return B_OK;
}

extern "C" status_t find_path(const void* codePointer, path_base_directory baseDirectory,
    const char* subPath, char* pathBuffer, size_t bufferSize)
{
    return find_path_etc(codePointer, nullptr, nullptr, baseDirectory, subPath, 0, pathBuffer, bufferSize);
}

extern "C" status_t find_path_etc(const void* codePointer, const char* dependency,
    const char* architecture, path_base_directory baseDirectory,
    const char* subPath, uint32 flags, char* pathBuffer, size_t bufferSize)
{
    if (!pathBuffer || bufferSize == 0)
        return B_BAD_VALUE;
    const char* base = "/boot/system";
    if (subPath && subPath[0] != '\0') {
        snprintf(pathBuffer, bufferSize, "%s/%s", base, subPath);
    } else {
        snprintf(pathBuffer, bufferSize, "%s", base);
    }
    return B_OK;
}

extern "C" status_t find_path_for_path(const char* path, path_base_directory baseDirectory,
    const char* subPath, char* pathBuffer, size_t bufferSize)
{
    return find_path_for_path_etc(path, nullptr, nullptr, baseDirectory, subPath, 0, pathBuffer, bufferSize);
}

extern "C" status_t find_path_for_path_etc(const char* path, const char* dependency,
    const char* architecture, path_base_directory baseDirectory,
    const char* subPath, uint32 flags, char* pathBuffer, size_t bufferSize)
{
    return find_path_etc(nullptr, dependency, architecture, baseDirectory, subPath, flags, pathBuffer, bufferSize);
}

extern "C" status_t find_paths(path_base_directory baseDirectory, const char* subPath,
    char*** _paths, size_t* _pathCount)
{
    return find_paths_etc(nullptr, baseDirectory, subPath, 0, _paths, _pathCount);
}

extern "C" status_t find_paths_etc(const char* architecture,
    path_base_directory baseDirectory, const char* subPath, uint32 flags,
    char*** _paths, size_t* _pathCount)
{
    if (_pathCount) *_pathCount = 0;
    if (_paths) *_paths = nullptr;
    return B_ENTRY_NOT_FOUND;
}
