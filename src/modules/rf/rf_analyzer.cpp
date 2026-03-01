#include "rf_analyzer.h"
#include "rf_utils.h"

void rf_analyzer() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("Analyzer needs a CC1101!", true);
        return;
    }

    if (!initRfModule("rx", 433.92)) {
        displayError("CC1101 not found!", true);
        return;
    }

    ELECHOUSE_cc1101.setRxBW(200);

    int arraySize = sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]);
    float current_freq = 0.00;
    int current_rssi = -100;

    float last_freq = 0.00;
    int last_rssi = -100;

    unsigned long lastUpdate = millis();
    bool firstUpdate = true;
    int threshold = -80;

    tft.fillScreen(TFT_BLACK);

    while (1) {
        if (check(NextPress)) {
            threshold += 5;
            if (threshold > -50) threshold = -50;
            lastUpdate = 0;
        }
        if (check(PrevPress)) {
            threshold -= 5;
            if (threshold < -100) threshold = -100;
            lastUpdate = 0;
        }

        float sweep_max_freq = 0.0f;
        int sweep_max_rssi = -100;

        for (int i = 0; i < arraySize; i++) {
            setMHZ(subghz_frequency_list[i]);
            // Give cc1101 a tiny bit of time to settle and take reading
            delayMicroseconds(100);

            int i_rssi = ELECHOUSE_cc1101.getRssi();
            if (i_rssi > sweep_max_rssi) {
                sweep_max_rssi = i_rssi;
                sweep_max_freq = subghz_frequency_list[i];
            }
            if (check(EscPress) || check(SelPress)) {
                /* returnToMenu = true; (removed) */
                deinitRfModule();
                delay(10);
                return;
            }
        }

        // Signal threshold to avoid just saving noise
        if (sweep_max_rssi > threshold) {
            last_freq = current_freq;
            last_rssi = current_rssi;
            current_freq = sweep_max_freq;
            current_rssi = sweep_max_rssi;
        }

        if (firstUpdate || millis() - lastUpdate > 500) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(2);
            tft.setCursor(10, 10);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.println("FREQ ANALYZER");

            tft.setTextSize(2);
            tft.setCursor(10, 40);
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            if (firstUpdate) {
                tft.printf("Freq: 0.00 MHz\n");
                tft.setCursor(10, 65);
                tft.printf("RSSI: -100 dBm\n");
            } else {
                tft.printf("Freq: %.2f MHz\n", current_freq);
                tft.setCursor(10, 65);
                tft.printf("RSSI: %d dBm\n", current_rssi);
            }

            tft.setTextSize(1);
            tft.setCursor(10, 100);
            tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
            tft.printf("Last Freq: %.2f MHz\n", last_freq);
            tft.setCursor(10, 115);
            tft.printf("Last RSSI: %d dBm\n", last_rssi);

            tft.setCursor(10, 135);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.printf("Threshold: %d dBm\n", threshold);

            tft.setCursor(10, tft.height() - 20);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.print("Press ESC or SEL to exit");

            firstUpdate = false;
            lastUpdate = millis();
        }
    }
}
