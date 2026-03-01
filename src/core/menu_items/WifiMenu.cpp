#include "WifiMenu.h"
#include "core/display.h"
#include "core/settings.h"
#include "core/utils.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wg.h"
#include "core/wifi/wifi_common.h"
#include "core/wifi/wifi_mac.h"
#include "modules/ethernet/ARPScanner.h"
#include "modules/wifi/ap_info.h"
#include "modules/wifi/clients.h"
#include "modules/wifi/evil_portal.h"
#include "modules/wifi/karma_attack.h"
#include "modules/wifi/responder.h"
#include "modules/wifi/scan_hosts.h"
#include "modules/wifi/sniffer.h"
#include "modules/wifi/wifi_atks.h"



#ifndef LITE_VERSION
#include "modules/wifi/wifi_recover.h"
#include "modules/pwnagotchi/pwnagotchi.h"
#endif

// #include "modules/reverseShell/reverseShell.h"
//  Developed by Fourier (github.com/9dl)
//  Use BruceC2 to interact with the reverse shell server
//  BruceC2: https://github.com/9dl/Bruce-C2
//  To use BruceC2:
//  1. Start Reverse Shell Mode in Bruce
//  2. Start BruceC2 and wait.
//  3. Visit 192.168.4.1 in your browser to access the web interface for shell executing.

// 32bit: https://github.com/9dl/Bruce-C2/releases/download/v1.0/BruceC2_windows_386.exe
// 64bit: https://github.com/9dl/Bruce-C2/releases/download/v1.0/BruceC2_windows_amd64.exe
#include "modules/wifi/tcp_utils.h"

// global toggle - controls whether scanNetworks includes hidden SSIDs
bool showHiddenNetworks = false;

