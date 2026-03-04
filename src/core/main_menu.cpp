#include "main_menu.h"
#include "display.h"
#include "utils.h"
#include <globals.h>

MainMenu::MainMenu() {
    _menuItems = {
        &wifiMenu,
        &bleMenu,
#if !defined(LITE_VERSION)
        &ethernetMenu,
#endif
        &rfMenu,
        &rfidMenu,
        &irMenu,
#if defined(FM_SI4713) && !defined(LITE_VERSION)
        &fmMenu,
#endif
        &fileMenu,
        &gpsMenu,
        &nrf24Menu,
#if !defined(LITE_VERSION)
#if !defined(DISABLE_INTERPRETER)
        &scriptsMenu,
#endif
        &loraMenu,
#endif
        &othersMenu,
#if !defined(LITE_VERSION)
        &connectMenu,
#endif
        &configMenu,
    };

    _totalItems = _menuItems.size();
}

MainMenu::~MainMenu() {}

void MainMenu::begin(void) {
    returnToMenu = false;
    options = {};

    std::vector<String> l = bruceConfig.disabledMenus;
    for (int i = 0; i < _totalItems; i++) {
        String itemName = _menuItems[i]->getName();
        if (find(l.begin(), l.end(), itemName) == l.end()) { // If menu item is not disabled
            options.push_back(
                {// selected lambda
                 _menuItems[i]->getName(),
                 [this, i]() { _menuItems[i]->optionsMenu(); },
                 false,                                  // selected = false
                 [](void *menuItem, bool shouldRender) { // render lambda
                     if (!shouldRender) return false;
                     drawMainBorder(false);

                     MenuItemInterface *obj = static_cast<MenuItemInterface *>(menuItem);
                     float scale = float((float)tftWidth / (float)240);
                     if (bruceConfigPins.rotation & 0b01) scale = float((float)tftHeight / (float)135);
                     obj->draw(scale);

                     // Draw horizontal scrollbar for active apps
                     int total_apps = options.size();
                     if (total_apps > 0) {
                         int current_index = 0;
                         for (int j = 0; j < total_apps; ++j) {
                             if (options[j].hovered) { current_index = j; break; }
                         }

                         int barAreaW = tftWidth - 24; // left and right margin
                         int barX = 12;
                         int barY = tftHeight - 14;
                         int currentW = barAreaW / total_apps;
                         int startX = barX + (barAreaW * current_index) / total_apps;
                         if (current_index == total_apps - 1) {
                             currentW = barAreaW - (startX - barX); // consume remaining cleanly
                         }

                         tft.fillRect(barX, barY - 1, barAreaW, 4, bruceConfig.bgColor); // clear track area safely
                         tft.fillRect(startX, barY, currentW, 2, bruceConfig.priColor); // highlight active chunk
                     }

#if defined(HAS_TOUCH)
                     TouchFooter();
#endif
                     return true;
                 },
                 _menuItems[i]
                }
            );
        }
    }
    _currentIndex = loopOptions(options, MENU_TYPE_MAIN, "Main Menu", _currentIndex);
};

/*********************************************************************
**  Function: hideAppsMenu
**  Menu to Hide or show menus
**********************************************************************/

void MainMenu::hideAppsMenu() {
    auto items = this->getItems();
    int _loop_selected = 0;
    while(true) {
        if (returnToMenu) {
            returnToMenu = false;
            return;
        }
        options.clear();
        for (auto item : items) {
            String label = item->getName();
            std::vector<String> l = bruceConfig.disabledMenus;
            bool enabled = find(l.begin(), l.end(), label) == l.end();
            options.push_back({label, [this, label, enabled]() {
                if (enabled) {
                    bruceConfig.addDisabledMenu(label);
                } else {
                    auto it = find(bruceConfig.disabledMenus.begin(), bruceConfig.disabledMenus.end(), label);
                    if (it != bruceConfig.disabledMenus.end()) {
                        bruceConfig.disabledMenus.erase(it);
                    }
                }
            }, !enabled}); // !enabled means disabled apps get `selcolor` (un-highlighted), active apps get `fgcolor` (highlighted)
        }
        options.push_back({"Show All", [=]() { bruceConfig.disabledMenus.clear(); }, false});
        addOptionToMainMenu();
        _loop_selected = loopOptions(options, MENU_TYPE_REGULAR, "", _loop_selected);
        bruceConfig.saveFile();
        if (_loop_selected == -1 || _loop_selected == options.size() - 1) return;
    }
}
