#include "USB_U2F.h"
#include <Arduino.h>

#if defined(USB_as_HID)

// Replicating Flipper's U2F HID handling
#define U2F_HID_MAX_PAYLOAD_LEN ((U2F_HID_PACKET_LEN - 7) + 128 * (U2F_HID_PACKET_LEN - 5))
#define U2F_HID_TYPE_MASK 0x80
#define U2F_HID_TYPE_INIT 0x80
#define U2F_HID_TYPE_CONT 0x00

#define U2F_HID_PING  (U2F_HID_TYPE_INIT | 0x01)
#define U2F_HID_MSG   (U2F_HID_TYPE_INIT | 0x03)
#define U2F_HID_LOCK  (U2F_HID_TYPE_INIT | 0x04)
#define U2F_HID_INIT  (U2F_HID_TYPE_INIT | 0x06)
#define U2F_HID_WINK  (U2F_HID_TYPE_INIT | 0x08)
#define U2F_HID_ERROR (U2F_HID_TYPE_INIT | 0x3f)

#define U2F_HID_ERR_NONE          0x00
#define U2F_HID_ERR_INVALID_CMD   0x01
#define U2F_HID_ERR_INVALID_PAR   0x02
#define U2F_HID_ERR_INVALID_LEN   0x03
#define U2F_HID_ERR_INVALID_SEQ   0x04
#define U2F_HID_ERR_MSG_TIMEOUT   0x05
#define U2F_HID_ERR_CHANNEL_BUSY  0x06
#define U2F_HID_ERR_LOCK_REQUIRED 0x0a
#define U2F_HID_ERR_SYNC_FAIL     0x0b
#define U2F_HID_ERR_OTHER         0x7f

#define U2F_HID_BROADCAST_CID 0xFFFFFFFF

struct U2fHid_packet {
    uint32_t cid;
    uint16_t len;
    uint8_t cmd;
    uint8_t payload[U2F_HID_MAX_PAYLOAD_LEN];
};

USB_U2F U2F_HID;

const uint8_t hid_report_descriptor[] = {
    0x06, 0xD0, 0xF1, // Usage Page (FIDO Alliance)
    0x09, 0x01,       // Usage (U2F HID Authenticator Device)
    0xA1, 0x01,       // Collection (Application)

    0x19, 0x20,       // Usage Min (0x20)
    0x29, 0x20,       // Usage Max (0x20)
    0x15, 0x00,       // Logical Min (0)
    0x26, 0xFF, 0x00, // Logical Max (255)
    0x75, 0x08,       // Report Size (8)
    0x95, 0x40,       // Report Count (64)
    0x81, 0x02,       // Input (Data, Var, Abs)

    0x19, 0x21,       // Usage Min (0x21)
    0x29, 0x21,       // Usage Max (0x21)
    0x15, 0x00,       // Logical Min (0)
    0x26, 0xFF, 0x00, // Logical Max (255)
    0x75, 0x08,       // Report Size (8)
    0x95, 0x40,       // Report Count (64)
    0x91, 0x02,       // Output (Data, Var, Abs)
    0xC0              // End Collection
};

USB_U2F::USB_U2F(void) : hid() {}

uint16_t USB_U2F::_onGetDescriptor(uint8_t* dst) {
    memcpy(dst, hid_report_descriptor, sizeof(hid_report_descriptor));
    return sizeof(hid_report_descriptor);
}

void USB_U2F::_onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) {
    if(len > 0 && _rxQueue != NULL) {
        uint8_t packet[U2F_HID_PACKET_LEN] = {0};
        uint16_t copy_len = len > U2F_HID_PACKET_LEN ? U2F_HID_PACKET_LEN : len;

        // Some hosts send 65 bytes (1 byte report ID + 64 bytes data). Detect padding if necessary.
        if (len == U2F_HID_PACKET_LEN + 1 && buffer[0] == 0) {
            memcpy(packet, buffer + 1, U2F_HID_PACKET_LEN);
        } else {
            memcpy(packet, buffer, copy_len);
        }

        xQueueSend(_rxQueue, packet, 0); // Using xQueueSend as it is task context
    }
}

