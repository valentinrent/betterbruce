#include "u2f_app.h"
#include <Arduino.h>
#include "core/display.h"
#include <globals.h>
#include "u2f.h"
#include "USB_U2F.h"
#include <USB.h>

#if defined(USB_as_HID)

static U2fData* u2f_instance = NULL;
static bool auth_requested = false;

static void u2f_event_callback(U2fNotifyEvent evt, void* context) {
    if (evt == U2fNotifyAuth || evt == U2fNotifyRegister) {
        auth_requested = true;
    } else if (evt == U2fNotifyAuthSuccess || evt == U2fNotifyError) {
        auth_requested = false;
    }
}

void u2f_key_setup() {
    u2f_instance = u2f_alloc();
    if (!u2f_init(u2f_instance)) {
        tft.fillScreen(bruceConfig.bgColor);
        tft.setTextColor(bruceConfig.priColor);
        tft.setTextSize(FM);
        tft.setCursor(10, 30);
        tft.print("U2F Init Failed");
        delay(2000);
        u2f_free(u2f_instance);
        return;
    }

    u2f_set_event_callback(u2f_instance, u2f_event_callback, NULL);

    U2F_HID.begin(u2f_instance);

    USB.VID(0x0483);
    USB.PID(0x5741);
    USB.productName("FIDO U2F Security Key");
    USB.manufacturerName("Bruce");
    USB.begin();

    bool exit_app = false;
    bool blink = false;
    unsigned long last_blink = 0;

    tft.fillScreen(bruceConfig.bgColor);
    tft.drawRect(5, 5, 150, 70, bruceConfig.priColor);

    while (!exit_app) {
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.setTextSize(FM);
        tft.setCursor(15, 15);
        tft.print("U2F Sec Key");

        if (millis() - last_blink > 500) {
            blink = !blink;
            last_blink = millis();
        }

        tft.setTextSize(1);
        tft.setCursor(15, 45);

        if (auth_requested) {
            if (blink) {
                tft.setTextColor(TFT_BLACK, bruceConfig.secColor);
                tft.print(" PRESS BUTTON ");
            } else {
                tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
                tft.print(" PRESS BUTTON ");
            }
        } else {
            tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
            tft.print("Waiting for PC...");
        }



        if (check(EscPress)) {
            exit_app = true;
        }

        if (check(SelPress) || check(UpPress) || check(DownPress)) {
            if (auth_requested) {
                u2f_confirm_user_present(u2f_instance);
                auth_requested = false;

                // Visual feedback
                tft.fillRoundRect(15, 60, 100, 10, 2, bruceConfig.secColor);
                delay(100);
                tft.fillRoundRect(15, 60, 100, 10, 2, bruceConfig.bgColor);
            }
        }

        delay(50);
    }

    // Cleanup
    U2F_HID.end();
    USB.~ESPUSB();
    USB.enableDFU();

    u2f_free(u2f_instance);
}
#else
void u2f_key_setup() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor);
    tft.setTextSize(FM);
    tft.setCursor(10, 30);
    tft.print("USB not enabled");
    delay(2000);
}
#endif
