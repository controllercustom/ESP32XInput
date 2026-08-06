// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <atomic>
#include <functional>
#include "esp_timer.h"

#define XINPUT_REPORT_SIZE 20U

class ESP32XInputClass {
public:
    struct __attribute__((packed)) XInputReport {
        uint8_t  bMessageType;      // 0x00 — Xbox 360 message type (required by xpad driver)
        uint8_t  bMessageSize;      // 0x14 — report length
        uint16_t wButtons;
        uint8_t  bLeftTrigger;
        uint8_t  bRightTrigger;
        int16_t  sThumbLX;
        int16_t  sThumbLY;
        int16_t  sThumbRX;
        int16_t  sThumbRY;
        uint32_t dwReserved0;
        uint16_t wReserved1;
    };

    enum Button : uint8_t {
        DPAD_UP = 0,
        DPAD_DOWN,
        DPAD_LEFT,
        DPAD_RIGHT,
        START,
        BACK,
        LEFT_THUMB,
        RIGHT_THUMB,
        LEFT_SHOULDER,
        RIGHT_SHOULDER,
        XBOX,
        A = 12,
        B,
        X,
        Y,
        BUTTON_COUNT = 16U
    };

    void begin(uint16_t vid = 0x045E, uint16_t pid = 0x028E);
    bool isConnected() const;

    void press(Button btn);
    void release(Button btn);
    void setButton(Button btn, bool pressed);
    bool getButton(Button btn) const;

    void setLeftTrigger(uint16_t value);
    void setRightTrigger(uint16_t value);
    void setStickLeft(int16_t x, int16_t y);
    void setStickRight(int16_t x, int16_t y);

    void setHat(uint8_t hat);
    void setDpad(uint8_t dir);
    uint8_t getHat() const;

    using RumbleCallback = std::function<void(uint8_t leftMotor, uint8_t rightMotor)>;
    using LedCallback    = std::function<void(uint8_t ledIndex)>;
    void onRumble(RumbleCallback cb);
    void onLed(LedCallback cb);

    uint32_t setPollInterval(uint32_t ms);
    void update();
    void send();
    bool ready();
    void releaseAll();
    void pollRumble();
    const XInputReport& getReport() const;

private:
    static const uint8_t XINPUT_INTERFACE_ID = 0U;
    static const uint8_t XINPUT_IN_EP        = 0x81U;
    static const uint8_t XINPUT_OUT_EP       = 0x01U;
    static const uint8_t EP_MAX_SIZE         = 32U;

    void _sendReport();
    bool _isDirty() const { return _dirtyFlag.load(); }
    void _markDirty() { _dirtyFlag.store(true); }
    bool _canSend() const;

    XInputReport _report{0x00, 0x14};
    volatile std::atomic<bool> _dirtyFlag{false};
    
    uint32_t _pollIntervalMs = 8U;
    esp_timer_handle_t _timerHandle = nullptr;
    bool _connected = false;
    mutable volatile bool _usbReady = false;
    mutable unsigned long _mountedAt = 0;

    RumbleCallback _onRumbleCb = nullptr;
    LedCallback    _onLedCb   = nullptr;

    static void IRAM_ATTR timerCallback(void* arg);
};

static_assert(sizeof(ESP32XInputClass::XInputReport) == XINPUT_REPORT_SIZE,
    "XInputReport must be exactly 20 bytes");

extern ESP32XInputClass ESP32XInput;
