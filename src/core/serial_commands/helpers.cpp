#include "helpers.h"
#ifndef LITE_VERSION
#include <globals.h>

bool _setupPsramFs() {
    // https://github.com/tobozo/ESP32-PsRamFS/blob/main/examples/PSRamFS_Test/PSRamFS_Test.ino
    static bool psRamFSMounted = false;
    if (psRamFSMounted) return true; // avoid reinit

#ifdef BOARD_HAS_PSRAM
    PSRamFS.setPartitionSize(ESP.getFreePsram() / 2); // use half of psram
#else
    PSRamFS.setPartitionSize(SAFE_STACK_BUFFER_SIZE);
#endif

    if (!PSRamFS.begin()) {
        serialDevice->println("PSRamFS Mount Failed");
        psRamFSMounted = false;
        return false;
    }
    // else
    psRamFSMounted = true;
    return true;
}

char *_readFileFromSerial(size_t fileSizeChar) {
    char *buf = psramFound() ? (char *)ps_malloc(fileSizeChar + 1) : (char *)malloc(fileSizeChar + 1);

    if (buf == NULL) {
        serialDevice->printf("Could not allocate %d\n", fileSizeChar);
        return NULL;
    }

    size_t bufSize = 0;
    buf[0] = '\0';

    unsigned long lastData = millis();

    String currLine = "";
    serialDevice->println("Reading input data from serial buffer until EOF");
    serialDevice->flush();
    while (true) {
        if (!serialDevice->available()) {
            if (millis() - lastData > 5000) break; // timeout
            delay(10);
            continue;
        }

        lastData = millis();
        currLine = serialDevice->readStringUntil('\n');
        if (currLine == "EOF") break;
        size_t lineLength = currLine.length();

        if (bufSize + lineLength + 2 > fileSizeChar) {
            log_e("Input truncated!");
            break;
        }

        memcpy(buf + bufSize, currLine.c_str(), lineLength);
        bufSize += lineLength;
        buf[bufSize++] = '\n';
    }
    buf[bufSize] = '\0';
    return buf;
}

uint8_t *_readBytesFromSerial(size_t fileSize, size_t *outSize) {
    uint8_t *buf = psramFound() ? (uint8_t *)ps_malloc(fileSize) : (uint8_t *)malloc(fileSize);
    if (!buf) {
        serialDevice->printf("Could not allocate %d bytes for upload\n", fileSize);
        *outSize = 0;
        return NULL;
    }

    size_t bytesRead = 0;
    unsigned long lastData = millis();
    serialDevice->printf("Ready to receive %d bytes...\n", fileSize);

    while (bytesRead < fileSize) {
        if (!serialDevice->available()) {
            if (millis() - lastData > 5000) break; // Timeout after 5s of inactivity
            delay(10);
            continue;
        }

        lastData = millis();
        // Read as much as available to fill the buffer
        size_t available = serialDevice->available();
        size_t bytesToRead = (available < (fileSize - bytesRead)) ? available : (fileSize - bytesRead);

        size_t chunkRead = serialDevice->readBytes((char*)(buf + bytesRead), bytesToRead);
        bytesRead += chunkRead;
    }

    *outSize = bytesRead;
    return buf;
}

bool getFsStorageFromPath(FS *&fs, String &filepath) {
    if (filepath.startsWith("/sd/")) {
        if (!setupSdCard()) return false;
        fs = &SD;
        filepath = filepath.substring(3); // becomes '/' + rest
        if (filepath == "") filepath = "/";
        return true;
    } else if (filepath.startsWith("/littlefs/")) {
        fs = &LittleFS;
        filepath = filepath.substring(9);
        if (filepath == "") filepath = "/";
        return true;
    }
    // Default fallback if no valid prefix:
    return getFsStorage(fs);
}

#endif
