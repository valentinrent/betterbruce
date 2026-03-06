/**
 * @file PN532.cpp
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Read and Write RFID tags using PN532 module
 * @version 0.1
 * @date 2024-08-19
 */

#include "PN532.h"
#include "core/display.h"
#include "core/i2c_finder.h"
#include "core/sd_functions.h"
#include "core/type_convertion.h"

#ifndef GPIO_NUM_25
#define GPIO_NUM_25 25
#endif

PN532::PN532(CONNECTION_TYPE connection_type) {
    _connection_type = connection_type;
    _use_i2c = (connection_type == I2C || connection_type == I2C_SPI);
    if (connection_type == CONNECTION_TYPE::I2C)
        nfc.setInterface(bruceConfigPins.i2c_bus.sda, bruceConfigPins.i2c_bus.scl);
#ifdef M5STICK
    else if (connection_type == CONNECTION_TYPE::I2C_SPI) nfc.setInterface(GPIO_NUM_26, GPIO_NUM_25);
#endif
    else nfc.setInterface(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_SS_PIN);
}

bool PN532::begin() {
#ifdef M5STICK
    if (_connection_type == CONNECTION_TYPE::I2C_SPI) {
        Wire.begin(GPIO_NUM_26, GPIO_NUM_25);
    } else if (_connection_type == CONNECTION_TYPE::I2C) {
        Wire.begin(bruceConfigPins.i2c_bus.sda, bruceConfigPins.i2c_bus.scl);
    }
#else
    Wire.begin(bruceConfigPins.i2c_bus.sda, bruceConfigPins.i2c_bus.scl);
#endif

    bool i2c_check = true;
    if (_use_i2c) {
        Wire.beginTransmission(PN532_I2C_ADDRESS);
        int error = Wire.endTransmission();
        i2c_check = (error == 0);
    }

    nfc.begin();

    load_dictionary();

    uint32_t versiondata = nfc.getFirmwareVersion();

    return i2c_check || versiondata;
}

void PN532::load_dictionary() {
    FS *fs;
    if (!getFsStorage(fs)) return;

    // Clear old keys if reloading
    for (uint8_t* k : dictionaryKeys) { free(k); }
    dictionaryKeys.clear();

    if ((*fs).exists("/BruceRFID/mf_classic_dict.nfc")) {
        File file = (*fs).open("/BruceRFID/mf_classic_dict.nfc", FILE_READ);
        if (!file) return;

        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() == 12) { // 6 bytes written in hex = 12 chars
                uint8_t* key = (uint8_t*)malloc(6);
                for (size_t i = 0; i < 12; i += 2) {
                    key[i / 2] = strtoul(line.substring(i, i + 2).c_str(), NULL, 16);
                }
                dictionaryKeys.push_back(key);
            }
        }
        file.close();
    }
}

int PN532::read(int cardBaudRate) {
    pageReadStatus = FAILURE;

    bool felica = false;
    if (cardBaudRate == PN532_MIFARE_ISO14443A) {
        if (!nfc.startPassiveTargetIDDetection(cardBaudRate)) return TAG_NOT_PRESENT;
        if (!nfc.readDetectedPassiveTargetID()) return FAILURE;
        format_data();
        set_uid();
    } else {
        uint16_t sys_code = 0xFFFF; // Default sys code for FeliCa
        uint8_t req_code = 0x01;    // Default request code for FeliCa
        uint8_t idm[8];
        uint8_t pmm[8];
        uint16_t sys_code_res;
        if (!nfc.felica_Polling(sys_code, req_code, idm, pmm, &sys_code_res)) { return TAG_NOT_PRESENT; }
        format_data_felica(idm, pmm, sys_code_res);
    }

    displayInfo("Reading data blocks...");
    pageReadStatus = read_data_blocks();
    pageReadSuccess = pageReadStatus == SUCCESS;

    // Always try parsing even for partial reads
    parsedCardData = "";
    if (dataPages > 0) {
        pageReadSuccess = true;
        pageReadStatus = SUCCESS;
        parse_known_card_formats();
    }

    return SUCCESS;
}

