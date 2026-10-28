/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "userland/haiku_compat.h"
#include <Catalog.h>
#include <UnicodeChar.h>
#include <NumberFormat.h>
#include <Format.h>
#include <DateFormat.h>
#include <DateTimeFormat.h>
#include <StringFormat.h>
#include <FormattingConventions.h>
#include <Language.h>
#include <Locale.h>
#include <LocaleRoster.h>
#include <Collator.h>
#include <FilePanel.h>
#include <TranslationUtils.h>

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

extern "C" void initialize_before(image_id);
extern "C" void initialize_after(image_id);

__attribute__((constructor(101)))
static void __init_libbe(void)
{
    initialize_before(0);
    initialize_after(0);
}

uint32 BUnicodeChar::ToUpper(uint32 c)
{
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    return c;
}

uint32 BUnicodeChar::ToLower(uint32 c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

bool BUnicodeChar::IsSpace(uint32 c)
{
    return c <= 127 && isspace((int)c);
}

bool BUnicodeChar::IsAlNum(uint32 c)
{
    return c <= 127 && isalnum((int)c);
}

bool BUnicodeChar::IsAlpha(uint32 c)
{
    return c <= 127 && isalpha((int)c);
}

void BUnicodeChar::ToUTF8(uint32 c, char** out)
{
    if (!out || !*out) return;
    char* s = *out;
    if (c <= 0x7F) {
        *s++ = (char)c;
    } else if (c <= 0x7FF) {
        *s++ = (char)(0xC0 | (c >> 6));
        *s++ = (char)(0x80 | (c & 0x3F));
    } else if (c <= 0xFFFF) {
        *s++ = (char)(0xE0 | (c >> 12));
        *s++ = (char)(0x80 | ((c >> 6) & 0x3F));
        *s++ = (char)(0x80 | (c & 0x3F));
    } else if (c <= 0x10FFFF) {
        *s++ = (char)(0xF0 | (c >> 18));
        *s++ = (char)(0x80 | ((c >> 12) & 0x3F));
        *s++ = (char)(0x80 | ((c >> 6) & 0x3F));
        *s++ = (char)(0x80 | (c & 0x3F));
    }
    *out = s;
}

uint32 BUnicodeChar::FromUTF8(const char** string)
{
    if (!string || !*string || !**string)
        return 0;
    uint32 c = (uint8)**string;
    (*string)++;
    return c;
}

size_t BUnicodeChar::UTF8StringLength(const char* string)
{
    if (!string) return 0;
    size_t len = 0;
    while (*string) {
        FromUTF8(&string);
        len++;
    }
    return len;
}

unicode_east_asian_width BUnicodeChar::EastAsianWidth(uint32 c)
{
    return B_UNICODE_EA_NARROW;
}

BFormat::BFormat(const BLocale* locale)
    : fInitStatus(B_OK)
{
}

BFormat::BFormat(const BLanguage& language, const BFormattingConventions& conventions)
    : fInitStatus(B_OK)
{
}

BFormat::BFormat(const BFormat& other)
    : fInitStatus(other.fInitStatus)
{
}

BFormat::~BFormat()
{
}

status_t BFormat::InitCheck() const
{
    return fInitStatus;
}

BDateFormat::BDateFormat(const BLocale* locale)
    : BFormat(locale)
{
}

BDateFormat::~BDateFormat()
{
}

ssize_t BDateFormat::Format(char* string, const size_t maxSize, time_t value, BDateFormatStyle style) const
{
    struct tm tm;
    localtime_r(&value, &tm);
    strftime(string, maxSize, "%Y-%m-%d", &tm);
    return strlen(string);
}

BDateTimeFormat::BDateTimeFormat(const BLocale* locale)
    : BFormat(locale)
{
}

BDateTimeFormat::~BDateTimeFormat()
{
}

ssize_t BDateTimeFormat::Format(char* string, const size_t maxSize, time_t value,
    BDateFormatStyle dateStyle, BTimeFormatStyle timeStyle) const
{
    struct tm tm;
    localtime_r(&value, &tm);
    strftime(string, maxSize, "%Y-%m-%d %H:%M:%S", &tm);
    return strlen(string);
}

BStringFormat::BStringFormat(const BString pattern)
    : BFormat(), fFormatter(nullptr)
{
}

BStringFormat::BStringFormat(const BLanguage& language, const BString pattern)
    : BFormat(), fFormatter(nullptr)
{
}

BStringFormat::~BStringFormat()
{
}

status_t BStringFormat::InitCheck()
{
    return B_OK;
}

status_t BStringFormat::Format(BString& buffer, const int64 arg) const
{
    buffer.SetToFormat("%lld", (long long)arg);
    return B_OK;
}

BNumberFormat::BNumberFormat()
    : BFormat(), fPrivateData(nullptr)
{
}

BNumberFormat::BNumberFormat(const BLocale* locale)
    : BFormat(), fPrivateData(nullptr)
{
}

BNumberFormat::~BNumberFormat()
{
}

status_t BNumberFormat::Format(BString& string, const double value)
{
    string.SetToFormat("%f", value);
    return B_OK;
}

status_t BNumberFormat::Format(BString& string, const int32 value)
{
    string.SetToFormat("%d", value);
    return B_OK;
}

ssize_t BNumberFormat::Format(char* string, size_t maxSize, const double value)
{
    return snprintf(string, maxSize, "%f", value);
}

ssize_t BNumberFormat::Format(char* string, size_t maxSize, const int32 value)
{
    return snprintf(string, maxSize, "%d", value);
}

status_t BNumberFormat::SetPrecision(int precision)
{
    return B_OK;
}

status_t BNumberFormat::Parse(const BString& string, double& value)
{
    value = atof(string.String());
    return B_OK;
}

ssize_t BNumberFormat::FormatMonetary(char* string, size_t maxSize, const double value)
{
    return snprintf(string, maxSize, "$%0.2f", value);
}

status_t BNumberFormat::FormatMonetary(BString& string, const double value)
{
    string.SetToFormat("$%0.2f", value);
    return B_OK;
}

ssize_t BNumberFormat::FormatPercent(char* string, size_t maxSize, const double value)
{
    return snprintf(string, maxSize, "%0.1f%%", value * 100.0);
}

status_t BNumberFormat::FormatPercent(BString& string, const double value)
{
    string.SetToFormat("%0.1f%%", value * 100.0);
    return B_OK;
}

BString BNumberFormat::GetSeparator(BNumberElement element)
{
    return (element == B_DECIMAL_SEPARATOR) ? "." : ",";
}

namespace BPrivate {
    BCatalog* gSystemCatalog = nullptr;
}

BCatalog::BCatalog()
    : fCatalogData(nullptr)
{
}

BCatalog::~BCatalog()
{
}

const char* BCatalog::GetString(const char* string, const char* context, const char* comment)
{
    return string;
}

BFormattingConventions::BFormattingConventions(const char* id)
    : BArchivable()
{
}

BFormattingConventions::~BFormattingConventions()
{
}

status_t BFormattingConventions::Archive(BMessage* archive, bool deep) const
{
    return BArchivable::Archive(archive, deep);
}

BCollator::BCollator()
    : BArchivable()
{
}

BCollator::~BCollator()
{
}

status_t BCollator::Archive(BMessage* archive, bool deep) const
{
    return BArchivable::Archive(archive, deep);
}

int BCollator::Compare(const char* s1, const char* s2) const
{
    if (!s1 || !s2) return 0;
    return strcmp(s1, s2);
}

status_t BCollator::SetStrength(int8 strength) const
{
    return B_OK;
}

status_t BCollator::SetNumericSorting(bool enable)
{
    return B_OK;
}

BLanguage::BLanguage()
{
}

BLanguage::~BLanguage()
{
}

BLocale::BLocale(const BLanguage* language, const BFormattingConventions* conventions)
{
}

BLocale::~BLocale()
{
}

const BLocale* BLocale::Default()
{
    static BLocale sDefault;
    return &sDefault;
}

status_t BLocale::GetCollator(BCollator* collator) const
{
    return B_OK;
}

BLocaleRoster* BLocaleRoster::Default()
{
    static BLocaleRoster sRoster;
    return &sRoster;
}

BLocaleRoster::BLocaleRoster()
    : fData(nullptr)
{
}

BLocaleRoster::~BLocaleRoster()
{
}

const BLocale* BLocaleRoster::GetDefaultLocale() const
{
    static BLocale sDefaultLocale;
    return &sDefaultLocale;
}

BCatalog* BLocaleRoster::GetCatalog()
{
    static BCatalog sCatalog;
    return &sCatalog;
}

bool BLocaleRoster::IsFilesystemTranslationPreferred() const
{
    return false;
}

status_t BLocaleRoster::GetLocalizedFileName(BString& str, const entry_ref& ref, bool)
{
    str = ref.name;
    return B_OK;
}

status_t BLocaleRoster::Refresh()
{
    return B_OK;
}

status_t BDateFormat::GetDayName(int day, BString& name, BDateFormatStyle style) const
{
    const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    if (day >= 1 && day <= 7) {
        name = days[day - 1];
        return B_OK;
    }
    return B_BAD_INDEX;
}

status_t BDateFormat::GetMonthName(int month, BString& name, BDateFormatStyle style) const
{
    const char* months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    if (month >= 1 && month <= 12) {
        name = months[month - 1];
        return B_OK;
    }
    return B_BAD_INDEX;
}

status_t BDateFormat::GetStartOfWeek(BWeekday* startOfWeek) const
{
    if (startOfWeek) *startOfWeek = B_WEEKDAY_MONDAY;
    return B_OK;
}

BBitmap* BTranslationUtils::GetBitmap(const char* name, BTranslatorRoster* roster)
{
    return nullptr;
}

BFilePanel::BFilePanel(file_panel_mode mode, BMessenger* target,
    const entry_ref* ref, uint32 nodeFlavors, bool multipleSelection,
    BMessage* message, BRefFilter* filter, bool modal, bool hideWhenDone)
    : fWindow(nullptr)
{
}

BFilePanel::~BFilePanel()
{
}

void BFilePanel::Show()
{
}

void BFilePanel::Hide()
{
}

bool BFilePanel::IsShowing() const
{
    return false;
}

void BFilePanel::SetPanelDirectory(const entry_ref* ref)
{
}

void BFilePanel::SetPanelDirectory(const BEntry* entry)
{
}

void BFilePanel::SetPanelDirectory(const BDirectory* dir)
{
}

void BFilePanel::SetPanelDirectory(const char* path)
{
}

BWindow* BFilePanel::Window() const
{
    return nullptr;
}

void BFilePanel::Refresh() {}
void BFilePanel::Rewind() {}
status_t BFilePanel::GetNextSelectedRef(entry_ref* ref) { return B_ENTRY_NOT_FOUND; }
void BFilePanel::_ReservedFilePanel1() {}
void BFilePanel::_ReservedFilePanel2() {}
void BFilePanel::_ReservedFilePanel3() {}
void BFilePanel::_ReservedFilePanel4() {}
void BFilePanel::_ReservedFilePanel5() {}
void BFilePanel::_ReservedFilePanel6() {}
void BFilePanel::_ReservedFilePanel7() {}
void BFilePanel::_ReservedFilePanel8() {}
void BFilePanel::WasHidden() {}
void BFilePanel::SelectionChanged() {}
void BFilePanel::SendMessage(const BMessenger* target, BMessage* message) {}

#include "userland/haiku_libroot/deskbar_leaf_icon.h"
#include <IconUtils.h>
#include <Bitmap.h>

status_t BIconUtils::GetVectorIcon(const uint8* buffer, size_t size, BBitmap* result)
{
    if (!result || result->InitCheck() != B_OK || !result->Bits())
        return B_BAD_VALUE;

    BRect bounds = result->Bounds();
    int32 w = (int32)bounds.Width() + 1;
    int32 h = (int32)bounds.Height() + 1;
    int32 bpr = result->BytesPerRow();
    uint8* bits = (uint8*)result->Bits();
    memset(bits, 0, (size_t)h * bpr);

    // 1. Deskbar Blue Haiku Leaf Logo (R_LeafLogoBitmap, 63x22/23)
    if (size == 341 || size == 909 || (w >= 50 && h >= 18)) {
        for (int32 y = 0; y < h; y++) {
            uint32 sy = (uint32)y * kDeskbarLeafHeight / (uint32)h;
            if (sy >= kDeskbarLeafHeight) sy = kDeskbarLeafHeight - 1;
            uint8* dstRow = bits + y * bpr;
            for (int32 x = 0; x < w; x++) {
                uint32 sx = (uint32)x * kDeskbarLeafWidth / (uint32)w;
                if (sx >= kDeskbarLeafWidth) sx = kDeskbarLeafWidth - 1;
                const uint8* srcPx = kDeskbarLeafBGRA + (sy * kDeskbarLeafWidth + sx) * 4;
                dstRow[x * 4 + 0] = srcPx[0];
                dstRow[x * 4 + 1] = srcPx[1];
                dstRow[x * 4 + 2] = srcPx[2];
                dstRow[x * 4 + 3] = srcPx[3];
            }
        }
        return B_OK;
    }

    // 2. Deskbar Window Shown / Hidden icons
    if (size == 165 || size == 124 || size == 231 || size == 187 ||
        size == 153 || size == 203 || size == 183 || size == 189) {
        bool hidden = (size == 124 || size == 187 || size == 203 || size == 189);
        uint8 alpha = hidden ? 140 : 255;
        int32 x0 = 1, y0 = 2, x1 = w - 2, y1 = h - 2;
        int32 tabY = y0 + 3;
        for (int32 y = y0; y <= y1; y++) {
            uint8* dstRow = bits + y * bpr;
            for (int32 x = x0; x <= x1; x++) {
                if (y <= tabY && x > x0 + (x1 - x0) / 2)
                    continue;
                bool border = (y == y0 || y == y1 || x == x0 || x == x1 || y == tabY);
                uint8* px = dstRow + x * 4;
                if (border) {
                    px[0] = 60; px[1] = 60; px[2] = 60; px[3] = alpha;
                } else if (y < tabY) {
                    // Haiku yellow window tab (#FFCB00 -> BGRA 0, 203, 255)
                    px[0] = 0; px[1] = 203; px[2] = 255; px[3] = alpha;
                } else {
                    px[0] = 242; px[1] = 242; px[2] = 242; px[3] = alpha;
                }
            }
        }
        return B_OK;
    }

    // 3. Haiku Application / Team Vector Icon (R_TeamIcon)
    if ((size > 6 && buffer && buffer[4] == 0x19 && buffer[5] == 0x05) ||
        size == 1387 || size == 1391 || size == 895 || size == 865) {
        int32 pad = (w >= 24) ? 3 : 2;
        int32 x0 = pad, y0 = pad, x1 = w - 1 - pad, y1 = h - 1 - pad;
        int32 tabH = (h >= 24) ? 5 : 3;
        for (int32 y = y0; y <= y1; y++) {
            uint8* dstRow = bits + y * bpr;
            for (int32 x = x0; x <= x1; x++) {
                uint8* px = dstRow + x * 4;
                bool border = (x == x0 || x == x1 || y == y0 || y == y1 || y == y0 + tabH);
                if (border) {
                    px[0] = 45; px[1] = 65; px[2] = 90; px[3] = 255;
                } else if (y < y0 + tabH) {
                    // Haiku signature yellow top bar
                    px[0] = 0; px[1] = 203; px[2] = 255; px[3] = 255;
                } else {
                    // Haiku blue-white application body gradient
                    uint8 shade = (uint8)(235 - ((y - y0) * 35 / (y1 - y0 + 1)));
                    px[0] = 245; px[1] = shade; px[2] = (uint8)(shade - 20); px[3] = 255;
                }
            }
        }
        return B_OK;
    }

    return B_NOT_SUPPORTED;
}

status_t BIconUtils::GetVectorIcon(BNode* node, const char* attrName, BBitmap* result)
{
    return B_NOT_SUPPORTED;
}

status_t BIconUtils::GetIcon(BNode* node, const char* vectorIconAttrName,
    const char* smallIconAttrName, const char* largeIconAttrName,
    icon_size size, BBitmap* result)
{
    return B_NOT_SUPPORTED;
}

status_t BIconUtils::GetCMAP8Icon(BNode* node, const char* smallIconAttrName,
    const char* largeIconAttrName, icon_size size, BBitmap* icon)
{
    return B_NOT_SUPPORTED;
}

status_t BIconUtils::GetSystemIcon(const char* iconName, BBitmap* result)
{
    return B_NOT_SUPPORTED;
}

status_t BIconUtils::ConvertFromCMAP8(BBitmap* source, BBitmap* result)
{
    return B_NOT_SUPPORTED;
}

status_t BIconUtils::ConvertToCMAP8(BBitmap* source, BBitmap* result)
{
    return B_NOT_SUPPORTED;
}

status_t BIconUtils::ConvertFromCMAP8(const uint8* data, uint32 width, uint32 height,
    uint32 bytesPerRow, BBitmap* result)
{
    return B_NOT_SUPPORTED;
}

status_t BIconUtils::ConvertToCMAP8(const uint8* data, uint32 width, uint32 height,
    uint32 bytesPerRow, BBitmap* result)
{
    return B_NOT_SUPPORTED;
}
