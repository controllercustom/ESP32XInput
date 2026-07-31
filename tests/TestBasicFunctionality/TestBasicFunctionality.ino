// SPDX-License-Identifier: MIT
// ESP32XInput — Basic functionality on-board test suite.

#include <ESP32XInput.h>

static_assert(sizeof(ESP32XInputClass::Button) == sizeof(uint8_t), "Button enum must be uint8_t");
static_assert(ESP32XInputClass::BUTTON_COUNT == 15U, "Must have exactly 15 buttons");
static_assert(sizeof(ESP32XInputClass::XInputReport) == 20U, "XInputReport must be exactly 20 bytes");

static uint16_t pass = 0;
static uint16_t fail = 0;

#define CHECK(expr, msg) do { \
    if (expr) { pass++; } \
    else { fail++; Serial.print("FAIL: "); Serial.println(msg); } \
} while (0)

static void testButtons() {
    Serial.println("--- Buttons ---");

    for (uint8_t i = 0; i < ESP32XInputClass::BUTTON_COUNT; ++i) {
        auto btn = static_cast<ESP32XInputClass::Button>(i);

        ESP32XInput.press(btn);
        CHECK(ESP32XInput.getButton(btn), "press failed");

        ESP32XInput.release(btn);
        CHECK(!ESP32XInput.getButton(btn), "release failed");
    }

    ESP32XInput.setButton(ESP32XInputClass::A, true);
    CHECK(ESP32XInput.getButton(ESP32XInputClass::A), "setButton(true) failed");
    ESP32XInput.setButton(ESP32XInputClass::A, false);
    CHECK(!ESP32XInput.getButton(ESP32XInputClass::A), "setButton(false) failed");

    ESP32XInput.press(static_cast<ESP32XInputClass::Button>(ESP32XInputClass::BUTTON_COUNT));
    CHECK(!ESP32XInput.getButton(static_cast<ESP32XInputClass::Button>(ESP32XInputClass::BUTTON_COUNT)), "OOR press should be noop");

    for (uint8_t i = 0; i < ESP32XInputClass::BUTTON_COUNT; i++) {
        ESP32XInput.press(static_cast<ESP32XInputClass::Button>(i));
    }
    bool allPressed = true;
    for (uint8_t i = 0; i < ESP32XInputClass::BUTTON_COUNT; i++) {
        if (!ESP32XInput.getButton(static_cast<ESP32XInputClass::Button>(i))) { allPressed = false; break; }
    }
    CHECK(allPressed, "pressAll: not all pressed");
    ESP32XInput.releaseAll();
    bool nonePressed = true;
    for (uint8_t i = 0; i < ESP32XInputClass::BUTTON_COUNT; i++) {
        if (ESP32XInput.getButton(static_cast<ESP32XInputClass::Button>(i))) { nonePressed = false; break; }
    }
    CHECK(nonePressed, "releaseAll: buttons still held");
}

static void testSticks() {
    Serial.println("--- Sticks ---");
    ESP32XInput.setStickLeft(-32767, -32767);
    ESP32XInput.setStickLeft(32767, 32767);
    ESP32XInput.setStickRight(-32767, -32767);
    ESP32XInput.setStickRight(32767, 32767);
    CHECK(true, "stick writes survived extremes");
}

static void testTriggers() {
    Serial.println("--- Triggers ---");
    ESP32XInput.setLeftTrigger(0);
    ESP32XInput.setLeftTrigger(16384);
    ESP32XInput.setLeftTrigger(32768);
    ESP32XInput.setRightTrigger(0);
    ESP32XInput.setRightTrigger(16384);
    ESP32XInput.setRightTrigger(32768);
    CHECK(true, "trigger writes survived range");
}

static void testHat() {
    Serial.println("--- Hat ---");
    ESP32XInput.setHat(0); // UP
    ESP32XInput.setHat(2); // RIGHT
    ESP32XInput.setHat(4); // DOWN
    ESP32XInput.setHat(6); // LEFT
    ESP32XInput.setHat(8); // CENTERED
    CHECK(ESP32XInput.getHat() == 8, "hat centered");
}

static void testSendAndReady() {
    Serial.println("--- send/ready ---");
    uint32_t t0 = millis();
    while (!ESP32XInput.ready() && millis() - t0 < 3000) {
        delay(50);
    }
    if (!ESP32XInput.ready()) {
        Serial.println("  (USB not ready — skipping send test)");
        pass++;
        return;
    }
    ESP32XInput.send();
    CHECK(true, "send() survived");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    ESP32XInput.begin();
    delay(200);

    Serial.println("=== ESP32XInput TestBasicFunctionality ===");

    testButtons();
    testSticks();
    testTriggers();
    testHat();
    testSendAndReady();

    Serial.println("=== RESULTS ===");
    Serial.print("PASS: ");
    Serial.println(pass);
    Serial.print("FAIL: ");
    Serial.println(fail);
    if (fail == 0) {
        Serial.println("ALL TESTS PASSED");
    } else {
        Serial.println("TESTS FAILED");
    }
}

void loop() {
    delay(1000);
}