void USB_U2F::sendResponse(const uint8_t* buf) {
    hid.SendReport(0, buf, U2F_HID_PACKET_LEN);
}

struct U2fHid_State {
    uint8_t seq_id_last;
    uint16_t req_buf_ptr;
    uint32_t req_len_left;
    uint32_t lock_cid;
    bool lock;
    U2fHid_packet packet;
};

static void u2f_hid_send_response(USB_U2F* u2f_usb, U2fHid_State* state) {
    uint8_t packet_buf[U2F_HID_PACKET_LEN];
    uint16_t len_remain = state->packet.len;
    uint8_t len_cur = 0;
    uint8_t seq_cnt = 0;
    uint16_t data_ptr = 0;

    memset(packet_buf, 0, U2F_HID_PACKET_LEN);
    memcpy(packet_buf, &(state->packet.cid), sizeof(uint32_t));

    // Init packet
    packet_buf[4] = state->packet.cmd;
    packet_buf[5] = state->packet.len >> 8;
    packet_buf[6] = (state->packet.len & 0xFF);
    len_cur = (len_remain < (U2F_HID_PACKET_LEN - 7)) ? (len_remain) : (U2F_HID_PACKET_LEN - 7);
    if(len_cur > 0) memcpy(&packet_buf[7], state->packet.payload, len_cur);

    u2f_usb->sendResponse(packet_buf);

    data_ptr = len_cur;
    len_remain -= len_cur;

    // Continuation packets
    while(len_remain > 0) {
        memset(&packet_buf[4], 0, U2F_HID_PACKET_LEN - 4);
        packet_buf[4] = seq_cnt;
        len_cur = (len_remain < (U2F_HID_PACKET_LEN - 5)) ? (len_remain) : (U2F_HID_PACKET_LEN - 5);
        memcpy(&packet_buf[5], &(state->packet.payload[data_ptr]), len_cur);
        u2f_usb->sendResponse(packet_buf);
        seq_cnt++;
        len_remain -= len_cur;
        data_ptr += len_cur;
    }
}

static void u2f_hid_send_error(USB_U2F* u2f_usb, U2fHid_State* state, uint8_t error) {
    state->packet.len = 1;
    state->packet.cmd = U2F_HID_ERROR;
    state->packet.payload[0] = error;
    u2f_hid_send_response(u2f_usb, state);
}

static bool u2f_hid_parse_request(USB_U2F* u2f_usb, U2fHid_State* state, U2fData* u2f_data) {
    if(state->packet.cmd == U2F_HID_PING) {
        u2f_hid_send_response(u2f_usb, state);
    } else if(state->packet.cmd == U2F_HID_MSG) {
        if((state->lock == true) && (state->packet.cid != state->lock_cid)) return false;

        uint16_t resp_len = u2f_msg_parse(u2f_data, state->packet.payload, state->packet.len);
        if(resp_len > 0) {
            state->packet.len = resp_len;
            u2f_hid_send_response(u2f_usb, state);
        } else {
            return false;
        }
    } else if(state->packet.cmd == U2F_HID_LOCK) {
        if(state->packet.len != 1) return false;
        uint8_t lock_timeout = state->packet.payload[0];
        if(lock_timeout == 0) {
            state->lock = false;
            state->lock_cid = 0;
        } else {
            state->lock = true;
            state->lock_cid = state->packet.cid;
        }
    } else if(state->packet.cmd == U2F_HID_INIT) {
        if((state->packet.len != 8) || (state->packet.cid != U2F_HID_BROADCAST_CID) || (state->lock == true))
            return false;
        state->packet.len = 17;
        uint32_t random_cid = esp_random();
        memcpy(&(state->packet.payload[8]), &random_cid, sizeof(uint32_t));
        state->packet.payload[12] = 2; // Protocol version
        state->packet.payload[13] = 1; // Device version major
        state->packet.payload[14] = 0; // Device version minor
        state->packet.payload[15] = 1; // Device build version
        state->packet.payload[16] = 1; // Capabilities: wink
        u2f_hid_send_response(u2f_usb, state);
    } else if(state->packet.cmd == U2F_HID_WINK) {
        if(state->packet.len != 0) return false;
        u2f_wink(u2f_data);
        state->packet.len = 0;
        u2f_hid_send_response(u2f_usb, state);
    } else {
        return false;
    }
    return true;
}

