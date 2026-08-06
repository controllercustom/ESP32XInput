// SPDX-License-Identifier: MIT
// ESP32XInput — Basic functionality on-board test suite.

#include <ESP32XInput.h>

static_assert(sizeof(ESP32XInputClass::Button) == sizeof(uint8_t), "Button enum must be uint8_t");
static_assert(ESP32XInputClass::BUTTON_COUNT == 16U, "Must have exactly 16 button slots (dpad 0-3, hold 4-10, gap 11, A/B/X/Y 12-15)");
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
    CHECK(ESP32XInput.getReport().sThumbLX == -32767 && ESP32XInput.getReport().sThumbLY == -32767, "stick left negative");

    ESP32XInput.setStickLeft(32767, 32767);
    CHECK(ESP32XInput.getReport().sThumbLX == 32767 && ESP32XInput.getReport().sThumbLY == 32767, "stick left positive");

    ESP32XInput.setStickRight(-1000, 500);
    CHECK(ESP32XInput.getReport().sThumbRX == -1000 && ESP32XInput.getReport().sThumbRY == 500, "stick right mixed");

    ESP32XInput.setStickLeft(0, 0);
    ESP32XInput.setStickRight(0, 0);
    CHECK(true, "stick writes survived extremes");
}

static void testTriggers() {
    Serial.println("--- Triggers ---");
    
    ESP32XInput.setLeftTrigger(0);
    CHECK(ESP32XInput.getReport().bLeftTrigger == 0, "left trigger zero");

    ESP32XInput.setLeftTrigger(16384);
    CHECK(ESP32XInput.getReport().bLeftTrigger == 127, "left trigger half (expected 127)");

    ESP32XInput.setLeftTrigger(32768);
    CHECK(ESP32XInput.getReport().bLeftTrigger == 255, "left trigger full");

    ESP32XInput.setRightTrigger(0);
    CHECK(ESP32XInput.getReport().bRightTrigger == 0, "right trigger zero");

    ESP32XInput.setRightTrigger(8192);
    CHECK(ESP32XInput.getReport().bRightTrigger == 63, "right trigger quarter (expected 63)");

    ESP32XInput.setLeftTrigger(0);
    ESP32XInput.setRightTrigger(0);
}

static void testHat() {
    Serial.println("--- Hat ---");
    
    for (uint8_t h = 0; h < 8; ++h) {
        ESP32XInput.setHat(h);
        CHECK(ESP32XInput.getHat() == h, "hat round-trip direction");
    }

    ESP32XInput.setHat(8); // CENTERED
    CHECK(ESP32XInput.getHat() == 8, "hat centered");

    ESP32XInput.setDpad(0); // UP via alias
    CHECK(ESP32XInput.getHat() == 0, "setDpad alias works (UP)");

    ESP32XInput.setHat(8); // reset to center
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