void WifiMenu::optionsMenu() {
    int _loop_selected = 0;
    while (true) {
        if (returnToMenu) {
            returnToMenu = false;
            return;
        }
        returnToMenu = false;
    options.clear();
    if (isWebUIActive) {
        drawMainBorderWithTitle("WiFi", true);
        padprintln("");
        padprintln("Starting a Wifi function will probably make the WebUI stop working");
        padprintln("");
        padprintln("Sel: to continue");
        padprintln("Any key: to Menu");
        while (1) {
            if (check(SelPress)) { break; }
            if (check(AnyKeyPress)) { return; }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
    if (WiFi.status() != WL_CONNECTED) {
        options = {
            {"Connect to Wifi", lambdaHelper(wifiConnectMenu, WIFI_STA)},
            {"Start WiFi AP", [=]() {
                 wifiConnectMenu(WIFI_AP);
                 displayInfo("pwd: " + bruceConfig.wifiAp.pwd, true);
             }},
        };
    }
    if (WiFi.getMode() != WIFI_MODE_NULL) { options.push_back({"WiFi Off", wifiDisconnect}); }
    if (WiFi.getMode() == WIFI_MODE_STA || WiFi.getMode() == WIFI_MODE_APSTA) {
        options.push_back({"AP info", displayAPInfo});
    }
    options.push_back({"Wifi Atks", wifi_atk_menu});
    options.push_back({"Evil Portal", [=]() {
                           if (isWebUIActive || server) {
                               stopWebUi();
                               wifiDisconnect();
                           }
                           EvilPortal();
                       }});
    // options.push_back({"ReverseShell", [=]()       { ReverseShell(); }});
#ifndef LITE_VERSION
    options.push_back({"Listen TCP", listenTcpPort});
    options.push_back({"Client TCP", clientTCP});
    options.push_back({"TelNET", telnet_setup});
    options.push_back({"SSH", lambdaHelper(ssh_setup, String(""))});
    options.push_back({"Sniffers", [this]() {
                           std::vector<Option> snifferOptions;
                           snifferOptions.push_back({"Raw Sniffer", sniffer_setup});
                           snifferOptions.push_back({"Probe Sniffer", karma_setup});
                           snifferOptions.push_back({"Back", []() {} });

                           int _sniff_selected = loopOptions(snifferOptions, MENU_TYPE_SUBMENU, "Sniffers");
                           if (_sniff_selected == -1 || _sniff_selected == snifferOptions.size() - 1) return;
                       }});
    options.push_back({"Scan Hosts", [=]() {
                           bool doScan = true;
                           if (!wifiConnected) doScan = wifiConnectMenu();

                           if (doScan) {
                               esp_netif_t *esp_netinterface =
                                   esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                               if (esp_netinterface == nullptr) {
                                   Serial.println("Failed to get netif handle");
                                   return;
                               }
                               ARPScanner{esp_netinterface};
                           }
                       }});
    options.push_back({"Wireguard", wg_setup});
    options.push_back({"Responder", responder});
    options.push_back({"Brucegotchi", brucegotchi_start});
    options.push_back({"WiFi Recov", wifi_recover_menu});
#endif

    options.push_back({"Config", [this]() { configMenu(); }});

    addOptionToMainMenu();

    _loop_selected = loopOptions(options, MENU_TYPE_SUBMENU, "WiFi", _loop_selected);

    options.clear();
    if (_loop_selected == -1 || _loop_selected == options.size() - 1) return;
    }
}

void WifiMenu::configMenu() {
    int _loop_selected = 0;
    while (true) {
        if (returnToMenu) {
            return;
        }
        std::vector<Option> wifiOptions;

    wifiOptions.push_back({"Change MAC", wifiMACMenu});
    wifiOptions.push_back({"Add Evil Wifi", addEvilWifiMenu});
    wifiOptions.push_back({"Remove Evil Wifi", removeEvilWifiMenu});

    // Evil Wifi Settings submenu (unchanged)
    wifiOptions.push_back({"Evil Wifi Settings", [this]() {
                               std::vector<Option> evilOptions;

                               evilOptions.push_back({"Password Mode", setEvilPasswordMode});
                               evilOptions.push_back({"Rename /creds", setEvilEndpointCreds});
                               evilOptions.push_back({"Allow /creds access", setEvilAllowGetCreds});
                               evilOptions.push_back({"Rename /ssid", setEvilEndpointSsid});
                               evilOptions.push_back({"Allow /ssid access", setEvilAllowSetSsid});
                               evilOptions.push_back({"Display endpoints", setEvilAllowEndpointDisplay});
                               evilOptions.push_back({"Back", []() {} });
                               int _evil_selected = loopOptions(evilOptions, MENU_TYPE_SUBMENU, "Evil Wifi Settings");
                               if (_evil_selected == -1 || _evil_selected == evilOptions.size() - 1) return;
                           }});

    {

        String hidden__wifi_option = String("Hidden Networks:") + (showHiddenNetworks ? "ON" : "OFF");

        // construct Option explicitly using char* label
        Option opt(hidden__wifi_option.c_str(), [this]() {
            showHiddenNetworks = !showHiddenNetworks;
            displayInfo(String("Hidden Networks:") + (showHiddenNetworks ? "ON" : "OFF"), true);
            // removing configMenu(); here because it will re-loop automatically
        });

        wifiOptions.push_back(opt);
    }
    wifiOptions.push_back({"Back", []() {} });
    _loop_selected = loopOptions(wifiOptions, MENU_TYPE_SUBMENU, "WiFi Config", _loop_selected);
    if (_loop_selected == -1 || _loop_selected == wifiOptions.size() - 1) return;
    }
}

void WifiMenu::drawIcon(float scale) {
    clearIconArea();
    int deltaY = scale * 20;
    int radius = scale * 6;

    tft.fillCircle(iconCenterX, iconCenterY + deltaY, radius, bruceConfig.priColor);
    tft.drawArc(
        iconCenterX,
        iconCenterY + deltaY,
        deltaY + radius,
        deltaY,
        130,
        230,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY + deltaY,
        2 * deltaY + radius,
        2 * deltaY,
        130,
        230,
        bruceConfig.priColor,
        bruceConfig.bgColor
    );
}
