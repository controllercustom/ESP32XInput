// SPDX-License-Identifier: MIT
// ESP32XInput — Latency benchmark emitter.
//
// Waits for "START" over Serial, then emits timestamped HID report events
// (buttons, sticks, triggers) at high speed. Attach a host-side python script
// that reads these timestamps and correlates them with Linux evdev events
// to measure end-to-end USB HID latency.
//
// Serial protocol:
//   TX lines:  TS:<us>:<seq>:<type>:<value>
//   TX lines:  BTN:<name>          (button enum -> evdev name mapping)
//   TX lines:  RPT:<payload_bytes>:<btn_count>
//   TX lines:  READY / DONE
//   RX line:   START               (begins measurement run)
//   RX line:   SYNC                (responds with SYNC:<us> for clock align)

#include <ESP32XInput.h>
#include "esp_timer.h"

static volatile uint64_t g_seq = 0;
static volatile bool g_running = false;

#define BTN_ITERATIONS   4
#define ANALOG_ITERATIONS 50

enum : uint8_t {
    TYPE_BTN_PRESS   = 0,
    TYPE_BTN_RELEASE = 1,
    TYPE_LEFT_STICK  = 2,
    TYPE_RIGHT_STICK = 3,
    TYPE_L_TRIGGER   = 4,
    TYPE_R_TRIGGER   = 5,
    TYPE_DPAD        = 6,
    TYPE_MARKER      = 99,
};

static const char* buttonNames[] = {
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
    "START", "BACK", "LEFT_THUMB", "RIGHT_THUMB",
    "LEFT_SHOULDER", "RIGHT_SHOULDER", "XBOX",
    "A", "B", "X", "Y"
};

static void emit(uint64_t ts_us, uint8_t type, uint16_t val) {
    g_seq++;
    Serial.print("TS:");
    Serial.print(ts_us);
    Serial.print(":");
    Serial.print(g_seq);
    Serial.print(":");
    Serial.print(type);
    Serial.print(":");
    Serial.println(val);
}

void setup() {
    Serial.begin(115200);

    ESP32XInput.begin();

    Serial.print("RPT:");
    Serial.print(sizeof(ESP32XInputClass::XInputReport));
    Serial.print(":");
    Serial.print(XINPUT_REPORT_SIZE);
    Serial.print(":");
    Serial.println(ESP32XInputClass::BUTTON_COUNT);

    for (uint8_t i = 0; i < ESP32XInputClass::BUTTON_COUNT; ++i) {
        Serial.print("BTN:");
        Serial.println(buttonNames[i]);
    }

    Serial.println("READY");
}

static void testButtonLatency() {
    for (uint8_t iter = 0; iter < BTN_ITERATIONS; ++iter) {
        for (uint8_t i = 0; i < ESP32XInputClass::BUTTON_COUNT; ++i) {
            auto btn = static_cast<ESP32XInputClass::Button>(i);

            uint64_t t0 = esp_timer_get_time();
            emit(t0, TYPE_BTN_PRESS, i);
            ESP32XInput.press(btn);
            ESP32XInput.send();
            delayMicroseconds(10000);

            uint64_t t1 = esp_timer_get_time();
            emit(t1, TYPE_BTN_RELEASE, i);
            ESP32XInput.release(btn);
            ESP32XInput.send();
            delayMicroseconds(10000);
        }
    }
}

static void testStickLatency() {
    for (uint8_t iter = 0; iter < ANALOG_ITERATIONS; ++iter) {
        uint64_t t;

        t = esp_timer_get_time();
        ESP32XInput.setStickLeft(32767, 0);
        ESP32XInput.send();
        emit(t, TYPE_LEFT_STICK, 0);
        delayMicroseconds(200);

        t = esp_timer_get_time();
        ESP32XInput.setStickLeft(0, 0);
        ESP32XInput.send();
        emit(t, TYPE_LEFT_STICK, 1);
        delayMicroseconds(200);

        t = esp_timer_get_time();
        ESP32XInput.setStickLeft(0, 32767);
        ESP32XInput.send();
        emit(t, TYPE_LEFT_STICK, 2);
        delayMicroseconds(200);

        t = esp_timer_get_time();
        ESP32XInput.setStickLeft(0, 0);
        ESP32XInput.send();
        emit(t, TYPE_LEFT_STICK, 3);
        delayMicroseconds(200);

        t = esp_timer_get_time();
        ESP32XInput.setStickRight(32767, 0);
        ESP32XInput.send();
        emit(t, TYPE_RIGHT_STICK, 0);
        delayMicroseconds(200);

        t = esp_timer_get_time();
        ESP32XInput.setStickRight(0, 0);
        ESP32XInput.send();
        emit(t, TYPE_RIGHT_STICK, 1);
        delayMicroseconds(200);
    }
}

static void testTriggerLatency() {
    for (uint8_t iter = 0; iter < ANALOG_ITERATIONS; ++iter) {
        uint64_t t;

        t = esp_timer_get_time();
        ESP32XInput.setLeftTrigger(32768U);
        ESP32XInput.send();
        emit(t, TYPE_L_TRIGGER, 0);
        delayMicroseconds(200);

        t = esp_timer_get_time();
        ESP32XInput.setLeftTrigger(0);
        ESP32XInput.send();
        emit(t, TYPE_L_TRIGGER, 1);
        delayMicroseconds(200);

        t = esp_timer_get_time();
        ESP32XInput.setRightTrigger(32768U);
        ESP32XInput.send();
        emit(t, TYPE_R_TRIGGER, 0);
        delayMicroseconds(200);

        t = esp_timer_get_time();
        ESP32XInput.setRightTrigger(0);
        ESP32XInput.send();
        emit(t, TYPE_R_TRIGGER, 1);
        delayMicroseconds(200);
    }
}

static void testDpadLatency() {
    for (uint8_t iter = 0; iter < BTN_ITERATIONS; ++iter) {
        uint64_t t;
        for (uint8_t h = 0; h <= 7; ++h) {
            t = esp_timer_get_time();
            ESP32XInput.setHat(h);
            ESP32XInput.send();
            emit(t, TYPE_DPAD, h);
            delayMicroseconds(200);
        }

        t = esp_timer_get_time();
        ESP32XInput.setHat(8);
        ESP32XInput.send();
        emit(t, TYPE_DPAD, 8);
        delayMicroseconds(200);
    }
}

static char serialBuf[64];
static size_t serialPos = 0;

void loop() {
    if (!g_running) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (serialPos > 0) {
                    serialBuf[serialPos] = '\0';

                    const char* p = serialBuf;
                    while (*p && (*p == ' ' || *p == '\t')) p++;
                    size_t len = strlen(p);
                    if (len > 0) {
                        while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\t' || p[len-1] == '\r' || p[len-1] == '\n')) len--;

                        if (strncmp(p, "START", len) == 0 && strlen("START") <= len) {
                            g_seq = 0;
                            g_running = true;
                            emit(esp_timer_get_time(), TYPE_MARKER, 0);
                        } else if (strncmp(p, "SYNC", len) == 0 && strlen("SYNC") <= len) {
                            Serial.print("SYNC:");
                            Serial.println(esp_timer_get_time());
                        }
                    }
                }
                serialPos = 0;
            } else if (serialPos < sizeof(serialBuf) - 1) {
                serialBuf[serialPos++] = c;
            }
        }
        return;
    }

    testButtonLatency();
    testStickLatency();
    testTriggerLatency();
    testDpadLatency();

    emit(esp_timer_get_time(), TYPE_MARKER, 1);
    g_running = false;
}