int PN532::clone() {
    if (!nfc.startPassiveTargetIDDetection()) return TAG_NOT_PRESENT;
    if (!nfc.readDetectedPassiveTargetID()) return FAILURE;

    if (nfc.targetUid.sak != uid.sak) return TAG_NOT_MATCH;

    uint8_t data[16];
    byte bcc = 0;
    int i;
    for (i = 0; i < uid.size; i++) {
        data[i] = uid.uidByte[i];
        bcc = bcc ^ uid.uidByte[i];
    }
    data[i++] = bcc;
    data[i++] = uid.sak;
    data[i++] = uid.atqaByte[1];
    data[i++] = uid.atqaByte[0];
    byte tmp = 0;
    while (i < 16) data[i++] = 0x62 + tmp++;
    if (nfc.mifareclassic_WriteBlock0(data)) {
        return SUCCESS;
    } else {
        // Backdoor failed, try direct write
        uint8_t num = 0;
        while ((!nfc.startPassiveTargetIDDetection() || !nfc.readDetectedPassiveTargetID()) && num++ < 5) {
            displayTextLine("hold on...");
            delay(10);
        }
        uid.size = nfc.targetUid.size;
        for (uint8_t i = 0; i < uid.size; i++) uid.uidByte[i] = nfc.targetUid.uidByte[i];

        if (authenticate_mifare_classic(0) == SUCCESS && nfc.mifareclassic_WriteDataBlock(0, data)) {
            return SUCCESS;
        }
    }
    return FAILURE;
}

int PN532::clone_full() {
    // Step 1: Write block 0 (UID / SAK / ATQA) — identical to clone()
    if (!nfc.startPassiveTargetIDDetection()) return TAG_NOT_PRESENT;
    if (!nfc.readDetectedPassiveTargetID()) return FAILURE;

    if (nfc.targetUid.sak != uid.sak) return TAG_NOT_MATCH;

    uint8_t block0[16];
    byte bcc = 0;
    int i;
    for (i = 0; i < uid.size; i++) {
        block0[i] = uid.uidByte[i];
        bcc = bcc ^ uid.uidByte[i];
    }
    block0[i++] = bcc;
    block0[i++] = uid.sak;
    block0[i++] = uid.atqaByte[1];
    block0[i++] = uid.atqaByte[0];
    byte tmp = 0;
    while (i < 16) block0[i++] = 0x62 + tmp++;

    bool block0Written = false;
    if (nfc.mifareclassic_WriteBlock0(block0)) {
        block0Written = true;
    } else {
        // Backdoor failed — try direct authenticated write
        uint8_t num = 0;
        while ((!nfc.startPassiveTargetIDDetection() || !nfc.readDetectedPassiveTargetID()) && num++ < 5) {
            displayTextLine("hold on...");
            delay(10);
        }
        uid.size = nfc.targetUid.size;
        for (uint8_t i = 0; i < uid.size; i++) uid.uidByte[i] = nfc.targetUid.uidByte[i];

        if (authenticate_mifare_classic(0) == SUCCESS && nfc.mifareclassic_WriteDataBlock(0, block0)) {
            block0Written = true;
        }
    }

    if (!block0Written) return FAILURE;

    // Step 2: Re-detect card, then write all remaining blocks including trailers
    uint8_t num = 0;
    while ((!nfc.startPassiveTargetIDDetection() || !nfc.readDetectedPassiveTargetID()) && num++ < 5) {
        displayTextLine("Waiting for card...");
        delay(50);
    }

    displayInfo("Writing all blocks...");
    return write_data_blocks_full();
}

int PN532::erase() {
    if (!nfc.startPassiveTargetIDDetection()) return TAG_NOT_PRESENT;
    if (!nfc.readDetectedPassiveTargetID()) return FAILURE;

    return erase_data_blocks();
}

int PN532::write(int cardBaudRate) {
    if (cardBaudRate == PN532_MIFARE_ISO14443A) {
        if (!nfc.startPassiveTargetIDDetection()) return TAG_NOT_PRESENT;
        if (!nfc.readDetectedPassiveTargetID()) return FAILURE;

        if (nfc.targetUid.sak != uid.sak) return TAG_NOT_MATCH;
    } else {
        uint16_t sys_code = 0xFFFF; // Default sys code for FeliCa
        uint8_t req_code = 0x01;    // Default request code for FeliCa
        uint8_t idm[8];
        uint8_t pmm[8];
        uint16_t sys_code_res;
        if (!nfc.felica_Polling(sys_code, req_code, idm, pmm, &sys_code_res)) { return TAG_NOT_PRESENT; }
    }

    return write_data_blocks();
}

int PN532::write_ndef() {
    if (!nfc.startPassiveTargetIDDetection()) return TAG_NOT_PRESENT;
    if (!nfc.readDetectedPassiveTargetID()) return FAILURE;

    return write_ndef_blocks();
}

