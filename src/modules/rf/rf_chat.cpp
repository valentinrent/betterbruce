#include "rf_chat.h"
#include "core/display.h"
#include "core/utils.h"
#include "rf_utils.h"
#include <globals.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include <ArduinoJson.h>

#include "core/configPins.h"
#include "modules/rfid/RFIDInterface.h"
#include "modules/rfid/RFID2.h"
#include "modules/rfid/PN532.h"

void load_rf_chat_config(float &freqHz, std::vector<uint8_t> &SHARED_KEY, String &chatUsername, String &modulation, String &encryptionType, String &passwordStr) {
    FS *fs;
    if (!getFsStorage(fs)) return;
    File file = fs->open("/rf_chat.json", FILE_READ);
    if (!file) return;

    JsonDocument doc;
    if (!deserializeJson(doc, file)) {
        if (doc["freqHz"].is<float>()) freqHz = doc["freqHz"].as<float>();
        if (doc["chatUsername"].is<String>()) chatUsername = doc["chatUsername"].as<String>();
        if (doc["modulation"].is<String>()) modulation = doc["modulation"].as<String>();
        if (doc["encryptionType"].is<String>()) encryptionType = doc["encryptionType"].as<String>();
        if (doc["passwordStr"].is<String>()) passwordStr = doc["passwordStr"].as<String>();
        if (doc["sharedKey"].is<String>()) {
            String keyStr = doc["sharedKey"].as<String>();
            if (keyStr.length() % 2 == 0 && keyStr.length() > 0) {
                SHARED_KEY.clear();
                for (int i=0; i<keyStr.length(); i+=2) {
                    SHARED_KEY.push_back((uint8_t)strtol(keyStr.substring(i, i+2).c_str(), NULL, 16));
                }
            }
        }
    }
    file.close();
}

void save_rf_chat_config(const float &freqHz, const std::vector<uint8_t> &SHARED_KEY, const String &chatUsername, const String &modulation, const String &encryptionType, const String &passwordStr) {
    FS *fs;
    if (!getFsStorage(fs)) return;
    File file = fs->open("/rf_chat.json", FILE_WRITE);
    if (!file) return;

    JsonDocument doc;
    doc["freqHz"] = freqHz;
    doc["chatUsername"] = chatUsername;
    doc["modulation"] = modulation;
    doc["encryptionType"] = encryptionType;
    doc["passwordStr"] = passwordStr;
    String keyStr = "";
    for(uint8_t b : SHARED_KEY) {
        char buf[3];
        sprintf(buf, "%02X", b);
        keyStr += buf;
    }
    doc["sharedKey"] = keyStr;
    serializeJson(doc, file);
    file.close();
}

std::vector<uint8_t> scan_nfc_key() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(5, 5);
    tft.print("Scan NFC Tag");
    tft.setCursor(5, 30);
    tft.setTextSize(1);
    tft.print("Place tag on reader...");

    std::vector<uint8_t> r_uid;

    RFIDInterface* rfidPtr = nullptr;
    if (bruceConfigPins.rfidModule == M5_RFID2_MODULE) {
        rfidPtr = new RFID2(true);
    } else {
        rfidPtr = new PN532();
    }

    if (rfidPtr && rfidPtr->begin()) {
        uint32_t startAt = millis();
        while (millis() - startAt < 5000) {
            int ret = rfidPtr->read();
            if (ret == RFIDInterface::SUCCESS && rfidPtr->uid.size > 0) {
                for (int i=0; i<rfidPtr->uid.size; i++) {
                    r_uid.push_back(rfidPtr->uid.uidByte[i]);
                }
                break;
            }
            if (check(EscPress)) break;
            delay(100);
        }
    } else {
        delay(1000);
    }
    if (rfidPtr) delete rfidPtr;

    if (r_uid.empty()) {
        tft.setCursor(5, 50);
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        tft.print("Read Failed / Timeout");
        delay(1500);
    } else {
        tft.setCursor(5, 50);
        tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
        tft.print("Tag Read OK");
        delay(1000);
    }
    return r_uid;
}

