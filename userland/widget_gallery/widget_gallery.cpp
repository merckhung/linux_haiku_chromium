// SPDX-License-Identifier: MIT
/*
 * Haiku GUI Widget Gallery Application
 * Demonstrates the full range of native BeOS / Haiku Interface Kit widgets
 * running natively on the Linux kernel via app_server and libbe.so.
 */

#include <Application.h>
#include <Window.h>
#include <View.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <RadioButton.h>
#include <TextControl.h>
#include <TextView.h>
#include <StringView.h>
#include <Slider.h>
#include <StatusBar.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <MenuField.h>
#include <ScrollView.h>
#include <ListView.h>
#include <StringItem.h>
#include <TabView.h>
#include <ScrollBar.h>
#include <SeparatorView.h>
#include <Alert.h>
#include <String.h>
#include <ControlLook.h>
#include <HaikuControlLook.h>
#include <stdio.h>
#include <sys/utsname.h>
#include <unistd.h>

// Application Signature
static const char* kAppSignature = "application/x-vnd.Haiku-WidgetGallery";

// Message Command Codes
enum {
    MSG_BTN_PRIMARY     = 'btp1',
    MSG_BTN_SECONDARY   = 'btp2',
    MSG_BTN_ABOUT       = 'btab',
    MSG_BTN_RESET       = 'btrb',

    MSG_CHECKBOX_HW     = 'ckhw',
    MSG_CHECKBOX_AUDIO  = 'ckau',
    MSG_CHECKBOX_DPI    = 'ckdp',
    MSG_CHECKBOX_EVDEV  = 'ckev',
    MSG_CHECKBOX_CRASH  = 'ckcr',

    MSG_RADIO_ASYNC     = 'rdas',
    MSG_RADIO_SYNC      = 'rdsy',
    MSG_RADIO_POOL      = 'rdpo',

    MSG_MENU_RES        = 'mnre',
    MSG_MENU_GPU        = 'mngp',
    MSG_MENU_AUDIO      = 'mnau',
    MSG_MENU_COLOR      = 'mncl',

    MSG_TEXT_USER       = 'txus',
    MSG_TEXT_EMAIL      = 'txem',
    MSG_TEXT_URL        = 'txur',
    MSG_TEXT_PASS       = 'txps',

    MSG_LIST_SELECTED   = 'lsel',

    MSG_SLIDER_VOL      = 'slvo',
    MSG_SLIDER_GAMMA    = 'slga',
    MSG_SLIDER_BAL      = 'slba',

    MSG_PROGRESS_STEP   = 'pgst',
    MSG_PROGRESS_RESET  = 'pgrs',

    MSG_MENU_TAB0       = 'mtb0',
    MSG_MENU_TAB1       = 'mtb1',
    MSG_MENU_TAB2       = 'mtb2',
    MSG_MENU_TAB3       = 'mtb3'
};

// Continuous Slider with Dynamic Live Text
class GallerySlider : public BSlider {
public:
    enum StyleMode {
        STYLE_PERCENT,
        STYLE_FLOAT_TENTHS,
        STYLE_BALANCE
    };

    GallerySlider(BRect frame, const char* name, const char* label,
                  BMessage* message, int32 minValue, int32 maxValue,
                  StyleMode mode = STYLE_PERCENT,
                  thumb_style thumb = B_BLOCK_THUMB)
        : BSlider(frame, name, label, message, minValue, maxValue, thumb),
          fMode(mode)
    {
    }

    virtual const char* UpdateText() const override
    {
        int32 v = Value();
        switch (fMode) {
            case STYLE_FLOAT_TENTHS:
                snprintf(fBuffer, sizeof(fBuffer), "%0.1f", v / 10.0f);
                break;
            case STYLE_BALANCE:
                if (v == 0)
                    snprintf(fBuffer, sizeof(fBuffer), "Center");
                else if (v < 0)
                    snprintf(fBuffer, sizeof(fBuffer), "L %d", (int)-v);
                else
                    snprintf(fBuffer, sizeof(fBuffer), "R %d", (int)v);
                break;
            case STYLE_PERCENT:
            default:
                snprintf(fBuffer, sizeof(fBuffer), "%d%%", (int)v);
                break;
        }
        return fBuffer;
    }

private:
    StyleMode fMode;
    mutable char fBuffer[32];
};

// Custom 2D Graphics Canvas View
class GalleryCanvasView : public BView {
public:
    GalleryCanvasView(BRect frame)
        : BView(frame, "GalleryCanvasView", B_FOLLOW_ALL, B_WILL_DRAW)
    {
        SetViewColor(245, 245, 245);
    }