int PN532::load() {
    String filepath;
    File file;
    FS *fs;

    if (!getFsStorage(fs)) return FAILURE;
    filepath = loopSD(*fs, true, "RFID|NFC", "/BruceRFID");
    file = fs->open(filepath, FILE_READ);

    if (!file) { return FAILURE; }

    String line;
    String strData;
    strAllPages = "";
    pageReadSuccess = true;

    while (file.available()) {
        line = file.readStringUntil('\n');
        strData = line.substring(line.indexOf(":") + 1);
        strData.trim();
        if (line.startsWith("Device type:")) printableUID.picc_type = strData;
        if (line.startsWith("UID:")) printableUID.uid = strData;
        if (line.startsWith("SAK:")) printableUID.sak = strData;
        if (line.startsWith("ATQA:")) printableUID.atqa = strData;
        if (line.startsWith("Pages total:")) dataPages = strData.toInt();
        if (line.startsWith("Pages read:")) pageReadSuccess = false;
        if (line.startsWith("Page ")) strAllPages += line + "\n";
    }

    file.close();
    delay(100);
    parse_data();

    return SUCCESS;
}

int PN532::save(String filename) {
    FS *fs;
    if (!getFsStorage(fs)) return FAILURE;

    File file = createNewFile(fs, "/BruceRFID", filename + ".rfid");

    if (!file) { return FAILURE; }

    file.println("Filetype: Bruce RFID File");
    file.println("Version 1");
    file.println("Device type: " + printableUID.picc_type);
    file.println("# UID, ATQA and SAK are common for all formats");
    file.println("UID: " + printableUID.uid);
    if (printableUID.picc_type != "FeliCa") {
        file.println("SAK: " + printableUID.sak);
        file.println("ATQA: " + printableUID.atqa);
        file.println("# Memory dump");
        file.println("Pages total: " + String(dataPages));
        if (!pageReadSuccess) file.println("Pages read: " + String(dataPages));
    } else {
        file.println("Manufacture id: " + printableUID.sak);
        file.println("Blocks total: " + String(totalPages));
        file.println("Blocks read: " + String(dataPages));
    }
    file.print(strAllPages);

    file.close();
    delay(100);
    return SUCCESS;
}

String PN532::get_tag_type() {
    String tag_type = nfc.PICC_GetTypeName(nfc.targetUid.sak);

    if (nfc.targetUid.sak == PICC_TYPE_MIFARE_UL) {
        switch (totalPages) {
            case 45: tag_type = "NTAG213"; break;
            case 135: tag_type = "NTAG215"; break;
            case 231: tag_type = "NTAG216"; break;
            default: break;
        }
    }

    return tag_type;
}

void PN532::set_uid() {
    uid.sak = nfc.targetUid.sak;
    uid.size = nfc.targetUid.size;

    for (byte i = 0; i < 2; i++) uid.atqaByte[i] = nfc.targetUid.atqaByte[i];

    for (byte i = 0; i < nfc.targetUid.size; i++) { uid.uidByte[i] = nfc.targetUid.uidByte[i]; }
}

void PN532::format_data() {
    byte bcc = 0;

    printableUID.picc_type = get_tag_type();

    printableUID.sak = nfc.targetUid.sak < 0x10 ? "0" : "";
    printableUID.sak += String(nfc.targetUid.sak, HEX);
    printableUID.sak.toUpperCase();

    // UID
    for (byte i = 0; i < nfc.targetUid.size; i++) { bcc = bcc ^ nfc.targetUid.uidByte[i]; }
    printableUID.uid = hexToStr(nfc.targetUid.uidByte, nfc.targetUid.size);

    // BCC
    printableUID.bcc = bcc < 0x10 ? "0" : "";
    printableUID.bcc += String(bcc, HEX);
    printableUID.bcc.toUpperCase();

    // ATQA
    printableUID.atqa = hexToStr(nfc.targetUid.atqaByte, 2);
}

void PN532::format_data_felica(uint8_t idm[8], uint8_t pmm[8], uint16_t sys_code) {
    // Reuse uid-sak-atqa to save memory
    printableUID.picc_type = "FeliCa";
    printableUID.uid = hexToStr(idm, 8);
    printableUID.sak = hexToStr(pmm, 8);
    printableUID.atqa = String(sys_code, HEX);

    memcpy(uid.uidByte, idm, 8);
}

