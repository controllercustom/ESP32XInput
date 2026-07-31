// SPDX-License-Identifier: MIT

#include <ESP32XInput.h>

void setup() {
    Serial.begin(115200);
    
    ESP32XInput.begin();
    
    while (!Serial) delay(10);
}

void loop() {
    static bool pressed = false;
    
    if (millis() % 2 == 0 && !pressed) {
        ESP32XInput.press(ESP32XInput.Button::A);
        Serial.println("Pressing A");
        pressed = true;
    } else if (millis() % 2 != 0 && pressed) {
        ESP32XInput.release(ESP32XInput.Button::A);
        Serial.println("Releasing A");
        pressed = false;
    }
    
    delay(500);
}
