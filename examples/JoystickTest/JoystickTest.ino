// SPDX-License-Identifier: MIT

#include <ESP32XInput.h>

void setup() {
    Serial.begin(115200);
    
    ESP32XInput.begin();
}

void loop() {
    static uint8_t phase = 0;
    
    switch (phase) {
        case 0: // Left stick sweep right
            for (int x = -32767; x <= 32767; x += 1000) {
                ESP32XInput.setStickLeft(x, 0);
                delay(5);
            }
            phase++;
            break;
            
        case 1: // Left stick sweep up
            for (int y = -32767; y <= 32767; y += 1000) {
                ESP32XInput.setStickLeft(0, y);
                delay(5);
            }
            phase++;
            break;
            
        case 2: // Right stick sweep right
            for (int x = -32767; x <= 32767; x += 1000) {
                ESP32XInput.setStickRight(x, 0);
                delay(5);
            }
            phase++;
            break;
            
        case 3: // Right stick sweep up
            for (int y = -32767; y <= 32767; y += 1000) {
                ESP32XInput.setStickRight(0, y);
                delay(5);
            }
            phase++;
            break;
            
        case 4: // Left trigger sweep
            for (uint16_t t = 0; t <= 32768; t += 1000) {
                ESP32XInput.setLeftTrigger(t);
                delay(5);
            }
            phase++;
            break;
            
        case 5: // Right trigger sweep
            for (uint16_t t = 0; t <= 32768; t += 1000) {
                ESP32XInput.setRightTrigger(t);
                delay(5);
            }
            phase++;
            break;
            
        case 6: // Reset to center/zero
            ESP32XInput.setStickLeft(0, 0);
            ESP32XInput.setStickRight(0, 0);
            ESP32XInput.setLeftTrigger(0);
            ESP32XInput.setRightTrigger(0);
            phase = 0;
            delay(500);
            break;
    }
}