void PN532::parse_data() {
    String strUID = printableUID.uid;
    strUID.trim();
    strUID.replace(" ", "");
    uid.size = strUID.length() / 2;
    for (size_t i = 0; i < strUID.length(); i += 2) {
        uid.uidByte[i / 2] = strtoul(strUID.substring(i, i + 2).c_str(), NULL, 16);
    }

    printableUID.sak.trim();
    uid.sak = strtoul(printableUID.sak.c_str(), NULL, 16);

    String strAtqa = printableUID.atqa;
    strAtqa.trim();
    strAtqa.replace(" ", "");
    for (size_t i = 0; i < strAtqa.length(); i += 2) {
        uid.atqaByte[i / 2] = strtoul(strAtqa.substring(i, i + 2).c_str(), NULL, 16);
    }
}

int PN532::read_data_blocks() {
    dataPages = 0;
    totalPages = 0;
    int readStatus = FAILURE;

    strAllPages = "";

    if (printableUID.picc_type != "FeliCa") {
        switch (uid.sak) {
            case PICC_TYPE_MIFARE_MINI:
            case PICC_TYPE_MIFARE_1K:
            case PICC_TYPE_MIFARE_4K: readStatus = read_mifare_classic_data_blocks(); break;

            case PICC_TYPE_MIFARE_UL:
                readStatus = read_mifare_ultralight_data_blocks();
                if (totalPages == 0) totalPages = dataPages;
                break;

            default: break;
        }
    } else {
        readStatus = read_felica_data();
    }

    return readStatus;
}

int PN532::read_mifare_classic_data_blocks() {
    byte no_of_sectors = 0;
    int sectorReadStatus = FAILURE;

    switch (uid.sak) {
        case PICC_TYPE_MIFARE_MINI:
            no_of_sectors = 5;
            totalPages = 20; // 320 bytes / 16 bytes per page
            break;

        case PICC_TYPE_MIFARE_1K:
            no_of_sectors = 16;
            totalPages = 64; // 1024 bytes / 16 bytes per page
            break;

        case PICC_TYPE_MIFARE_4K:
            no_of_sectors = 40;
            totalPages = 256; // 4096 bytes / 16 bytes per page
            break;

        default: // Should not happen. Ignore.
            break;
    }

    if (no_of_sectors) {
        bool anySuccess = false;
        for (int8_t i = 0; i < no_of_sectors; i++) {
            displayInfo("Reading sector " + String(i) + "/" + String(no_of_sectors) + "...");
            sectorReadStatus = read_mifare_classic_data_sector(i);
            if (sectorReadStatus == SUCCESS) {
                anySuccess = true;
            } else if (sectorReadStatus == TAG_NOT_PRESENT) {
                break; // Card removed, stop entirely
            } else {
                // Auth failed or read error for this sector — fill with zeros and continue
                byte firstBlock = (i < 32) ? i * 4 : 128 + (i - 32) * 16;
                byte blocksInSector = (i < 32) ? 4 : 16;
                for (byte b = 0; b < blocksInSector; b++) {
                    strAllPages += "Page " + String(dataPages) + ": 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";
                    dataPages++;
                }
                // Re-detect card for next sector attempt
                nfc.startPassiveTargetIDDetection();
                nfc.readDetectedPassiveTargetID();
            }
        }
        return anySuccess ? SUCCESS : sectorReadStatus;
    }
    return sectorReadStatus;
}

int PN532::read_mifare_classic_data_sector(byte sector) {
    byte firstBlock;
    byte no_of_blocks;

    if (sector < 32) {
        no_of_blocks = 4;
        firstBlock = sector * no_of_blocks;
    } else if (sector < 40) {
        no_of_blocks = 16;
        firstBlock = 128 + (sector - 32) * no_of_blocks;
    } else {
        return FAILURE;
    }

    byte buffer[18];
    byte blockAddr;
    String strPage;

    int authStatus = authenticate_mifare_classic(firstBlock);
    if (authStatus != SUCCESS) return authStatus;

    for (int8_t blockOffset = 0; blockOffset < no_of_blocks; blockOffset++) {
        strPage = "";
        blockAddr = firstBlock + blockOffset;

        if (!nfc.mifareclassic_ReadDataBlock(blockAddr, buffer)) return FAILURE;

        strPage = hexToStr(buffer, 16);

        strAllPages += "Page " + String(dataPages) + ": " + strPage + "\n";
        dataPages++;
    }

    return SUCCESS;
}

