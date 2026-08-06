// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com

// BasicGamepad — UART command parser for ESP32XInput.
// Accepts KEY=VALUE\n commands over Serial and sends XInput reports.
//
// Commands:
//   BTN_A=1, BTN_B=0, BTN_X=1, BTN_Y=0, etc.  (0 or 1)
//   BTN_START=1, BTN_BACK=1, BTN_LB=1, BTN_RB=1
//   BTN_LTHUMB=1, BTN_RTHUMB=1, BTN_GUIDE=1
//   DPAD_UP=1, DPAD_DOWN=0, DPAD_LEFT=0, DPAD_RIGHT=1
//   LX=0, LY=0, RX=0, RY=0                       (signed -32767..32767)
//   TRIG_L=200, TRIG_R=200                         (unsigned 0..32768)
//   RELEASE                                         (release all + send)

#include <ESP32XInput.h>

static int16_t lx = 0, ly = 0, rx = 0, ry = 0;

static void exec(const char *key, const char *val) {
  bool pressed = atoi(val) != 0;

  if (strcmp(key, "BTN_A") == 0)         { ESP32XInput.setButton(ESP32XInputClass::A, pressed); }
  else if (strcmp(key, "BTN_B") == 0)         { ESP32XInput.setButton(ESP32XInputClass::B, pressed); }
  else if (strcmp(key, "BTN_X") == 0)         { ESP32XInput.setButton(ESP32XInputClass::X, pressed); }
  else if (strcmp(key, "BTN_Y") == 0)         { ESP32XInput.setButton(ESP32XInputClass::Y, pressed); }
  else if (strcmp(key, "BTN_START") == 0)     { ESP32XInput.setButton(ESP32XInputClass::START, pressed); }
  else if (strcmp(key, "BTN_BACK") == 0)      { ESP32XInput.setButton(ESP32XInputClass::BACK, pressed); }
  else if (strcmp(key, "BTN_LB") == 0)        { ESP32XInput.setButton(ESP32XInputClass::LEFT_SHOULDER, pressed); }
  else if (strcmp(key, "BTN_RB") == 0)        { ESP32XInput.setButton(ESP32XInputClass::RIGHT_SHOULDER, pressed); }
  else if (strcmp(key, "BTN_LTHUMB") == 0)    { ESP32XInput.setButton(ESP32XInputClass::LEFT_THUMB, pressed); }
  else if (strcmp(key, "BTN_RTHUMB") == 0)    { ESP32XInput.setButton(ESP32XInputClass::RIGHT_THUMB, pressed); }
  else if (strcmp(key, "BTN_GUIDE") == 0)     { ESP32XInput.setButton(ESP32XInputClass::XBOX, pressed); }
  else if (strcmp(key, "DPAD_UP") == 0)       { /* handled below */ }
  else if (strcmp(key, "DPAD_DOWN") == 0)     { /* handled below */ }
  else if (strcmp(key, "DPAD_LEFT") == 0)     { /* handled below */ }
  else if (strcmp(key, "DPAD_RIGHT") == 0)    { /* handled below */ }
  else if (strcmp(key, "LX") == 0)  { lx = atoi(val); ESP32XInput.setStickLeft(lx, ly); }
  else if (strcmp(key, "LY") == 0)  { ly = atoi(val); ESP32XInput.setStickLeft(lx, ly); }
  else if (strcmp(key, "RX") == 0)  { rx = atoi(val); ESP32XInput.setStickRight(rx, ry); }
  else if (strcmp(key, "RY") == 0)  { ry = atoi(val); ESP32XInput.setStickRight(rx, ry); }
  else if (strcmp(key, "TRIG_L") == 0)  { ESP32XInput.setLeftTrigger((uint16_t)atoi(val)); }
  else if (strcmp(key, "TRIG_R") == 0)  { ESP32XInput.setRightTrigger((uint16_t)atoi(val)); }
  else if (strcmp(key, "RELEASE") == 0) {
    ESP32XInput.releaseAll();
    Serial.println("SENT=1");
    return;
  }
  else {
    Serial.print("UNKNOWN_CMD=");
    Serial.println(key);
    return;
  }

  // Handle d-pad: read current state, update single direction, rebuild hat.
  if (strncmp(key, "DPAD_", 5) == 0) {
    static bool dpadUp = false, dpadDown = false, dpadLeft = false, dpadRight = false;
    if (strcmp(key, "DPAD_UP") == 0) dpadUp = pressed;
    else if (strcmp(key, "DPAD_DOWN") == 0) dpadDown = pressed;
    else if (strcmp(key, "DPAD_LEFT") == 0) dpadLeft = pressed;
    else if (strcmp(key, "DPAD_RIGHT") == 0) dpadRight = pressed;

    uint8_t hat = 8; // centered
    if (dpadUp && !dpadDown && !dpadLeft && !dpadRight) hat = 0;
    else if (dpadUp && !dpadDown && dpadRight && !dpadLeft) hat = 1;
    else if (!dpadUp && !dpadDown && dpadRight && !dpadLeft) hat = 2;
    else if (!dpadUp && dpadDown && dpadRight && !dpadLeft) hat = 3;
    else if (!dpadUp && dpadDown && !dpadLeft && !dpadRight) hat = 4;
    else if (!dpadUp && dpadDown && dpadLeft && !dpadRight) hat = 5;
    else if (!dpadUp && !dpadDown && dpadLeft && !dpadRight) hat = 6;
    else if (dpadUp && !dpadDown && dpadLeft && !dpadRight) hat = 7;
    ESP32XInput.setHat(hat);
  }

  ESP32XInput.send();
  Serial.println("SENT=1");
}

static char buf[32];
static size_t pos = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  ESP32XInput.begin();
  Serial.println("READY");
}

void loop() {
  ESP32XInput.pollRumble();
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (pos > 0) {
        buf[pos] = '\0';
        char *eq = (char *)memchr(buf, '=', pos);
        if (eq) {
          *eq = '\0';
          exec(buf, eq + 1);
        }
        pos = 0;
      }
    } else if (pos < sizeof(buf) - 1) {
      buf[pos++] = c;
    }
  }
}