    virtual void Draw(BRect updateRect) override
    {
        BRect b = Bounds();

        // 1. Grid Background
        SetHighColor(228, 228, 228);
        SetPenSize(1.0f);
        for (float x = 20; x < b.right; x += 25)
            StrokeLine(BPoint(x, 0), BPoint(x, b.bottom));
        for (float y = 20; y < b.bottom; y += 25)
            StrokeLine(BPoint(0, y), BPoint(b.right, y));

        // 2. Banner with Haiku Yellow and drop shadow
        BRect shadow(18, 14, b.right - 18, 52);
        SetHighColor(210, 210, 210);
        FillRoundRect(shadow, 6.0f, 6.0f);

        BRect banner(16, 12, b.right - 20, 50);
        SetHighColor(255, 203, 0); // Iconic Haiku Yellow
        FillRoundRect(banner, 6.0f, 6.0f);
        SetHighColor(200, 160, 0);
        StrokeRoundRect(banner, 6.0f, 6.0f);

        SetHighColor(20, 20, 20);
        SetLowColor(255, 203, 0);
        SetFont(be_bold_font);
        DrawString("Haiku 2D Vector Graphics Engine (Anti-Grain Geometry + FreeType)", BPoint(26, 34));

        // 3. Color Swatches Palette
        struct Swatch { const char* name; rgb_color color; } swatches[] = {
            {"Haiku Yellow", {255, 203, 0, 255}},
            {"Sky Blue",     {51, 102, 153, 255}},
            {"Leaf Green",   {46, 160, 67, 255}},
            {"Alert Red",    {220, 53, 69, 255}},
            {"Slate Grey",   {85, 95, 110, 255}},
            {"Cobalt",       {26, 115, 232, 255}},
            {"Orange",       {255, 112, 67, 255}}
        };
        float sx = 20.0f;
        for (int i = 0; i < 7; i++) {
            BRect sRect(sx, 64, sx + 98, 92);
            SetHighColor(swatches[i].color);
            FillRoundRect(sRect, 4.0f, 4.0f);
            SetHighColor(40, 40, 40);
            StrokeRoundRect(sRect, 4.0f, 4.0f);
            sx += 108.0f;
        }

        // 4. Circle / Ellipse Primitives
        BRect circle1(25, 110, 105, 190);
        SetHighColor(51, 102, 153);
        FillEllipse(circle1);
        SetHighColor(20, 50, 90);
        SetPenSize(2.0f);
        StrokeEllipse(circle1);

        BRect circle2(120, 110, 200, 190);
        SetHighColor(220, 53, 69);
        FillEllipse(circle2);
        SetHighColor(140, 20, 30);
        StrokeEllipse(circle2);

        BRect circle3(215, 110, 295, 190);
        SetHighColor(46, 160, 67);
        FillEllipse(circle3);
        SetHighColor(20, 100, 35);
        StrokeEllipse(circle3);

        // 5. Concentric Radar Target Reticle
        BPoint center(380, 150);
        SetPenSize(1.5f);
        SetHighColor(32, 56, 100);
        for (float r = 12.0f; r <= 48.0f; r += 12.0f) {
            StrokeEllipse(BRect(center.x - r, center.y - r, center.x + r, center.y + r));
        }
        StrokeLine(BPoint(center.x - 56, center.y), BPoint(center.x + 56, center.y));
        StrokeLine(BPoint(center.x, center.y - 56), BPoint(center.x, center.y + 56));

        // 6. Polyline Trend Chart
        SetHighColor(240, 240, 240);
        FillRect(BRect(470, 105, 755, 195));
        SetHighColor(180, 180, 180);
        StrokeRect(BRect(470, 105, 755, 195));

        SetHighColor(46, 160, 67);
        SetPenSize(2.5f);
        BPoint pts[] = {
            {480, 180}, {515, 135}, {550, 160}, {585, 120},
            {620, 145}, {655, 115}, {690, 130}, {740, 110}
        };
        for (int p = 0; p < 7; p++) {
            StrokeLine(pts[p], pts[p+1]);
        }
        SetHighColor(255, 203, 0);
        for (int p = 0; p < 8; p++) {
            FillEllipse(BRect(pts[p].x - 3, pts[p].y - 3, pts[p].x + 3, pts[p].y + 3));
            SetHighColor(20, 20, 20);
            StrokeEllipse(BRect(pts[p].x - 3, pts[p].y - 3, pts[p].x + 3, pts[p].y + 3));
            SetHighColor(255, 203, 0);
        }

        // 7. Footer note
        SetHighColor(60, 60, 60);
        SetLowColor(245, 245, 245);
        SetPenSize(1.0f);
        SetFont(be_plain_font);
        DrawString("Real-time anti-aliased subpixel rendering powered natively by Haiku app_server", BPoint(20, 225));
    }
};

// Main Window Class
class GalleryWindow : public BWindow {
public:
    GalleryWindow(BRect frame)
        : BWindow(frame, "Haiku GUI Widget Gallery", B_TITLED_WINDOW,
                  B_QUIT_ON_WINDOW_CLOSE | B_NOT_ZOOMABLE),
          fInteractionCount(0),
          fTabView(NULL),
          fStatusString(NULL),
          fCounterString(NULL),
          fStatusBar1(NULL),
          fStatusBar2(NULL),
          fVolSlider(NULL),
          fGammaSlider(NULL),
          fBalSlider(NULL),
          fUserText(NULL),
          fEmailText(NULL),
          fUrlText(NULL),
          fPassText(NULL),
          fListView(NULL)
    {
        _InitLayout();
    }