int PN532::authenticate_mifare_classic(byte block) {
    uint8_t successA = 0;
    uint8_t successB = 0;

    for (auto key : keys) {
        successA = nfc.mifareclassic_AuthenticateBlock(uid.uidByte, uid.size, block, 0, key);
        if (successA) break;

        if (!nfc.startPassiveTargetIDDetection() || !nfc.readDetectedPassiveTargetID()) {
            return TAG_NOT_PRESENT;
        }
    }

    if (!successA) {
        uint8_t keyA[6];

        for (const auto &mifKey : bruceConfig.mifareKeys) {
            for (size_t i = 0; i < mifKey.length(); i += 2) {
                keyA[i / 2] = strtoul(mifKey.substring(i, i + 2).c_str(), NULL, 16);
            }

            successA = nfc.mifareclassic_AuthenticateBlock(uid.uidByte, uid.size, block, 0, keyA);
            if (successA) break;

            if (!nfc.startPassiveTargetIDDetection() || !nfc.readDetectedPassiveTargetID()) {
                return TAG_NOT_PRESENT;
            }
        }
    }

    if (!successA) {
        int dictSize = (int)dictionaryKeys.size();
        for (int di = 0; di < dictSize; di++) {
            if (di % 50 == 0) {
                displayInfo("Dict key " + String(di) + "/" + String(dictSize) + " (A)");
            }
            successA = nfc.mifareclassic_AuthenticateBlock(uid.uidByte, uid.size, block, 0, dictionaryKeys[di]);
            if (successA) break;

            if (!nfc.startPassiveTargetIDDetection() || !nfc.readDetectedPassiveTargetID()) {
                return TAG_NOT_PRESENT;
            }
        }
    }

    for (auto key : keys) {
        successB = nfc.mifareclassic_AuthenticateBlock(uid.uidByte, uid.size, block, 1, key);
        if (successB) break;

        if (!nfc.startPassiveTargetIDDetection() || !nfc.readDetectedPassiveTargetID()) {
            return TAG_NOT_PRESENT;
        }
    }

    if (!successB) {
        uint8_t keyB[6];

        for (const auto &mifKey : bruceConfig.mifareKeys) {
            for (size_t i = 0; i < mifKey.length(); i += 2) {
                keyB[i / 2] = strtoul(mifKey.substring(i, i + 2).c_str(), NULL, 16);
            }

            successB = nfc.mifareclassic_AuthenticateBlock(uid.uidByte, uid.size, block, 1, keyB);
            if (successB) break;

            if (!nfc.startPassiveTargetIDDetection() || !nfc.readDetectedPassiveTargetID()) {
                return TAG_NOT_PRESENT;
            }
        }
    }

    if (!successB) {
        int dictSize = (int)dictionaryKeys.size();
        for (int di = 0; di < dictSize; di++) {
            if (di % 50 == 0) {
                displayInfo("Dict key " + String(di) + "/" + String(dictSize) + " (B)");
            }
            successB = nfc.mifareclassic_AuthenticateBlock(uid.uidByte, uid.size, block, 1, dictionaryKeys[di]);
            if (successB) break;

            if (!nfc.startPassiveTargetIDDetection() || !nfc.readDetectedPassiveTargetID()) {
                return TAG_NOT_PRESENT;
            }
        }
    }

    return (successA || successB) ? SUCCESS : TAG_AUTH_ERROR;
}

int PN532::read_mifare_ultralight_data_blocks() {
    uint8_t success;
    byte buffer[18];
    byte i;
    String strPage = "";

    uint8_t buf[4];
    nfc.mifareultralight_ReadPage(3, buf);
    switch (buf[2]) {
        // NTAG213
        case 0x12: totalPages = 45; break;
        // NTAG215
        case 0x3E: totalPages = 135; break;
        // NTAG216
        case 0x6D: totalPages = 231; break;
        // MIFARE UL
        default: totalPages = 64; break;
    }

    for (byte page = 0; page < totalPages; page += 4) {
        success = nfc.ntag2xx_ReadPage(page, buffer);
        if (!success) return FAILURE;

        for (byte offset = 0; offset < 4; offset++) {
            strPage = "";
            for (byte index = 0; index < 4; index++) {
                i = 4 * offset + index;
                strPage += buffer[i] < 0x10 ? F(" 0") : F(" ");
                strPage += String(buffer[i], HEX);
            }
            strPage.trim();
            strPage.toUpperCase();

            strAllPages += "Page " + String(dataPages) + ": " + strPage + "\n";
            dataPages++;
            if (dataPages >= totalPages) break;
        }
    }

    return SUCCESS;
}

