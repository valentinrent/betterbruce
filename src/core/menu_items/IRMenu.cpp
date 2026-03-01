#include "IRMenu.h"
#include "core/display.h"
#include "core/settings.h"
#include "core/utils.h"
#include "modules/ir/TV-B-Gone.h"
#include "modules/ir/custom_ir.h"
#include "modules/ir/ir_jammer.h"
#include "modules/ir/ir_read.h"

void IRMenu::optionsMenu() {
    int _loop_selected = 0;
    while (true) {
        if (returnToMenu) {
            returnToMenu = false;
            return;
        }

#if defined(ARDUINO_M5STICK_S3)
    bool prevPower = M5.Power.getExtOutput();
    M5.Power.setExtOutput(true); // ENABLE 5V OUTPUT
#endif
    options = {
        {"TV-B-Gone", StartTvBGone              },
        {"Custom IR", otherIRcodes              },
        {"IR Read",   [=]() { IrRead(); }       },
#if !defined(LITE_VERSION)
        {"IR Jammer", startIrJammer             }, // Simple frequency-adjustable jammer
#endif
        {"Config",    [this]() { configMenu(); }},
    };
    addOptionToMainMenu();

    String txt = "Infrared";
    txt += " Tx: " + String(bruceConfigPins.irTx) + " Rx: " + String(bruceConfigPins.irRx) +
           " Rpts: " + String(bruceConfigPins.irTxRepeats);
    _loop_selected = loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str(), _loop_selected);
#if defined(ARDUINO_M5STICK_S3)
    M5.Power.setExtOutput(prevPower);
#endif
        if (_loop_selected == -1 || _loop_selected == options.size() - 1) return;
    }
}

void IRMenu::configMenu() {
    int _loop_selected = 0;
    while (true) {
        if (returnToMenu) {
            return;
        }
    options = {
        {"Ir TX Pin", lambdaHelper(gsetIrTxPin, true)},
        {"Ir RX Pin", lambdaHelper(gsetIrRxPin, true)},
        {"Ir TX Repeats", setIrTxRepeats},
        {"Back", []() {} },
    };

    _loop_selected = loopOptions(options, MENU_TYPE_SUBMENU, "IR Config", _loop_selected);
    if (_loop_selected == -1 || _loop_selected == options.size() - 1) return;
    }
}

void IRMenu::drawIcon(float scale) {
    clearIconArea();
    int iconSize = scale * 60;
    int radius = scale * 7;
    int deltaRadius = scale * 10;

    if (iconSize % 2 != 0) iconSize++;

    tft.fillRect(
        iconCenterX - iconSize / 2, iconCenterY - iconSize / 2, iconSize / 6, iconSize, bruceConfig.priColor
    );
    tft.fillRect(
        iconCenterX - iconSize / 3,
        iconCenterY - iconSize / 3,
        iconSize / 6,
        2 * iconSize / 3,
        bruceConfig.priColor
    );

    tft.drawCircle(iconCenterX - iconSize / 6, iconCenterY, radius, bruceConfig.priColor);

    tft.drawArc(
        iconCenterX - iconSize / 6,
        iconCenterY,
        2.5 * radius,
        2 * radius,
        220,
        320,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
    tft.drawArc(
        iconCenterX - iconSize / 6,
        iconCenterY,
        2.5 * radius + deltaRadius,
        2 * radius + deltaRadius,
        220,
        320,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
    tft.drawArc(
        iconCenterX - iconSize / 6,
        iconCenterY,
        2.5 * radius + 2 * deltaRadius,
        2 * radius + 2 * deltaRadius,
        220,
        320,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
}