    virtual void MessageReceived(BMessage* message) override
    {
        fInteractionCount++;
        _UpdateCounter();

        switch (message->what) {
            case MSG_BTN_PRIMARY:
                _SetStatus("Primary Action Button executed!");
                if (fStatusBar1)
                    fStatusBar1->Update(10.0f, "Action executed", "In Progress");
                break;

            case MSG_BTN_SECONDARY:
                _SetStatus("Standard Push Button clicked.");
                if (fStatusBar1)
                    fStatusBar1->Update(-5.0f, "Action stepped back", "Active");
                break;

            case MSG_BTN_RESET:
                _ResetAll();
                break;

            case MSG_BTN_ABOUT: {
                BAlert* alert = new BAlert(
                    "About Haiku Widget Gallery",
                    "Haiku GUI Widget Gallery\n\n"
                    "Comprehensive demonstration of BeOS / Haiku Interface Kit:\n"
                    "• BTabView, BTab (Folder Notebook Tabs)\n"
                    "• BMenuBar, BMenu, BMenuItem, BPopUpMenu, BMenuField\n"
                    "• BButton, BCheckBox, BRadioButton, BBox\n"
                    "• BTextControl, BTextView, BScrollView, BScrollBar\n"
                    "• BListView, BStringItem (Scrollable Selection List)\n"
                    "• BSlider, BStatusBar, BStringView, BSeparatorView\n"
                    "• 2D Vector Drawing (AGG anti-aliasing via app_server)\n\n"
                    "Running 100% natively on 64-bit Linux Kernel 6.12.",
                    "Close", NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
                alert->Go();
                _SetStatus("About Dialog displayed.");
                break;
            }

            case MSG_CHECKBOX_HW: {
                int32 val = 0;
                if (message->FindInt32("be:value", &val) == B_OK)
                    _SetStatus(val ? "Demo Hardware 2D/3D: ENABLED" : "Demo Hardware 2D/3D: DISABLED");
                break;
            }

            case MSG_CHECKBOX_AUDIO: {
                int32 val = 0;
                if (message->FindInt32("be:value", &val) == B_OK)
                    _SetStatus(val ? "Demo Audio Notifications: ENABLED" : "Demo Audio Notifications: DISABLED");
                break;
            }

            case MSG_CHECKBOX_DPI: {
                int32 val = 0;
                if (message->FindInt32("be:value", &val) == B_OK)
                    _SetStatus(val ? "Demo High-DPI Font Antialiasing: ON" : "Demo High-DPI Font Antialiasing: OFF");
                break;
            }

            case MSG_CHECKBOX_EVDEV: {
                int32 val = 0;
                if (message->FindInt32("be:value", &val) == B_OK)
                    _SetStatus(val ? "Demo Direct Evdev Stream: ACTIVE" : "Demo Direct Evdev Stream: SUSPENDED");
                break;
            }

            case MSG_CHECKBOX_CRASH: {
                int32 val = 0;
                if (message->FindInt32("be:value", &val) == B_OK)
                    _SetStatus(val ? "Demo Crashpad Handler: WATCHING" : "Demo Crashpad Handler: DISABLED");
                break;
            }

            case MSG_RADIO_ASYNC:
                _SetStatus("Demo Threading: Multi-threaded BLooper Messaging (Standard)");
                break;

            case MSG_RADIO_SYNC:
                _SetStatus("Demo Threading: Synchronous Direct Event Dispatch");
                break;

            case MSG_RADIO_POOL:
                _SetStatus("Demo Threading: Asynchronous Task Worker Pool");
                break;

            case MSG_MENU_RES: {
                const char* label = NULL;
                if (message->FindString("label", &label) == B_OK) {
                    BString s;
                    s.SetToFormat("Display Mode selected: %s", label);
                    _SetStatus(s.String());
                }
                break;
            }

            case MSG_MENU_GPU: {
                const char* label = NULL;
                if (message->FindString("label", &label) == B_OK) {
                    BString s;
                    s.SetToFormat("Renderer selected: %s", label);
                    _SetStatus(s.String());
                }
                break;
            }

            case MSG_MENU_AUDIO: {
                const char* label = NULL;
                if (message->FindString("label", &label) == B_OK) {
                    BString s;
                    s.SetToFormat("Audio Sink selected: %s", label);
                    _SetStatus(s.String());
                }
                break;
            }

            case MSG_MENU_COLOR: {
                const char* label = NULL;
                if (message->FindString("label", &label) == B_OK) {
                    BString s;
                    s.SetToFormat("Color Space selected: %s", label);
                    _SetStatus(s.String());
                }
                break;
            }

            case MSG_TEXT_USER:
                if (fUserText) {
                    BString s;
                    s.SetToFormat("Username updated: '%s'", fUserText->Text());
                    _SetStatus(s.String());
                }
                break;

            case MSG_TEXT_EMAIL:
                if (fEmailText) {
                    BString s;
                    s.SetToFormat("Email updated: '%s'", fEmailText->Text());
                    _SetStatus(s.String());
                }
                break;

            case MSG_TEXT_URL:
                if (fUrlText) {
                    BString s;
                    s.SetToFormat("Home URL updated: '%s'", fUrlText->Text());
                    _SetStatus(s.String());
                }
                break;

            case MSG_TEXT_PASS:
                _SetStatus("Password field modified (hidden).");
                break;

            case MSG_LIST_SELECTED: {
                if (fListView) {
                    int32 sel = fListView->CurrentSelection();
                    if (sel >= 0) {
                        BStringItem* item = dynamic_cast<BStringItem*>(fListView->ItemAt(sel));
                        if (item) {
                            BString s;
                            s.SetToFormat("Selected System Service: %s", item->Text());
                            _SetStatus(s.String());
                        }
                    }
                }
                break;
            }

            case MSG_SLIDER_VOL:
                if (fVolSlider) {
                    BString s;
                    s.SetToFormat("Volume: %d%%", (int)fVolSlider->Value());
                    _SetStatus(s.String());
                }
                break;

            case MSG_SLIDER_GAMMA:
                if (fGammaSlider) {
                    BString s;
                    s.SetToFormat("Display Gamma: %0.1f", fGammaSlider->Value() / 10.0f);
                    _SetStatus(s.String());
                }
                break;

            case MSG_SLIDER_BAL:
                if (fBalSlider) {
                    int32 v = fBalSlider->Value();
                    BString s;
                    if (v == 0)
                        s = "Stereo Balance: Center (0)";
                    else if (v < 0)
                        s.SetToFormat("Stereo Balance: Left (%d)", (int)-v);
                    else
                        s.SetToFormat("Stereo Balance: Right (%d)", (int)v);
                    _SetStatus(s.String());
                }
                break;

            case MSG_PROGRESS_STEP:
                if (fStatusBar1)
                    fStatusBar1->Update(10.0f, "Sample Phase", "Active");
                if (fStatusBar2)
                    fStatusBar2->Update(5.0f, "Building Kernel Drivers", "Compiling");
                _SetStatus("Progress incremented (+10%).");
                break;

            case MSG_PROGRESS_RESET:
                if (fStatusBar1)
                    fStatusBar1->Reset("Waiting for download…", "0 / 100 MB");
                if (fStatusBar2)
                    fStatusBar2->Reset("Compiler Idle", "0%");
                _SetStatus("Progress indicators reset.");
                break;

            case MSG_MENU_TAB0:
                if (fTabView) fTabView->Select(0);
                break;
            case MSG_MENU_TAB1:
                if (fTabView) fTabView->Select(1);
                break;
            case MSG_MENU_TAB2:
                if (fTabView) fTabView->Select(2);
                break;
            case MSG_MENU_TAB3:
                if (fTabView) fTabView->Select(3);
                break;

            default:
                BWindow::MessageReceived(message);
                break;
        }
    }

private:
    static void _GetSystemInfo(BString& kernelLine, BString& cpuLine,
        BString& screenLine)
    {
        struct utsname uts;
        if (uname(&uts) == 0)
            kernelLine.SetToFormat("Kernel: %s %s (%s)", uts.sysname,
                uts.release, uts.machine);
        else
            kernelLine = "Kernel: unknown";

        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        long pages = sysconf(_SC_PHYS_PAGES);
        long pageSize = sysconf(_SC_PAGESIZE);
        long long memMiB = (pages > 0 && pageSize > 0)
            ? (long long)pages * pageSize / (1024 * 1024) : 0;
        cpuLine.SetToFormat("CPUs online: %ld, RAM: %lld MiB", cpus, memMiB);

        screenLine = "Framebuffer: unknown";
        FILE* f = fopen("/sys/class/graphics/fb0/virtual_size", "r");
        if (f != NULL) {
            int w = 0, h = 0;
            if (fscanf(f, "%d,%d", &w, &h) == 2)
                screenLine.SetToFormat("Framebuffer: /dev/fb0 %dx%d", w, h);
            fclose(f);
        }
    }

    void _SetStatus(const char* text)
    {
        if (fStatusString)
            fStatusString->SetText(text);
        printf("[WidgetGallery] %s\n", text);
    }

    void _UpdateCounter()
    {
        if (fCounterString) {
            BString s;
            s.SetToFormat("Total User Events: %d", (int)fInteractionCount);
            fCounterString->SetText(s.String());
        }
    }

    void _ResetAll()
    {
        if (fVolSlider) fVolSlider->SetValue(65);
        if (fGammaSlider) fGammaSlider->SetValue(18);
        if (fBalSlider) fBalSlider->SetValue(0);
        if (fUserText) fUserText->SetText("haiku_developer");
        if (fEmailText) fEmailText->SetText("dev@haiku-os.org");
        if (fUrlText) fUrlText->SetText("https://www.yahoo.com");
        if (fStatusBar1) fStatusBar1->SetTo(75.0f, "Sample Phase", "75%");
        if (fStatusBar2) fStatusBar2->SetTo(92.0f, "Kernel Drivers", "92%");
        if (fListView) fListView->Select(0);
        _SetStatus("Demo controls reset. System Information reads live values.");
    }

    void _InitLayout()
    {
        printf("[WidgetGallery] _InitLayout start\n");
        BRect bounds = Bounds();

        // 1. Menu Bar
        printf("[WidgetGallery] Creating BMenuBar...\n");
        BRect menuBarRect = bounds;
        menuBarRect.bottom = 20;
        BMenuBar* menuBar = new BMenuBar(menuBarRect, "MenuBar");

        // Menu "File"
        BMenu* fileMenu = new BMenu("File");
        fileMenu->AddItem(new BMenuItem("Reset All Controls", new BMessage(MSG_BTN_RESET), 'R'));
        fileMenu->AddItem(new BMenuItem("Step Progress", new BMessage(MSG_PROGRESS_STEP), 'P'));
        fileMenu->AddSeparatorItem();
        fileMenu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
        menuBar->AddItem(fileMenu);

        // Menu "View" (Select Tabs)
        BMenu* viewMenu = new BMenu("View");
        viewMenu->AddItem(new BMenuItem("Controls & Toggles Tab", new BMessage(MSG_MENU_TAB0), '1'));
        viewMenu->AddItem(new BMenuItem("Text & Lists Tab", new BMessage(MSG_MENU_TAB1), '2'));
        viewMenu->AddItem(new BMenuItem("Sliders & Progress Tab", new BMessage(MSG_MENU_TAB2), '3'));
        viewMenu->AddItem(new BMenuItem("2D Vector Graphics Tab", new BMessage(MSG_MENU_TAB3), '4'));
        menuBar->AddItem(viewMenu);

        // Menu "Help"
        BMenu* helpMenu = new BMenu("Help");
        helpMenu->AddItem(new BMenuItem("About Haiku Widget Gallery…", new BMessage(MSG_BTN_ABOUT)));
        menuBar->AddItem(helpMenu);

        AddChild(menuBar);

        // 2. Tab View Container
        printf("[WidgetGallery] Creating BTabView...\n");
        BRect tabBounds = bounds;
        tabBounds.top = menuBarRect.bottom + 1.0f;
        tabBounds.bottom = bounds.bottom - 36.0f; // Leave room for footer status bar
        fTabView = new BTabView(tabBounds, "mainTabView", B_WIDTH_FROM_LABEL);
        fTabView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

        BRect contentBounds = fTabView->ContainerView()->Bounds();

        // =====================================================================
        // TAB 1: CONTROLS & TOGGLES
        // =====================================================================
        printf("[WidgetGallery] Building Tab 1 (Controls & Toggles)...\n");
        BView* tab1 = new BView(contentBounds, "Controls & Toggles (demo)", B_FOLLOW_ALL, B_WILL_DRAW);
        tab1->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

        // Box 1: Push Buttons (Left Top)
        BBox* boxButtons = new BBox(BRect(10, 10, 380, 130), "boxButtons");
        boxButtons->SetLabel("Push Buttons & Commands (BButton)");
        tab1->AddChild(boxButtons);

        BButton* btnPrimary = new BButton(BRect(12, 22, 120, 52), "btnPrimary",
                                          "Primary Action", new BMessage(MSG_BTN_PRIMARY));
        btnPrimary->MakeDefault(true);
        boxButtons->AddChild(btnPrimary);

        BButton* btnSecondary = new BButton(BRect(130, 22, 240, 52), "btnSecondary",
                                            "Standard Button", new BMessage(MSG_BTN_SECONDARY));
        boxButtons->AddChild(btnSecondary);

        BButton* btnDisabled = new BButton(BRect(250, 22, 355, 52), "btnDisabled",
                                           "Disabled Button", NULL);
        btnDisabled->SetEnabled(false);
        boxButtons->AddChild(btnDisabled);

        BButton* btnAbout = new BButton(BRect(12, 64, 180, 94), "btnAbout",
                                        "Show Modal Alert…", new BMessage(MSG_BTN_ABOUT));
        boxButtons->AddChild(btnAbout);

        BButton* btnReset = new BButton(BRect(190, 64, 355, 94), "btnReset",
                                        "Reset Defaults", new BMessage(MSG_BTN_RESET));
        boxButtons->AddChild(btnReset);

        // Box 2: Option Selectors (Left Bottom)
        BBox* boxMenus = new BBox(BRect(10, 140, 380, 310), "boxMenus");
        boxMenus->SetLabel("Option Selectors (BMenuField & BPopUpMenu)");
        tab1->AddChild(boxMenus);

        // MenuField 1: Display Mode
        BPopUpMenu* resMenu = new BPopUpMenu("resMenu");
        BMessage* m1 = new BMessage(MSG_MENU_RES); m1->AddString("label", "1280x800 WXGA");
        BMessage* m2 = new BMessage(MSG_MENU_RES); m2->AddString("label", "1024x768 XGA");
        BMessage* m3 = new BMessage(MSG_MENU_RES); m3->AddString("label", "1920x1080 FHD");
        resMenu->AddItem(new BMenuItem("1280x800 WXGA (Native Linux FB)", m1));
        resMenu->AddItem(new BMenuItem("1024x768 XGA (Standard 4:3)", m2));
        resMenu->AddItem(new BMenuItem("1920x1080 Full HD (1080p)", m3));
        resMenu->ItemAt(0)->SetMarked(true);

        BMenuField* mfRes = new BMenuField(BRect(12, 22, 355, 50), "mfRes",
                                           "Display Mode:", resMenu);
        mfRes->SetDivider(110.0f);
        boxMenus->AddChild(mfRes);

        // MenuField 2: Rendering Backend
        BPopUpMenu* gpuMenu = new BPopUpMenu("gpuMenu");
        BMessage* mg1 = new BMessage(MSG_MENU_GPU); mg1->AddString("label", "SwiftShader Vulkan (ANGLE)");
        BMessage* mg2 = new BMessage(MSG_MENU_GPU); mg2->AddString("label", "Haiku AGG 2D Software");
        BMessage* mg3 = new BMessage(MSG_MENU_GPU); mg3->AddString("label", "Direct Linux DRM/KMS");
        gpuMenu->AddItem(new BMenuItem("SwiftShader Vulkan (ANGLE)", mg1));
        gpuMenu->AddItem(new BMenuItem("Haiku AGG 2D Software", mg2));
        gpuMenu->AddItem(new BMenuItem("Direct Linux DRM/KMS", mg3));
        gpuMenu->ItemAt(0)->SetMarked(true);

        BMenuField* mfGPU = new BMenuField(BRect(12, 58, 355, 86), "mfGPU",
                                           "Demo GPU Backend:", gpuMenu);
        mfGPU->SetDivider(110.0f);
        boxMenus->AddChild(mfGPU);

        // MenuField 3: Audio Sink
        BPopUpMenu* audioMenu = new BPopUpMenu("audioMenu");
        BMessage* ma1 = new BMessage(MSG_MENU_AUDIO); ma1->AddString("label", "VirtIO Sound Device");
        BMessage* ma2 = new BMessage(MSG_MENU_AUDIO); ma2->AddString("label", "ALSA HW:0 Direct PCM");
        audioMenu->AddItem(new BMenuItem("VirtIO Sound Device (QEMU)", ma1));
        audioMenu->AddItem(new BMenuItem("ALSA HW:0 Direct PCM", ma2));
        audioMenu->ItemAt(0)->SetMarked(true);

        BMenuField* mfAudio = new BMenuField(BRect(12, 94, 355, 122), "mfAudio",
                                             "Demo Audio Sink:", audioMenu);
        mfAudio->SetDivider(110.0f);
        boxMenus->AddChild(mfAudio);

        // MenuField 4: Color Space
        BPopUpMenu* colorMenu = new BPopUpMenu("colorMenu");
        BMessage* mc1 = new BMessage(MSG_MENU_COLOR); mc1->AddString("label", "32-bit B_RGB32 TrueColor");
        BMessage* mc2 = new BMessage(MSG_MENU_COLOR); mc2->AddString("label", "16-bit B_RGB16 HighColor");
        colorMenu->AddItem(new BMenuItem("32-bit B_RGB32 TrueColor", mc1));
        colorMenu->AddItem(new BMenuItem("16-bit B_RGB16 HighColor", mc2));
        colorMenu->ItemAt(0)->SetMarked(true);

        BMenuField* mfColor = new BMenuField(BRect(12, 130, 355, 158), "mfColor",
                                             "Demo Depth:", colorMenu);
        mfColor->SetDivider(110.0f);
        boxMenus->AddChild(mfColor);

        // Box 3: CheckBoxes (Right Top)
        BBox* boxCheck = new BBox(BRect(395, 10, 775, 155), "boxCheck");
        boxCheck->SetLabel("Toggle Check Boxes (BCheckBox)");
        tab1->AddChild(boxCheck);

        BCheckBox* cb1 = new BCheckBox(BRect(12, 20, 360, 42), "cb1",
                                       "Demo: Hardware 2D/3D Acceleration", new BMessage(MSG_CHECKBOX_HW));
        cb1->SetValue(B_CONTROL_ON);
        boxCheck->AddChild(cb1);

        BCheckBox* cb2 = new BCheckBox(BRect(12, 46, 360, 68), "cb2",
                                       "Demo: Audio Notifications", new BMessage(MSG_CHECKBOX_AUDIO));
        cb2->SetValue(B_CONTROL_ON);
        boxCheck->AddChild(cb2);

        BCheckBox* cb3 = new BCheckBox(BRect(12, 72, 360, 94), "cb3",
                                       "Demo: Subpixel Antialiasing", new BMessage(MSG_CHECKBOX_DPI));
        boxCheck->AddChild(cb3);

        BCheckBox* cb4 = new BCheckBox(BRect(12, 98, 360, 120), "cb4",
                                       "Demo: Evdev Event Stream", new BMessage(MSG_CHECKBOX_EVDEV));
        cb4->SetValue(B_CONTROL_ON);
        boxCheck->AddChild(cb4);

        // Box 4: Radio Buttons (Right Bottom)
        BBox* boxRadio = new BBox(BRect(395, 165, 775, 310), "boxRadio");
        boxRadio->SetLabel("Demo Options (BRadioButton)");
        tab1->AddChild(boxRadio);

        BRadioButton* rb1 = new BRadioButton(BRect(12, 22, 360, 44), "rb1",
                                             "Multi-threaded Looper Execution (BMessage)", new BMessage(MSG_RADIO_ASYNC));
        rb1->SetValue(B_CONTROL_ON);
        boxRadio->AddChild(rb1);

        BRadioButton* rb2 = new BRadioButton(BRect(12, 50, 360, 72), "rb2",
                                             "Synchronous In-Process Dispatch", new BMessage(MSG_RADIO_SYNC));
        boxRadio->AddChild(rb2);

        BRadioButton* rb3 = new BRadioButton(BRect(12, 78, 360, 100), "rb3",
                                             "Asynchronous Task Worker Pool", new BMessage(MSG_RADIO_POOL));
        boxRadio->AddChild(rb3);

        BSeparatorView* sep = new BSeparatorView("sep1", (const char*)NULL, B_HORIZONTAL);
        sep->MoveTo(12, 110);
        sep->ResizeTo(348, 10);
        boxRadio->AddChild(sep);

        fTabView->AddTab(tab1);

        // =====================================================================
        // TAB 2: TEXT & LISTS
        // =====================================================================
        printf("[WidgetGallery] Building Tab 2 (Text & Lists)...\n");
        BView* tab2 = new BView(contentBounds, "Text & Lists", B_FOLLOW_ALL, B_WILL_DRAW);
        tab2->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

        // Box: Text Fields
        BBox* boxText = new BBox(BRect(10, 10, 380, 160), "boxText");
        boxText->SetLabel("Single-Line Text Fields (BTextControl)");
        tab2->AddChild(boxText);

        fUserText = new BTextControl(BRect(12, 22, 355, 48), "fUserText",
                                     "Username:", "haiku_developer", new BMessage(MSG_TEXT_USER));
        fUserText->SetDivider(90.0f);
        boxText->AddChild(fUserText);

        fEmailText = new BTextControl(BRect(12, 54, 355, 80), "fEmailText",
                                      "Email:", "dev@haiku-os.org", new BMessage(MSG_TEXT_EMAIL));
        fEmailText->SetDivider(90.0f);
        boxText->AddChild(fEmailText);

        fUrlText = new BTextControl(BRect(12, 86, 355, 112), "fUrlText",
                                    "Home URL:", "https://www.yahoo.com", new BMessage(MSG_TEXT_URL));
        fUrlText->SetDivider(90.0f);
        boxText->AddChild(fUrlText);

        fPassText = new BTextControl(BRect(12, 118, 355, 144), "fPassText",
                                     "Password:", "haiku2026", new BMessage(MSG_TEXT_PASS));
        fPassText->SetDivider(90.0f);
        if (fPassText->TextView())
            fPassText->TextView()->HideTyping(true);
        boxText->AddChild(fPassText);

        // Box: Multi-line Editor
        BBox* boxEditor = new BBox(BRect(10, 170, 380, 310), "boxEditor");
        boxEditor->SetLabel("Multi-Line Text Editor (BTextView & BScrollView)");
        tab2->AddChild(boxEditor);

        BRect textFrame(12, 20, 340, 125);
        BRect textBounds = textFrame;
        textBounds.OffsetTo(B_ORIGIN);
        textBounds.InsetBy(4, 4);

        BTextView* textView = new BTextView(textFrame, "editor", textBounds,
                                            B_FOLLOW_LEFT_TOP, B_WILL_DRAW);
        textView->SetText(
            "Welcome to the full-blown Haiku GUI on Linux!\n\n"
            "This rich widget gallery demonstrates Haiku's native\n"
            "C++ looper/handler multithreading model. All controls\n"
            "are rendered by app_server and communicate via ports.\n"
            "You can click, select, and edit this text freely.");

        BScrollView* scrollEditor = new BScrollView("scrollEditor", textView,
                                                    B_FOLLOW_LEFT_TOP, 0, false, true, B_FANCY_BORDER);
        boxEditor->AddChild(scrollEditor);

        // Box: Scrollable List View
        BBox* boxList = new BBox(BRect(395, 10, 775, 310), "boxList");
        boxList->SetLabel("Scrollable List View (BListView & BStringItem)");
        tab2->AddChild(boxList);

        BRect listFrame(12, 22, 350, 280);
        fListView = new BListView(listFrame, "servicesList", B_SINGLE_SELECTION_LIST);
        fListView->AddItem(new BStringItem("app_server (GUI Display Server)"));
        fListView->AddItem(new BStringItem("Deskbar (System Taskbar & Tray)"));
        fListView->AddItem(new BStringItem("Terminal (PTY Terminal Emulator)"));
        fListView->AddItem(new BStringItem("Chromium (Ozone Web Engine)"));
        fListView->AddItem(new BStringItem("WidgetGallery (Interface Kit Showcase)"));
        fListView->AddItem(new BStringItem("Linux Kernel 6.12 (64-bit Core)"));
        fListView->AddItem(new BStringItem("libroot.so (Clean-Room Compatibility)"));
        fListView->AddItem(new BStringItem("libbe.so (Interface, App, Storage Kits)"));
        fListView->AddItem(new BStringItem("SwiftShader (CPU Vulkan Rasterizer)"));
        fListView->AddItem(new BStringItem("Evdev Stream (/dev/input/event*)"));
        fListView->AddItem(new BStringItem("Framebuffer (/dev/fb0 1280x800)"));
        fListView->SetSelectionMessage(new BMessage(MSG_LIST_SELECTED));
        fListView->Select(0);

        BScrollView* scrollList = new BScrollView("scrollList", fListView,
                                                  B_FOLLOW_LEFT_TOP, 0, false, true, B_FANCY_BORDER);
        boxList->AddChild(scrollList);

        fTabView->AddTab(tab2);

        // =====================================================================
        // TAB 3: SLIDERS & PROGRESS
        // =====================================================================
        printf("[WidgetGallery] Building Tab 3 (Sliders & Progress)...\n");
        BView* tab3 = new BView(contentBounds, "Sliders & Progress", B_FOLLOW_ALL, B_WILL_DRAW);
        tab3->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

        // Box: Continuous Sliders
        BBox* boxSliders = new BBox(BRect(10, 10, 380, 225), "boxSliders");
        boxSliders->SetLabel("Demo Sliders (BSlider with Live Labels)");
        tab3->AddChild(boxSliders);

        // Slider 1: Volume
        fVolSlider = new GallerySlider(BRect(12, 20, 355, 70), "fVolSlider",
                                       "Master Audio Volume", new BMessage(MSG_SLIDER_VOL),
                                       0, 100, GallerySlider::STYLE_PERCENT);
        fVolSlider->SetLimitLabels("0%", "100%");
        fVolSlider->SetHashMarks(B_HASH_MARKS_BOTH);
        fVolSlider->SetHashMarkCount(11);
        fVolSlider->SetValue(65);
        boxSliders->AddChild(fVolSlider);

        // Slider 2: Gamma
        fGammaSlider = new GallerySlider(BRect(12, 80, 355, 130), "fGammaSlider",
                                         "Display Gamma Calibration", new BMessage(MSG_SLIDER_GAMMA),
                                         0, 30, GallerySlider::STYLE_FLOAT_TENTHS);
        fGammaSlider->SetLimitLabels("0.0", "3.0");
        fGammaSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
        fGammaSlider->SetHashMarkCount(16);
        fGammaSlider->SetValue(18);
        boxSliders->AddChild(fGammaSlider);

        // Slider 3: Balance
        fBalSlider = new GallerySlider(BRect(12, 140, 355, 190), "fBalSlider",
                                       "Stereo Channel Balance", new BMessage(MSG_SLIDER_BAL),
                                       -50, 50, GallerySlider::STYLE_BALANCE);
        fBalSlider->SetLimitLabels("Left", "Right");
        fBalSlider->SetHashMarks(B_HASH_MARKS_TOP);
        fBalSlider->SetHashMarkCount(11);
        fBalSlider->SetValue(0);
        boxSliders->AddChild(fBalSlider);

        // Box: Standalone Scroll Bars
        BBox* boxScrollBars = new BBox(BRect(10, 235, 380, 310), "boxScrollBars");
        boxScrollBars->SetLabel("Standalone Scroll Bars (BScrollBar)");
        tab3->AddChild(boxScrollBars);

        BScrollBar* hsb = new BScrollBar(BRect(12, 25, 355, 41), "hsb", NULL, 0, 100, B_HORIZONTAL);
        hsb->SetProportion(0.35f);
        hsb->SetValue(30);
        boxScrollBars->AddChild(hsb);

        BStringView* sbNote = new BStringView(BRect(12, 48, 355, 68), "sbNote",
                                              "Direct BScrollBar controls for custom canvas panning");
        boxScrollBars->AddChild(sbNote);

        // Box: Progress Indicators
        BBox* boxProgress = new BBox(BRect(395, 10, 775, 195), "boxProgress");
        boxProgress->SetLabel("Demo Progress (BStatusBar)");
        tab3->AddChild(boxProgress);

        fStatusBar1 = new BStatusBar(BRect(12, 22, 360, 72), "fStatusBar1",
                                     "Demo Task A:", "100%");
        fStatusBar1->SetMaxValue(100.0f);
        fStatusBar1->SetTo(75.0f, "Sample Phase", "75%");
        boxProgress->AddChild(fStatusBar1);

        fStatusBar2 = new BStatusBar(BRect(12, 82, 360, 132), "fStatusBar2",
                                     "Demo Task B:", "100%");
        fStatusBar2->SetMaxValue(100.0f);
        fStatusBar2->SetTo(92.0f, "Sample Phase", "92%");
        boxProgress->AddChild(fStatusBar2);

        BButton* btnStep = new BButton(BRect(12, 145, 180, 175), "btnStep",
                                       "Step Progress (+10%)", new BMessage(MSG_PROGRESS_STEP));
        boxProgress->AddChild(btnStep);

        BButton* btnResetP = new BButton(BRect(190, 145, 360, 175), "btnResetP",
                                         "Reset Progress", new BMessage(MSG_PROGRESS_RESET));
        boxProgress->AddChild(btnResetP);

        // Box: System information, read at runtime from the running system.
        BBox* boxInfo = new BBox(BRect(395, 205, 775, 310), "boxInfo");
        boxInfo->SetLabel("System Information (live)");
        tab3->AddChild(boxInfo);

        BString kernelLine, cpuLine, screenLine;
        _GetSystemInfo(kernelLine, cpuLine, screenLine);

        BStringView* m1Label = new BStringView(BRect(12, 22, 360, 42), "m1",
                                               kernelLine.String());
        boxInfo->AddChild(m1Label);

        BStringView* m2Label = new BStringView(BRect(12, 46, 360, 66), "m2",
                                               cpuLine.String());
        boxInfo->AddChild(m2Label);

        BStringView* m3Label = new BStringView(BRect(12, 70, 360, 90), "m3",
                                               screenLine.String());
        boxInfo->AddChild(m3Label);

        fTabView->AddTab(tab3);

        // =====================================================================
        // TAB 4: 2D VECTOR GRAPHICS
        // =====================================================================
        printf("[WidgetGallery] Building Tab 4 (2D Vector Graphics)...\n");
        BView* tab4 = new BView(contentBounds, "2D Vector Graphics", B_FOLLOW_ALL, B_WILL_DRAW);
        tab4->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

        GalleryCanvasView* canvas = new GalleryCanvasView(contentBounds);
        tab4->AddChild(canvas);

        fTabView->AddTab(tab4);

        // Add TabView to Window
        AddChild(fTabView);

        // 3. Persistent Bottom Footer (Status & Event Counter)
        BRect footerRect(10, bounds.bottom - 32.0f, bounds.right - 10, bounds.bottom - 6.0f);
        fStatusString = new BStringView(BRect(footerRect.left, footerRect.top, footerRect.right - 220, footerRect.bottom),
                                        "fStatusString", "Demo controls; System Information reads live values");
        fStatusString->SetFont(be_bold_font);
        AddChild(fStatusString);

        fCounterString = new BStringView(BRect(footerRect.right - 210, footerRect.top, footerRect.right, footerRect.bottom),
                                         "fCounterString", "Total User Events: 0");
        fCounterString->SetAlignment(B_ALIGN_RIGHT);
        AddChild(fCounterString);

        printf("[WidgetGallery] _InitLayout complete!\n");
    }

    int32           fInteractionCount;
    BTabView*       fTabView;
    BStringView*    fStatusString;
    BStringView*    fCounterString;
    BStatusBar*     fStatusBar1;
    BStatusBar*     fStatusBar2;
    GallerySlider*  fVolSlider;
    GallerySlider*  fGammaSlider;
    GallerySlider*  fBalSlider;
    BTextControl*   fUserText;
    BTextControl*   fEmailText;
    BTextControl*   fUrlText;
    BTextControl*   fPassText;
    BListView*      fListView;
};

// Application Class
class GalleryApp : public BApplication {
public:
    GalleryApp()
        : BApplication(kAppSignature)
    {
    }

    virtual void ReadyToRun() override
    {
        // Place Widget Gallery cleanly on the left side of the 1280x800 desktop
        BRect frame(25, 45, 845, 665);
        GalleryWindow* win = new GalleryWindow(frame);
        win->Show();
        printf("[WidgetGallery] Full-blown Gallery window launched on Haiku GUI (820x620)\n");
    }
};

int main(int argc, char** argv)
{
    printf("[WidgetGallery] Initializing Full-Blown Haiku GUI Widget Gallery...\n");
    if (be_control_look == NULL) {
        printf("[WidgetGallery] Initializing HaikuControlLook fallback...\n");
        be_control_look = new BPrivate::HaikuControlLook();
    }
    GalleryApp app;
    app.Run();
    printf("[WidgetGallery] Exiting.\n");
    return 0;
}