int PN532::read_felica_data() {
    String strPage = "";
    totalPages = 14;

    for (uint16_t i = 0x8000; i < 0x8000 + totalPages; i++) {
        uint16_t block_list[1] = {i}; // Read the block i
        uint8_t block_data[1][16] = {0};
        uint16_t default_service_code[1] = {
            0x000B
        }; // Default service code for reading. Should works for every card
        int res = nfc.felica_ReadWithoutEncryption(1, default_service_code, 1, block_list, block_data);

        for (size_t i = 0; i < 16; i++) {
            if (res) { // If card block read successfully, copy data to string
                strPage = hexToStr(block_data[0], 16);
            }
        }
        if (res) { // If PN532 can't read the FeliCa tag, don't write the block to file
            strAllPages += "Block " + String(dataPages++) + ": " + strPage + "\n";
        }
        strPage = ""; // Reset for the next block
    }

    return SUCCESS;
}

int PN532::write_data_blocks() {
    String pageLine = "";
    String strBytes = "";
    int lineBreakIndex;
    int pageIndex;
    bool blockWriteSuccess;
    int totalSize = strAllPages.length();

    while (strAllPages.length() > 0) {
        lineBreakIndex = strAllPages.indexOf("\n");

        if (lineBreakIndex < 0) { break; } // Added for .JS and BugFix

        pageLine = strAllPages.substring(0, lineBreakIndex);
        strAllPages = strAllPages.substring(lineBreakIndex + 1);

        pageIndex = pageLine.substring(5, pageLine.indexOf(":")).toInt();
        strBytes = pageLine.substring(pageLine.indexOf(":") + 1);
        strBytes.trim();

        if (pageIndex == 0) continue;

        if (printableUID.picc_type != "FeliCa") {
            switch (uid.sak) {
                case PICC_TYPE_MIFARE_MINI:
                case PICC_TYPE_MIFARE_1K:
                case PICC_TYPE_MIFARE_4K:
                    if (pageIndex == 0 || (pageIndex + 1) % 4 == 0) continue;
                    blockWriteSuccess = write_mifare_classic_data_block(pageIndex, strBytes);
                    break;

                case PICC_TYPE_MIFARE_UL:
                    if (pageIndex < 4 || pageIndex >= dataPages - 5) continue;
                    blockWriteSuccess = write_mifare_ultralight_data_block(pageIndex, strBytes);
                    break;

                default: blockWriteSuccess = false; break;
            }
        } else {
            blockWriteSuccess = write_felica_data_block(pageIndex, strBytes);
        }

        if (!blockWriteSuccess) return FAILURE;

        progressHandler(totalSize - strAllPages.length(), totalSize, "Writing data blocks...");
    }

    return SUCCESS;
}

// ---------------------------------------------------------------------------
// write_data_blocks_full — like write_data_blocks() but also writes sector
// trailers (the last block of every sector) so a full card clone is possible.
// Block 0 is still skipped (already written by clone_full).
// ---------------------------------------------------------------------------
int PN532::write_data_blocks_full() {
    // Only supported for MIFARE Classic
    if (uid.sak != PICC_TYPE_MIFARE_MINI &&
        uid.sak != PICC_TYPE_MIFARE_1K   &&
        uid.sak != PICC_TYPE_MIFARE_4K) {
        // Fall back to normal write for other card types
        return write_data_blocks();
    }

    String allPagesSnapshot = strAllPages; // preserve original
    String pageLine;
    String strBytes;
    int lineBreakIndex;
    int pageIndex;
    bool blockWriteSuccess;
    int totalSize = strAllPages.length();

    while (strAllPages.length() > 0) {
        lineBreakIndex = strAllPages.indexOf("\n");
        if (lineBreakIndex < 0) break;

        pageLine   = strAllPages.substring(0, lineBreakIndex);
        strAllPages = strAllPages.substring(lineBreakIndex + 1);

        pageIndex = pageLine.substring(5, pageLine.indexOf(":")).toInt();
        strBytes  = pageLine.substring(pageLine.indexOf(":") + 1);
        strBytes.trim();

        // Always skip block 0 — already written by clone_full
        if (pageIndex == 0) continue;

        // Determine whether this block is a sector trailer
        bool isTrailer = ((pageIndex + 1) % 4 == 0);

        if (isTrailer) {
            blockWriteSuccess = write_mifare_classic_trailer_block(pageIndex, strBytes);
        } else {
            blockWriteSuccess = write_mifare_classic_data_block(pageIndex, strBytes);
        }

        if (!blockWriteSuccess) {
            strAllPages = allPagesSnapshot; // restore
            return FAILURE;
        }

        progressHandler(totalSize - strAllPages.length(), totalSize, "Cloning all blocks...");
    }

    strAllPages = allPagesSnapshot; // restore for subsequent operations
    return SUCCESS;
}

