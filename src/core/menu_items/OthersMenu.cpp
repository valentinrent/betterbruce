#include "OthersMenu.h"

#include <globals.h>
#include "core/display.h"
#include "core/utils.h"
#include "modules/badusb_ble/ducky_typer.h"
#include "modules/bjs_interpreter/interpreter.h"
#include "modules/others/clicker.h"
#include "modules/others/ibutton.h"
#include "modules/others/mic.h"
#include "modules/others/qrcode_menu.h"
#include "modules/others/tururururu.h"
// Removed: #include "modules/others/timer.h"

void OthersMenu::optionsMenu() {
    int _loop_selected = 0;
    while (true) {
        if (returnToMenu) {
            returnToMenu = false;
            return;
        }
    options = {
        {"QRCodes",      qrcode_menu                  },
        {"Megalodon",    shark_setup                  },

#if defined(MIC_SPM1423) || defined(MIC_INMP441)
        {"Microphone",   [this]() { micMenu(); }      }, //@deveclipse
#endif

// New consolidated BadUSB & HID submenu
#if !defined(LITE_VERSION) || defined(USB_as_HID)
        {"BadUSB & HID", [this]() { badUsbHidMenu(); }},
#endif

#ifndef LITE_VERSION
        {"iButton",      setup_ibutton                },
#endif

        // Timer removed - moved to another "Clock"
    };

    addOptionToMainMenu();
    _loop_selected = loopOptions(options, MENU_TYPE_SUBMENU, "Others", _loop_selected);
    if (_loop_selected == -1 || _loop_selected == options.size() - 1) return;
    }
}

void OthersMenu::badUsbHidMenu() {
    int _loop_selected = 0;
    while (true) {
        if (returnToMenu) return;
    options = {
#ifndef LITE_VERSION
        {"BadUSB",       [=]() { ducky_setup(hid_usb, false); }   },
        {"USB Keyboard", [=]() { ducky_keyboard(hid_usb, false); }},
#endif

#ifdef USB_as_HID
        {"USB Clicker",  clicker_setup                            },
#endif

        {"Back",         []() {}                                  },
    };

    _loop_selected = loopOptions(options, MENU_TYPE_SUBMENU, "BadUSB & HID", _loop_selected);
    if (_loop_selected == -1 || _loop_selected == options.size() - 1) return;
    }
}

void OthersMenu::micMenu() {
    int _loop_selected = 0;
    while (true) {
        if (returnToMenu) return;
    options = {
#if defined(MIC_SPM1423) || defined(MIC_INMP441)
        {"Spectrum", mic_test                   },
        {"Record",   mic_record_app             },
#endif
        {"Back",     []() {}              },
    };

    _loop_selected = loopOptions(options, MENU_TYPE_SUBMENU, "Microphone", _loop_selected);
    if (_loop_selected == -1 || _loop_selected == options.size() - 1) return;
    }
}

void OthersMenu::drawIcon(float scale) {
    clearIconArea();

    // Dynamic radius calculation based on scale for responsive rendering
    int radius = scale * 7;

    // Center circle
    tft.fillCircle(iconCenterX, iconCenterY, radius, bruceConfig.priColor);

    // Concentric arcs - dynamically scaled for different screen sizes
    tft.drawArc(
        iconCenterX, iconCenterY, 2.5 * radius, 2 * radius, 0, 340, bruceConfig.priColor, bruceConfig.bgColor
    );

    tft.drawArc(
        iconCenterX, iconCenterY, 3.5 * radius, 3 * radius, 20, 360, bruceConfig.priColor, bruceConfig.bgColor
    );

    tft.drawArc(
        iconCenterX, iconCenterY, 4.5 * radius, 4 * radius, 0, 200, bruceConfig.priColor, bruceConfig.bgColor
    );

    tft.drawArc(
        iconCenterX,
        iconCenterY,
        4.5 * radius,
        4 * radius,
        240,
        360,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
}