void rf_chat() {
    static float freqHz = 433.92;
    static std::vector<uint8_t> SHARED_KEY = {0x42, 0x13, 0x37, 0xAA, 0x55};
    static String chatUsername = "Anon";
    static String modulation = "2-FSK";
    static String encryptionType = "None"; // None, Hex Key, Password, Read NFC
    static String passwordStr = "secret";
    static bool config_loaded = false;

    if (!config_loaded) {
        load_rf_chat_config(freqHz, SHARED_KEY, chatUsername, modulation, encryptionType, passwordStr);
        config_loaded = true;
    }

    bool exit_module = false;
    int mainMenuIdx = 0;

    auto getEncryptionKey = [&]() -> std::vector<uint8_t> {
        if (encryptionType == "None") return {};
        if (encryptionType == "Hex Key") return SHARED_KEY;
        if (encryptionType == "Password") {
            std::vector<uint8_t> pwKey;
            for (char c : passwordStr) pwKey.push_back((uint8_t)c);
            return pwKey;
        }
        if (encryptionType == "Read NFC") return SHARED_KEY;
        return {};
    };

    auto applyCustomCC1101Config = [&]() {
        if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) return;
        ELECHOUSE_cc1101.setSidle();

        // 1. Core Params
        if (modulation == "ASK/OOK") ELECHOUSE_cc1101.setModulation(2);
        else if (modulation == "2-FSK") ELECHOUSE_cc1101.setModulation(0);
        else if (modulation == "GFSK") ELECHOUSE_cc1101.setModulation(1);
        else if (modulation == "4-FSK") ELECHOUSE_cc1101.setModulation(3);
        else if (modulation == "MSK") ELECHOUSE_cc1101.setModulation(4);

        ELECHOUSE_cc1101.setPktFormat(0);    // Normal FIFO Mode
        ELECHOUSE_cc1101.setLengthConfig(1); // Variable Packet Length
        ELECHOUSE_cc1101.setPacketLength(61); // HARDWARE CAP
        ELECHOUSE_cc1101.setSyncMode(2);      // 16/16 sync word bits detected
        ELECHOUSE_cc1101.setSyncWord(0x46, 0x4C); // 'FL' Sync

        // 2. Exact Flipper Zero RF Tuning (~100kHz BW, ~10kbaud, 23.8kHz Dev)
        ELECHOUSE_cc1101.setRxBW(101.56);
        ELECHOUSE_cc1101.setDRate(9.99);
        ELECHOUSE_cc1101.setDeviation(23.8);

        // 3. Hardware verification parameters
        ELECHOUSE_cc1101.setCrc(true);      // Require CRC trailing bytes
        ELECHOUSE_cc1101.setCRC_AF(true);   // Hardware auto-flush bad CRCs
    };


    while (!exit_module) {
        std::vector<String> mainOpts = {
            "Enter Chat",
            "Configs",
            "Exit"
        };

        tft.fillScreen(bruceConfig.bgColor);
        tft.setTextSize(FM);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.setCursor(5, 5);
        tft.print("Sub-G Chat");

        auto drawMainView = [&]() {
            for(int i=0; i<mainOpts.size(); i++) {
                if (i == mainMenuIdx) {
                    tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);
                } else {
                    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
                }
                tft.setCursor(10, 30 + i * 20);
                tft.print(mainOpts[i]);
            }
        };

        drawMainView();

        bool in_menu = true;
        while(in_menu) {
            if (check(EscPress)) {
                exit_module = true;
                in_menu = false;
            } else if (check(PrevPress)) {
                mainMenuIdx--;
                if (mainMenuIdx < 0) mainMenuIdx = mainOpts.size() - 1;
                drawMainView();
                delay(100);
            } else if (check(NextPress)) {
                mainMenuIdx++;
                if (mainMenuIdx >= mainOpts.size()) mainMenuIdx = 0;
                drawMainView();
                delay(100);
            } else if (check(SelPress)) {
                in_menu = false;

                if (mainMenuIdx == 0) { // Enter Chat
                    bool exit_chat = false;

                    std::vector<String> messagesList = {
                        "Hello!",
                        "Yes",
                        "No",
                        "Still there?",
                        "Where are u?",
                        "OK",
                        "Danger!",
                        "Safe",
                        "[Custom Msg]"
                    };
                    int selMessageIdx = 0;

                    struct ChatMessage {
                        bool me;
                        String text;
                    };
                    std::vector<ChatMessage> chatLog;

                    if (!initRfModule("rx", freqHz)) {
                        displayError("failed init rf rx", true);
                        break;
                    }

                    if (bruceConfigPins.rfModule == CC1101_SPI_MODULE) {
                        applyCustomCC1101Config();
                        ELECHOUSE_cc1101.SetRx();
                    }

                    auto drawChatView = [&]() {
                        tft.fillScreen(bruceConfig.bgColor);

                        tft.setTextSize(1);
                        tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
                        tft.setCursor(5, 5);
                        tft.print("Sub-G (");
                        tft.print(freqHz, 2);
                        tft.print(")");

                        tft.setCursor(tft.width() - 75, 5);
                        tft.print("[LISTENING]");

                        int y = 20;
                        int startIdx = std::max(0, (int)chatLog.size() - 6);
                        for(int i = startIdx; i < chatLog.size(); i++) {
                            tft.setCursor(5, y);
                            if (chatLog[i].me) {
                                tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
                                tft.print("Me: " + chatLog[i].text);
                            } else {
                                tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
                                tft.print(chatLog[i].text);
                            }
                            y += 15;
                        }

                        tft.fillRect(0, tft.height() - 40, tft.width(), 40, TFT_DARKGREY);
                        tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
                        tft.setCursor(5, tft.height() - 35);
                        tft.print("Send:");

                        tft.setTextColor(getColorVariation(bruceConfig.priColor), TFT_DARKGREY);
                        tft.setCursor(10, tft.height() - 20);
                        tft.print("> " + messagesList[selMessageIdx]);
                    };

                    auto buildPacketBytes = [&](String msgStr) -> std::vector<uint8_t> {
                        String fullMsg = chatUsername + ": " + msgStr;
                        std::vector<uint8_t> pt;
                        for(char c : fullMsg) pt.push_back((uint8_t)c);

                        std::vector<uint8_t> ct;
                        std::vector<uint8_t> key = getEncryptionKey();

                        if (key.empty()) {
                            ct = pt;
                        } else {
                            for(size_t i = 0; i < pt.size(); i++) {
                                ct.push_back(pt[i] ^ key[i % key.size()]);
                            }
                        }

                        return ct;
                    };

                    drawChatView();

                    while (!exit_chat) {
                        if (check(EscPress)) {
                            exit_chat = true;
                        } else if (check(PrevPress)) {
                            selMessageIdx--;
                            if (selMessageIdx < 0) selMessageIdx = messagesList.size() - 1;
                            drawChatView();
                            delay(100);
                        } else if (check(NextPress)) {
                            selMessageIdx++;
                            if (selMessageIdx >= messagesList.size()) selMessageIdx = 0;
                            drawChatView();
                            delay(100);
                        } else if (check(SelPress)) {
                            if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
                                displayWarning("Requires CC1101");
                                delay(1000);
                                drawChatView();
                                continue;
                            }

                            String msgStr = messagesList[selMessageIdx];

                            if (msgStr == "[Custom Msg]") {
                                String custom = keyboard("", 20, "Type message:");
                                if (custom != "" && custom != "\x1B") {
                                    msgStr = custom;
                                } else {
                                    drawChatView();
                                    continue;
                                }
                            }

                            std::vector<uint8_t> packetData = buildPacketBytes(msgStr);

                            deinitRfModule();
                            initRfModule("tx", freqHz);

                            applyCustomCC1101Config();
                            ELECHOUSE_cc1101.setPA(12);

                            tft.setTextColor(TFT_RED, TFT_DARKGREY);
                            tft.setCursor(tft.width() - 30, tft.height() - 35);
                            tft.print("TX...");
                            delay(50);

                            ELECHOUSE_cc1101.SendData(packetData.data(), packetData.size(), 100);
                            delay(50);

                            chatLog.push_back({true, msgStr});
                            if (chatLog.size() > 10) chatLog.erase(chatLog.begin());

                            deinitRfModule();
                            initRfModule("rx", freqHz);

                            applyCustomCC1101Config();
                            ELECHOUSE_cc1101.SetRx();

                            drawChatView();
                            delay(100);

                        }

                        if (bruceConfigPins.rfModule == CC1101_SPI_MODULE) {
                            if (ELECHOUSE_cc1101.CheckRxFifo(100)) {
                                // Since setCRC_AF(true) is enabled, any packet present here MUST have a good CRC!
                                uint8_t rxBuffer[256]; // Oversized intentionally to prevent variables length overflow attacks
                                int len = ELECHOUSE_cc1101.ReceiveData(rxBuffer);

                                if (len > 0 && len <= 62) {
                                    std::vector<uint8_t> payload;
                                    for (int p = 0; p < len; p++) {
                                        payload.push_back(rxBuffer[p]);
                                    }

                                    String decoded = "";
                                    std::vector<uint8_t> key = getEncryptionKey();

                                    for (size_t i = 0; i < payload.size(); i++) {
                                        uint8_t dec = key.empty() ? payload[i] : (payload[i] ^ key[i % key.size()]);
                                        if (dec >= 32 && dec <= 126) decoded += (char)dec;
                                    }

                                    if (decoded != "") {
                                        chatLog.push_back({false, decoded});
                                        if (chatLog.size() > 10) chatLog.erase(chatLog.begin());
                                        drawChatView();
                                    }
                                }
                            }
                        }

                        delay(10);
                    }

                    deinitRfModule();
                    tft.fillScreen(bruceConfig.bgColor);

                } else if (mainMenuIdx == 1) { // Configs
                    bool in_config = true;
                    int configIdx = 0;

                    while (in_config) {
                        std::vector<String> configOpts;
                        configOpts.push_back("Freq: " + String(freqHz, 2) + " MHz");
                        configOpts.push_back("Modulation: " + modulation);
                        configOpts.push_back("Encryption: " + encryptionType);
                        int encSetIdx = -1;
                        if (encryptionType != "None") {
                            configOpts.push_back("Set " + encryptionType);
                            encSetIdx = configOpts.size() - 1;
                        }
                        int userIdx = configOpts.size();
                        configOpts.push_back("Username: " + chatUsername);
                        int backIdx = configOpts.size();
                        configOpts.push_back("Back");

                        tft.fillScreen(bruceConfig.bgColor);
                        tft.setTextSize(FM);
                        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
                        tft.setCursor(5, 5);
                        tft.print("Chat Configs");

                        for(int i=0; i<configOpts.size(); i++) {
                            if (i == configIdx) {
                                tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);
                            } else {
                                tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
                            }
                            tft.setCursor(10, 30 + i * 20);
                            tft.print(configOpts[i]);
                        }

                        if (encryptionType == "Hex Key" && configIdx == encSetIdx) {
                            tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
                            tft.setCursor(10, 30 + configOpts.size() * 20);
                            String kt = "";
                            for(auto b: SHARED_KEY) { char buf[3]; sprintf(buf,"%02X",b); kt+=buf; }
                            tft.print("Key: " + kt);
                        } else if (encryptionType == "Password" && configIdx == encSetIdx) {
                            tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
                            tft.setCursor(10, 30 + configOpts.size() * 20);
                            tft.print("Pass: " + passwordStr);
                        }

                        while (true) {
                            if (check(EscPress)) {
                                in_config = false;
                                break;
                            } else if (check(PrevPress)) {
                                configIdx--;
                                if (configIdx < 0) configIdx = configOpts.size() - 1;
                                delay(100);
                                break;
                            } else if (check(NextPress)) {
                                configIdx++;
                                if (configIdx >= configOpts.size()) configIdx = 0;
                                delay(100);
                                break;
                            } else if (check(SelPress)) {
                                if (configIdx == 0) {
                                    if (freqHz == 315.0f) freqHz = 433.92f;
                                    else if (freqHz == 433.92f) freqHz = 868.0f;
                                    else freqHz = 315.0f;
                                    save_rf_chat_config(freqHz, SHARED_KEY, chatUsername, modulation, encryptionType, passwordStr);
                                } else if (configIdx == 1) {
                                    if (modulation == "ASK/OOK") modulation = "2-FSK";
                                    else if (modulation == "2-FSK") modulation = "GFSK";
                                    else if (modulation == "GFSK") modulation = "4-FSK";
                                    else if (modulation == "4-FSK") modulation = "MSK";
                                    else modulation = "ASK/OOK";
                                    save_rf_chat_config(freqHz, SHARED_KEY, chatUsername, modulation, encryptionType, passwordStr);
                                } else if (configIdx == 2) {
                                    if (encryptionType == "None") encryptionType = "Hex Key";
                                    else if (encryptionType == "Hex Key") encryptionType = "Password";
                                    else if (encryptionType == "Password") encryptionType = "Read NFC";
                                    else encryptionType = "None";
                                    save_rf_chat_config(freqHz, SHARED_KEY, chatUsername, modulation, encryptionType, passwordStr);
                                } else if (configIdx == encSetIdx) {
                                    if (encryptionType == "Hex Key") {
                                        String newKeyBase = "";
                                        for(int i=0; i<SHARED_KEY.size(); i++) {
                                            char b[3];
                                            sprintf(b, "%02X", SHARED_KEY[i]);
                                            newKeyBase += String(b);
                                        }
                                        String newKeyStr = hex_keyboard(newKeyBase, 20, "Hex Key (even len):");
                                        if (newKeyStr != "" && newKeyStr != "\x1B") {
                                            if (newKeyStr.length() % 2 == 0) {
                                                SHARED_KEY.clear();
                                                for(int i=0; i<newKeyStr.length(); i+=2) {
                                                    String byteStr = newKeyStr.substring(i, i+2);
                                                    SHARED_KEY.push_back((uint8_t)strtol(byteStr.c_str(), NULL, 16));
                                                }
                                                if (SHARED_KEY.empty()) {
                                                    SHARED_KEY = {0x42, 0x13, 0x37, 0xAA, 0x55};
                                                }
                                            }
                                        }
                                    } else if (encryptionType == "Password") {
                                        String p = keyboard(passwordStr, 20, "Password:");
                                        if (p != "" && p != "\x1B") {
                                            passwordStr = p;
                                        }
                                    } else if (encryptionType == "Read NFC") {
                                        std::vector<uint8_t> rKey = scan_nfc_key();
                                        if (!rKey.empty()) {
                                            SHARED_KEY = rKey;
                                        } else {
                                            encryptionType = "None";
                                        }
                                    }
                                    save_rf_chat_config(freqHz, SHARED_KEY, chatUsername, modulation, encryptionType, passwordStr);
                                } else if (configIdx == userIdx) {
                                    String newName = keyboard(chatUsername, 10, "Username:");
                                    if (newName != "" && newName != "\x1B") {
                                        chatUsername = newName;
                                        save_rf_chat_config(freqHz, SHARED_KEY, chatUsername, modulation, encryptionType, passwordStr);
                                    }
                                } else if (configIdx == backIdx) {
                                    in_config = false;
                                }
                                delay(100);
                                break;
                            }
                            delay(10);
                        }
                    }
                } else if (mainMenuIdx == 2) {
                    exit_module = true;
                }
            }
            delay(10);
        }
    }
}