bool PN532::write_mifare_classic_data_block(int block, String data) {
    data.replace(" ", "");
    byte size = data.length() / 2;
    byte buffer[size];

    if (size != 16) return false;

    for (size_t i = 0; i < data.length(); i += 2) {
        buffer[i / 2] = strtoul(data.substring(i, i + 2).c_str(), NULL, 16);
    }

    if (authenticate_mifare_classic(block) != SUCCESS) return false;

    return nfc.mifareclassic_WriteDataBlock(block, buffer);
}

// Writes a MIFARE Classic sector trailer block (key A + access bits + key B).
// Authentication uses the KEY stored in the dump itself (bytes 0-5 = key A).
bool PN532::write_mifare_classic_trailer_block(int block, String data) {
    data.replace(" ", "");
    byte size = data.length() / 2;
    if (size != 16) return false;

    byte buffer[16];
    for (size_t i = 0; i < data.length(); i += 2) {
        buffer[i / 2] = strtoul(data.substring(i, i + 2).c_str(), NULL, 16);
    }

    // Authenticate with existing key before overwriting the trailer
    if (authenticate_mifare_classic(block) != SUCCESS) return false;

    return nfc.mifareclassic_WriteDataBlock(block, buffer);
}

bool PN532::write_mifare_ultralight_data_block(int block, String data) {
    data.replace(" ", "");
    byte size = data.length() / 2;
    byte buffer[size];

    if (size != 4) return false;

    for (size_t i = 0; i < data.length(); i += 2) {
        buffer[i / 2] = strtoul(data.substring(i, i + 2).c_str(), NULL, 16);
    }

    return nfc.ntag2xx_WritePage(block, buffer);
}

int PN532::write_felica_data_block(int block, String data) {
    data.replace(" ", "");
    byte size = data.length() / 2;
    uint8_t block_data[1][16] = {0};

    if (size != 16) { return false; }

    for (size_t i = 0; i < data.length(); i += 2) {
        block_data[0][i / 2] = strtoul(data.substring(i, i + 2).c_str(), NULL, 16);
    }

    uint16_t block_list[1] = {(uint16_t)(block +
                                         0x8000)}; // Write the block i. Block in FeliCa start from 0x8000

    uint16_t default_service_code[1] = {
        0x0009
    }; // Default service code for writing. Should works for every card

    return nfc.felica_WriteWithoutEncryption(1, default_service_code, block, block_list, block_data);
}

int PN532::erase_data_blocks() {
    bool blockWriteSuccess;

    switch (uid.sak) {
        case PICC_TYPE_MIFARE_MINI:
        case PICC_TYPE_MIFARE_1K:
        case PICC_TYPE_MIFARE_4K:
            for (byte i = 1; i < 64; i++) {
                if ((i + 1) % 4 == 0) continue;
                blockWriteSuccess =
                    write_mifare_classic_data_block(i, "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
                if (!blockWriteSuccess) return FAILURE;
            }
            break;

        case PICC_TYPE_MIFARE_UL:
            // NDEF stardard
            blockWriteSuccess = write_mifare_ultralight_data_block(4, "03 00 FE 00");
            if (!blockWriteSuccess) return FAILURE;

            for (byte i = 5; i < 130; i++) {
                blockWriteSuccess = write_mifare_ultralight_data_block(i, "00 00 00 00");
                if (!blockWriteSuccess) return FAILURE;
            }
            break;

        default: break;
    }

    return SUCCESS;
}

int PN532::write_ndef_blocks() {
    if (uid.sak != PICC_TYPE_MIFARE_UL) return TAG_NOT_MATCH;

    byte ndef_size = ndefMessage.messageSize + 3;
    byte payload_size = ndef_size % 4 == 0 ? ndef_size : ndef_size + (4 - (ndef_size % 4));
    byte ndef_payload[payload_size];
    byte i;
    bool blockWriteSuccess;
    uint8_t success;

    ndef_payload[0] = ndefMessage.begin;
    ndef_payload[1] = ndefMessage.messageSize;
    ndef_payload[2] = ndefMessage.header;
    ndef_payload[3] = ndefMessage.tnf;
    ndef_payload[4] = ndefMessage.payloadSize;
    ndef_payload[5] = ndefMessage.payloadType;

    for (i = 0; i < ndefMessage.payloadSize; i++) { ndef_payload[i + 6] = ndefMessage.payload[i]; }

    ndef_payload[ndef_size - 1] = ndefMessage.end;

    if (payload_size > ndef_size) {
        for (i = ndef_size; i < payload_size; i++) { ndef_payload[i] = 0; }
    }

    for (int i = 0; i < payload_size; i += 4) {
        int block = 4 + (i / 4);
        success = nfc.ntag2xx_WritePage(block, ndef_payload + i);
        if (!success) return FAILURE;
    }

    return SUCCESS;
}

// ---------------------------------------------------------------------------
// Helper: extract 16 bytes from a "Page N: XX XX XX ... XX" line
// ---------------------------------------------------------------------------
static bool getBlockBytes(const String& allPages, int blockNum, uint8_t out[16]) {
    String prefix = "Page " + String(blockNum) + ": ";
    int idx = allPages.indexOf(prefix);
    if (idx < 0) return false;
    int start = idx + prefix.length();
    int eol = allPages.indexOf('\n', start);
    String hex = (eol > 0) ? allPages.substring(start, eol) : allPages.substring(start);
    hex.trim();
    hex.replace(" ", "");
    if (hex.length() < 32) return false; // need 16 bytes = 32 hex chars
    for (int i = 0; i < 16; i++) {
        out[i] = strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), NULL, 16);
    }
    return true;
}

