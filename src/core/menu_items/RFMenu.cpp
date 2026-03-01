#include "RFMenu.h"
#include <globals.h>
#include "core/display.h"
#include "core/settings.h"
#include "core/utils.h"
#include "modules/rf/record.h"
#include "modules/rf/rf_analyzer.h"
#include "modules/rf/rf_bruteforce.h"
#include "modules/rf/rf_jammer.h"
#include "modules/rf/rf_scan.h"
#include "modules/rf/rf_send.h"
#include "modules/rf/rf_spectrum.h"
#include "modules/rf/rf_waterfall.h"

void RFMenu::optionsMenu() {
    int selected = 0;
    returnToMenu = false;

    while (true) {
        if (returnToMenu) {
            returnToMenu = false;
            return;
        }

        options = {
            {"Scan/Copy",       [=]() { RFScan(); }       },
#if !defined(LITE_VERSION)
            {"Rec RAW",      rf_raw_record             }, // Pablo-Ortiz-Lopez
            {"Custom RF",   sendCustomRF              },
#endif
            {"Spectrum",        rf_spectrum               },
#if !defined(LITE_VERSION)
            {"RSSI Spec",   rf_CC1101_rssi            }, // @Pirata
            {"SquareWave Spec", rf_SquareWave             }, // @Pirata
            {"Spectogram",      rf_waterfall              }, // dev_eclipse
            {"Analyzer",        rf_analyzer               },
            {"Bruteforce",      rf_bruteforce             }, // dev_eclipse
            {"Jammer Itmt",     [=]() { RFJammer(false); }},
#endif
            {"Jammer Full",     [=]() { RFJammer(true); } },
            {"Config",          [this]() { configMenu(); }},
        };
        addOptionToMainMenu();

        String txt = "RF";
        if (bruceConfigPins.rfModule == CC1101_SPI_MODULE) txt += " (CC1101)"; // Indicates if CC1101 is connected
        else txt += " Tx: " + String(bruceConfigPins.rfTx) + " Rx: " + String(bruceConfigPins.rfRx);

        selected = loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str(), selected);

        if (selected == -1 || selected == options.size() - 1) { return; }
    }
}

void RFMenu::configMenu() {
    int selected = 0;
    while (true) {
        if (returnToMenu) {
            // we don't reset returnToMenu here because we want it to cascade back up to optionsMenu
            return;
        }

        options = {
            {"RF TX Pin", lambdaHelper(gsetRfTxPin, true)},
            {"RF RX Pin", lambdaHelper(gsetRfRxPin, true)},
            {"RF Module", setRFModuleMenu},
            {"RF Frequency", setRFFreqMenu},
            {"Back", []() {}},
        };

        selected = loopOptions(options, MENU_TYPE_SUBMENU, "RF Config", selected);

        if (selected == -1 || selected == options.size() - 1) { return; }
    }
}

void RFMenu::drawIcon(float scale) {
    clearIconArea();
    int radius = scale * 7;
    int deltaRadius = scale * 10;
    int triangleSize = scale * 30;

    if (triangleSize % 2 != 0) triangleSize++;

    // Body
    tft.fillCircle(iconCenterX, iconCenterY - radius, radius, bruceConfig.priColor);
    tft.fillTriangle(
        iconCenterX,
        iconCenterY,
        iconCenterX - triangleSize / 2,
        iconCenterY + triangleSize,
        iconCenterX + triangleSize / 2,
        iconCenterY + triangleSize,
        bruceConfig.priColor
    );

    // Left Arcs
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius,
        2 * radius,
        40,
        140,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius + deltaRadius,
        2 * radius + deltaRadius,
        40,
        140,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius + 2 * deltaRadius,
        2 * radius + 2 * deltaRadius,
        40,
        140,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );

    // Right Arcs
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius,
        2 * radius,
        220,
        320,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius + deltaRadius,
        2 * radius + deltaRadius,
        220,
        320,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius + 2 * deltaRadius,
        2 * radius + 2 * deltaRadius,
        220,
        320,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
}
