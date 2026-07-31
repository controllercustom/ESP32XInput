// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com

// FullController — GPIO + analog stick gamepad for ESP32XInput.
// Reads 14 digital buttons, 4 d-pad directions, and 4 analog sticks
// from GPIO pins and sends XInput HID reports.

#include <ESP32XInput.h>

#define PIN_BTN_A      0
#define PIN_BTN_B      1
#define PIN_BTN_X      2
#define PIN_BTN_Y      3
#define PIN_BTN_LB     4
#define PIN_BTN_RB     5
#define PIN_BTN_BACK   6
#define PIN_BTN_START  7
#define PIN_BTN_LTHUMB 8
#define PIN_BTN_RTHUMB 9
#define PIN_BTN_GUIDE  10
#define PIN_DPAD_UP    11
#define PIN_DPAD_DN    12
#define PIN_DPAD_LT    13
#define PIN_DPAD_RT    14
#define PIN_LX         15
#define PIN_LY         16
#define PIN_RX         17
#define PIN_RY         18

static const struct { uint8_t pin; ESP32XInputClass::Button btn; } btn_pins[] = {
  {PIN_BTN_A,      ESP32XInputClass::A},
  {PIN_BTN_B,      ESP32XInputClass::B},
  {PIN_BTN_X,      ESP32XInputClass::X},
  {PIN_BTN_Y,      ESP32XInputClass::Y},
  {PIN_BTN_LB,     ESP32XInputClass::LEFT_SHOULDER},
  {PIN_BTN_RB,     ESP32XInputClass::RIGHT_SHOULDER},
  {PIN_BTN_BACK,   ESP32XInputClass::BACK},
  {PIN_BTN_START,  ESP32XInputClass::START},
  {PIN_BTN_LTHUMB, ESP32XInputClass::LEFT_THUMB},
  {PIN_BTN_RTHUMB, ESP32XInputClass::RIGHT_THUMB},
  {PIN_BTN_GUIDE,  ESP32XInputClass::XBOX},
};

void setup() {
  for (size_t i = 0; i < sizeof(btn_pins) / sizeof(btn_pins[0]); i++) {
    pinMode(btn_pins[i].pin, INPUT_PULLUP);
  }
  pinMode(PIN_DPAD_UP, INPUT_PULLUP);
  pinMode(PIN_DPAD_DN, INPUT_PULLUP);
  pinMode(PIN_DPAD_LT, INPUT_PULLUP);
  pinMode(PIN_DPAD_RT, INPUT_PULLUP);

  Serial.begin(115200);
  ESP32XInput.begin();
  delay(500);
}

void loop() {
  for (size_t i = 0; i < sizeof(btn_pins) / sizeof(btn_pins[0]); i++) {
    ESP32XInput.setButton(btn_pins[i].btn, !digitalRead(btn_pins[i].pin));
  }

  uint8_t dpad = 8; // centered
  bool up    = !digitalRead(PIN_DPAD_UP);
  bool down  = !digitalRead(PIN_DPAD_DN);
  bool left  = !digitalRead(PIN_DPAD_LT);
  bool right = !digitalRead(PIN_DPAD_RT);

  if (up && !down && !left && !right) dpad = 0;
  else if (up && !down && right && !left) dpad = 1;
  else if (!up && !down && right && !left) dpad = 2;
  else if (!up && down && right && !left) dpad = 3;
  else if (!up && down && !left && !right) dpad = 4;
  else if (!up && down && left && !right) dpad = 5;
  else if (!up && !down && left && !right) dpad = 6;
  else if (up && !down && left && !right) dpad = 7;
  ESP32XInput.setHat(dpad);

  int16_t lx = (analogRead(PIN_LX) * 65535 / 4095) - 32768;
  int16_t ly = (analogRead(PIN_LY) * 65535 / 4095) - 32768;
  int16_t rx = (analogRead(PIN_RX) * 65535 / 4095) - 32768;
  int16_t ry = (analogRead(PIN_RY) * 65535 / 4095) - 32768;
  ESP32XInput.setStickLeft(lx, ly);
  ESP32XInput.setStickRight(rx, ry);

  ESP32XInput.send();
  delay(8);
}