void PN532::parse_known_card_formats() {
    // Only parse MIFARE Classic for now
    if (uid.sak != PICC_TYPE_MIFARE_MINI &&
        uid.sak != PICC_TYPE_MIFARE_1K &&
        uid.sak != PICC_TYPE_MIFARE_4K) return;

    uint8_t block[16];

    // --- Aime card detection ---
    // Block 1 starts with "SBSD" (0x53 0x42 0x53 0x44)
    if (getBlockBytes(strAllPages, 1, block)) {
        if (block[0] == 'S' && block[1] == 'B' && block[2] == 'S' && block[3] == 'D') {
            // Access code in block 2, bytes 6-15
            uint8_t block2[16];
            if (getBlockBytes(strAllPages, 2, block2)) {
                char code[30];
                snprintf(code, sizeof(code),
                    "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x",
                    block2[6], block2[7], block2[8], block2[9], block2[10],
                    block2[11], block2[12], block2[13], block2[14], block2[15]);
                parsedCardData += "Aime Card\n";
                parsedCardData += "Access Code: " + String(code) + "\n";
            }
        }
    }

    // --- Skylanders detection ---
    // Block 1 bytes 0-1 are typically the Skylander ID
    if (getBlockBytes(strAllPages, 1, block)) {
        // Skylanders always use key A0A1A2A3A4A5 and have specific ATQA
        if (uid.atqaByte[0] == 0x44 && uid.atqaByte[1] == 0x00 && uid.sak == PICC_TYPE_MIFARE_1K) {
            uint16_t skylander_id = (block[1] << 8) | block[0];
            uint16_t skylander_var = (block[3] << 8) | block[2];
            if (skylander_id != 0 && skylander_id != 0xFFFF) {
                parsedCardData += "Skylanders Figure\n";
                parsedCardData += "ID: " + String(skylander_id) + "\n";
                parsedCardData += "Variant: " + String(skylander_var) + "\n";
            }
        }
    }

    // --- Disney Infinity detection ---
    // Block 4, bytes 0-1 = 0x0F 0x01
    if (getBlockBytes(strAllPages, 4, block)) {
        if (block[0] == 0x0F && block[1] == 0x01) {
            uint32_t figure_num = ((uint32_t)block[4] << 24) | ((uint32_t)block[5] << 16) |
                                  ((uint32_t)block[6] << 8) | block[7];
            parsedCardData += "Disney Infinity Figure\n";
            parsedCardData += "Figure #: " + String(figure_num) + "\n";
        }
    }

    // --- Generic sector key summary ---
    // Show which sectors were read (useful feedback)
    int sectorsRead = 0;
    int sectorsTotal = 0;
    switch (uid.sak) {
        case PICC_TYPE_MIFARE_MINI: sectorsTotal = 5; break;
        case PICC_TYPE_MIFARE_1K:   sectorsTotal = 16; break;
        case PICC_TYPE_MIFARE_4K:   sectorsTotal = 40; break;
    }
    for (int s = 0; s < sectorsTotal; s++) {
        int trailerBlock = (s < 32) ? (s * 4 + 3) : (128 + (s - 32) * 16 + 15);
        uint8_t trailer[16];
        if (getBlockBytes(strAllPages, trailerBlock, trailer)) sectorsRead++;
    }
    if (sectorsTotal > 0) {
        parsedCardData += "Sectors: " + String(sectorsRead) + "/" + String(sectorsTotal) + " read\n";
    }
    if (!dictionaryKeys.empty()) {
        parsedCardData += "Dict keys loaded: " + String((int)dictionaryKeys.size()) + "\n";
    }
}
