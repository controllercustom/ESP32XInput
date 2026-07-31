// SPDX-License-Identifier: MIT

#include <ESP32XInput.h>

void onRumble(uint8_t leftMotor, uint8_t rightMotor) {
    Serial.print("Rumble received - Left motor: ");
    Serial.print(leftMotor);
    Serial.print(", Right motor: ");
    Serial.println(rightMotor);
}

void onLedChange(uint8_t ledIndex) {
    Serial.print("LED change requested, index: ");
    Serial.println(ledIndex);
}

void setup() {
    Serial.begin(115200);
    
    ESP32XInput.onRumble(onRumble);
    ESP32XInput.onLed(onLedChange);
    
    ESP32XInput.begin();
}

void loop() {
    static uint8_t buttonIdx = 0;
    const char* names[] = {"A", "B", "X", "Y"};
    
    Serial.print("Pressing ");
    Serial.println(names[buttonIdx]);
    
    switch (buttonIdx) {
        case 0: ESP32XInput.press(ESP32XInput.Button::A); break;
        case 1: ESP32XInput.press(ESP32XInput.Button::B); break;
        case 2: ESP32XInput.press(ESP32XInput.Button::X); break;
        case 3: ESP32XInput.press(ESP32XInput.Button::Y); break;
    }
    
    delay(1000);
    
    switch (buttonIdx) {
        case 0: ESP32XInput.release(ESP32XInput.Button::A); break;
        case 1: ESP32XInput.release(ESP32XInput.Button::B); break;
        case 2: ESP32XInput.release(ESP32XInput.Button::X); break;
        case 3: ESP32XInput.release(ESP32XInput.Button::Y); break;
    }
    
    buttonIdx = (buttonIdx + 1) % 4;
}