void USB_U2F::_u2fTaskConfig(void *pvParameters) {
    USB_U2F* u2f_usb = (USB_U2F*)pvParameters;
    U2fHid_State* state = (U2fHid_State*)calloc(1, sizeof(U2fHid_State));
    if (!state) vTaskDelete(NULL);

    uint8_t packet_buf[U2F_HID_PACKET_LEN];

    while(1) {
        if(xQueueReceive(u2f_usb->_rxQueue, packet_buf, portMAX_DELAY) == pdTRUE) {
            uint32_t len_cur = U2F_HID_PACKET_LEN;

            if((packet_buf[4] & U2F_HID_TYPE_MASK) == U2F_HID_TYPE_INIT) {
                // Init packet
                state->packet.len = (packet_buf[5] << 8) | (packet_buf[6]);
                if(state->packet.len > U2F_HID_MAX_PAYLOAD_LEN) {
                    state->req_len_left = 0;
                    continue; // Wrong packet len
                }

                if(state->packet.len > (len_cur - 7)) {
                    state->req_len_left = state->packet.len - (len_cur - 7);
                    len_cur = len_cur - 7;
                } else {
                    state->req_len_left = 0;
                    len_cur = state->packet.len;
                }

                memcpy(&(state->packet.cid), packet_buf, 4);
                state->packet.cmd = packet_buf[4];
                state->seq_id_last = 0;
                state->req_buf_ptr = len_cur;

                if(len_cur > 0) memcpy(state->packet.payload, &packet_buf[7], len_cur);

            } else {
                // Continuation packet
                if(state->req_len_left > 0) {
                    uint32_t cid_temp = 0;
                    memcpy(&cid_temp, packet_buf, 4);
                    uint8_t seq_temp = packet_buf[4];

                    if((cid_temp == state->packet.cid) && (seq_temp == state->seq_id_last)) {
                        if(state->req_len_left > (len_cur - 5)) {
                            len_cur = len_cur - 5;
                            state->req_len_left -= len_cur;
                        } else {
                            len_cur = state->req_len_left;
                            state->req_len_left = 0;
                        }

                        memcpy(&(state->packet.payload[state->req_buf_ptr]), &packet_buf[5], len_cur);
                        state->req_buf_ptr += len_cur;
                        state->seq_id_last++;
                    }
                }
            }

            if(state->req_len_left == 0) {
                if(u2f_hid_parse_request(u2f_usb, state, u2f_usb->u2f_instance) == false) {
                    u2f_hid_send_error(u2f_usb, state, U2F_HID_ERR_INVALID_CMD);
                }
            }
        }
    }
}

void USB_U2F::begin(U2fData* u2f_data) {
    if(!active) {
        u2f_instance = u2f_data;
        if(_rxQueue == NULL) {
            _rxQueue = xQueueCreate(10, U2F_HID_PACKET_LEN);
        }
        if(_taskHandle == NULL) {
            xTaskCreate(_u2fTaskConfig, "u2f_hid_task", 8192, this, 5, &_taskHandle);
        }

        hid.begin();
        hid.addDevice(this, sizeof(hid_report_descriptor));
        active = true;
    }
}

void USB_U2F::end(void) {
    if(active) {
        if(_taskHandle != NULL) {
            vTaskDelete(_taskHandle);
            _taskHandle = NULL;
        }
        if(_rxQueue != NULL) {
            vQueueDelete(_rxQueue);
            _rxQueue = NULL;
        }
        active = false;
    }
}

#endif
