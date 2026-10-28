/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

#include <InterfaceDefs.h>
#include <Keymap.h>
#include <stdlib.h>
#include <string.h>

static void PopulateDefaultKeymap(key_map* keys, char** outChars, uint32* outCharsSize)
{
    memset(keys, 0, sizeof(*keys));
    keys->version = 3;
    keys->caps_key = 0x3b;
    keys->scroll_key = 0x0f;
    keys->num_key = 0x22;
    keys->left_shift_key = 0x4b;
    keys->right_shift_key = 0x56;
    keys->left_command_key = 0x5d;
    keys->right_command_key = 0x5f;
    keys->left_control_key = 0x5c;
    keys->right_control_key = 0x60;
    keys->left_option_key = 0x66;
    keys->right_option_key = 0x67;
    keys->menu_key = 0x68;

    // Build a simple Pascal-string table for 128 keycodes
    const size_t bufSize = 512;
    char* chars = (char*)calloc(1, bufSize);
    uint32 offset = 2; // 0 = empty Pascal string (length 0)
    for (int k = 1; k < 128 && offset + 4 < bufSize; k++) {
        chars[offset] = 1;
        chars[offset + 1] = (char)((k >= 32 && k < 127) ? k : ' ');
        keys->normal_map[k] = offset;
        keys->shift_map[k] = offset;
        keys->caps_map[k] = offset;
        keys->caps_shift_map[k] = offset;
        offset += 2;
    }
    if (outChars) *outChars = chars;
    else free(chars);
    if (outCharsSize) *outCharsSize = (uint32)bufSize;
}

status_t BKeymap::SetToCurrent()
{
    return SetToDefault();
}

status_t BKeymap::SetToDefault()
{
    delete[] fChars;
    fChars = nullptr;
    fCharsSize = 0;

    char* rawChars = nullptr;
    PopulateDefaultKeymap(&fKeys, &rawChars, &fCharsSize);
    if (rawChars && fCharsSize > 0) {
        fChars = new char[fCharsSize];
        memcpy(fChars, rawChars, fCharsSize);
        free(rawChars);
    }
    return B_OK;
}

void _get_key_map(key_map** map, char** key_buffer, ssize_t* key_buffer_size)
{
    if (!map || !key_buffer) return;
    *map = (key_map*)malloc(sizeof(key_map));
    uint32 sz = 0;
    PopulateDefaultKeymap(*map, key_buffer, &sz);
    if (key_buffer_size) *key_buffer_size = (ssize_t)sz;
}

void get_key_map(key_map** map, char** key_buffer)
{
    _get_key_map(map, key_buffer, nullptr);
}

#include <Directory.h>
#include <kernel/util/KMessage.h>
#include <stdio.h>

namespace BPrivate {

status_t get_extended_team_info(team_id team, uint32 /*flags*/, KMessage& info)
{
    char procPath[64];
    char nameBuf[64] = "sh";
    snprintf(procPath, sizeof(procPath), "/proc/%d/comm", (int)team);
    FILE* f = fopen(procPath, "r");
    if (f) {
        if (fgets(nameBuf, sizeof(nameBuf), f)) {
            size_t len = strlen(nameBuf);
            while (len > 0 && (nameBuf[len - 1] == '\n' || nameBuf[len - 1] == '\r'))
                nameBuf[--len] = '\0';
        }
        fclose(f);
    }

    char cwdLink[64];
    char cwdPath[B_PATH_NAME_LENGTH];
    snprintf(cwdLink, sizeof(cwdLink), "/proc/%d/cwd", (int)team);
    ssize_t len = readlink(cwdLink, cwdPath, sizeof(cwdPath) - 1);
    if (len > 0)
        cwdPath[len] = '\0';
    else
        strcpy(cwdPath, "/boot/home");

    BDirectory dir(cwdPath);
    node_ref nref;
    ino_t dirIno = 1;
    if (dir.GetNodeRef(&nref) == B_OK)
        dirIno = nref.node;

    info.AddString("name", nameBuf);
    info.AddInt32("cwd device", 1);
    info.AddInt64("cwd directory", (int64)dirIno);
    return B_OK;
}

}  // namespace BPrivate
