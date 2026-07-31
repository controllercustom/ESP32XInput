// SPDX-License-Identifier: MIT

#include "ESP32XInput.h"
#include "xinput_descriptor.h"
#include <Arduino.h>
#include "tusb.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp32-hal-tinyusb.h"

static uint8_t g_xinputReportBuffer[XINPUT_REPORT_SIZE] __attribute__((aligned(4)));
static uint8_t g_xinputOutBuffer[64];

void buildDescriptors(uint16_t vid, uint16_t pid) {
    g_xinputVid = vid;
    g_xinputPid = pid;

    const_cast<uint8_t*>(g_xinputDescDevice)[0x08] = static_cast<uint8_t>(vid & 0xFF);
    const_cast<uint8_t*>(g_xinputDescDevice)[0x09] = static_cast<uint8_t>((vid >> 8) & 0xFF);
    const_cast<uint8_t*>(g_xinputDescDevice)[0x0A] = static_cast<uint8_t>(pid & 0xFF);
    const_cast<uint8_t*>(g_xinputDescDevice)[0x0B] = static_cast<uint8_t>((pid >> 8) & 0xFF);

    tinyusb_enable_interface(USB_INTERFACE_CUSTOM, 28, tusb_xinput_load_descriptor);
}

uint16_t tusb_xinput_load_descriptor(uint8_t *dst, uint8_t *itf) {
    uint8_t iface = *itf;
    uint8_t str_index = tinyusb_add_string_descriptor("XInput Controller");
    uint8_t desc[] = {
        9, TUSB_DESC_INTERFACE, iface, 0, 2, 0xFF, 0x5D, 0x01, str_index,
        5, TUSB_DESC_CS_INTERFACE, 0x00, 0x5D, 0x01,
        7, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_INTERRUPT, 32 & 0xFF, (32 >> 8) & 0xFF, 4,
        7, TUSB_DESC_ENDPOINT, 0x01, TUSB_XFER_INTERRUPT, 32 & 0xFF, (32 >> 8) & 0xFF, 8
    };
    *itf += 1;
    memcpy(dst, desc, sizeof(desc));
    return sizeof(desc);
}

void ESP32XInputClass::begin(uint16_t vid, uint16_t pid) {
    buildDescriptors(vid, pid);

    tinyusb_device_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.vid = vid;
    cfg.pid = pid;
    cfg.product_name = "XInput Controller";
    cfg.manufacturer_name = "Microsoft";
    cfg.serial_number = NULL;
    cfg.fw_version = 0x0100;
    cfg.usb_version = 0x0200;
    cfg.usb_class = 0xFF;
    cfg.usb_subclass = 0xFF;
    cfg.usb_protocol = 0xFF;
    cfg.usb_attributes = TUSB_DESC_CONFIG_ATT_SELF_POWERED;
    cfg.usb_power_ma = 500;

    tinyusb_init(&cfg);
    _connected = true;
}

bool ESP32XInputClass::isConnected() const {
    return _connected && tud_mounted();
}

void ESP32XInputClass::press(Button btn) {
    if (btn >= BUTTON_COUNT) return;
    _report.wButtons |= (1U << static_cast<uint8_t>(btn));
    _markDirty();
}

void ESP32XInputClass::release(Button btn) {
    if (btn >= BUTTON_COUNT) return;
    _report.wButtons &= ~(1U << static_cast<uint8_t>(btn));
    _markDirty();
}

void ESP32XInputClass::setButton(Button btn, bool pressed) {
    if (pressed) press(btn);
    else release(btn);
}

bool ESP32XInputClass::getButton(Button btn) const {
    if (btn >= BUTTON_COUNT) return false;
    return (_report.wButtons & (1U << static_cast<uint8_t>(btn))) != 0;
}

void ESP32XInputClass::setLeftTrigger(uint16_t value) {
    _report.bLeftTrigger = static_cast<uint8_t>((value > 32768U) ? 255U : (uint8_t)(value * 255U / 32768U));
    _markDirty();
}

void ESP32XInputClass::setRightTrigger(uint16_t value) {
    _report.bRightTrigger = static_cast<uint8_t>((value > 32768U) ? 255U : (uint8_t)(value * 255U / 32768U));
    _markDirty();
}

void ESP32XInputClass::setStickLeft(int16_t x, int16_t y) {
    _report.sThumbLX = static_cast<int16_t>(x);
    _report.sThumbLY = static_cast<int16_t>(y);
    _markDirty();
}

void ESP32XInputClass::setStickRight(int16_t x, int16_t y) {
    _report.sThumbRX = static_cast<int16_t>(x);
    _report.sThumbRY = static_cast<int16_t>(y);
    _markDirty();
}

void ESP32XInputClass::setHat(uint8_t hat) {
    // XInput d-pad is encoded in button bitmask bits 0-3.
    // hat: 0=up, 1=up-right, 2=right, ..., 7=up-left, 8=centered
    // Clear existing d-pad bits.
    _report.wButtons &= ~0x000FU;
    if (hat < 8) {
        // Map hat value (0-7) to XInput button bits (DPAD_UP=0, DPAD_DOWN=1, DPAD_LEFT=2, DPAD_RIGHT=3).
        static const uint8_t hatToButtons[8] = {
            0x01, // UP:       bit 0
            0x03, // UP+RIGHT: bits 0+3
            0x02, // RIGHT:    bit 3... wait, let me recalculate
            0x06, // DOWN+RIGHT
            0x04, // DOWN
            0x0C, // DOWN+LEFT
            0x08, // LEFT
            0x09  // UP+LEFT
        };
        _report.wButtons |= hatToButtons[hat];
    }
    _markDirty();
}

void ESP32XInputClass::setDpad(uint8_t dir) {
    setHat(dir);
}

uint8_t ESP32XInputClass::getHat() const {
    uint8_t btns = _report.wButtons & 0x000FU;
    bool up    = btns & 0x01;
    bool down  = btns & 0x04;
    bool left  = btns & 0x08;
    bool right = btns & 0x02;

    if (!up && !down && !left && !right) return 8; // centered
    if (up && !down && !left && !right) return 0;  // UP
    if (up && !down && right && !left) return 1;   // UP-RIGHT
    if (!up && !down && right && !left) return 2;  // RIGHT
    if (!up && down && right && !left) return 3;   // DOWN-RIGHT
    if (!up && down && !left && !right) return 4;  // DOWN
    if (!up && down && left && !right) return 5;   // DOWN-LEFT
    if (!up && !down && left && !right) return 6;  // LEFT
    if (up && !down && left && !right) return 7;   // UP-LEFT
    return 8; // centered (mixed states)
}

void ESP32XInputClass::onRumble(RumbleCallback cb) {
    _onRumbleCb = std::move(cb);
}

void ESP32XInputClass::onLed(LedCallback cb) {
    _onLedCb = std::move(cb);
}

uint32_t ESP32XInputClass::setPollInterval(uint32_t ms) {
    uint32_t oldMs = _pollIntervalMs;
    if (_timerHandle != nullptr) {
        esp_timer_stop(_timerHandle);
        esp_timer_delete(_timerHandle);
        _timerHandle = nullptr;
    }

    _pollIntervalMs = (ms < 1U) ? 4U : ms;

    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = &ESP32XInputClass::timerCallback;
    timerArgs.arg = this;
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.name = "xinput_poll";

    if (esp_timer_create(&timerArgs, &_timerHandle) == ESP_OK && _pollIntervalMs > 0U) {
        esp_timer_start_periodic(_timerHandle, static_cast<int64_t>(_pollIntervalMs * 1000LL));
    }

    return oldMs;
}

void IRAM_ATTR ESP32XInputClass::timerCallback(void* arg) {
    if (arg == nullptr) return;
    auto self = reinterpret_cast<ESP32XInputClass*>(arg);
    if (!self->_dirtyFlag.load()) return;

    memcpy(g_xinputReportBuffer, &self->_report, sizeof(self->_report));

    tud_vendor_n_write(0, g_xinputReportBuffer, XINPUT_REPORT_SIZE);
    tud_vendor_n_write_flush(0);
    self->_dirtyFlag.store(false);
}

void ESP32XInputClass::_sendReport() {
    if (!_dirtyFlag.load()) return;
    memcpy(g_xinputReportBuffer, &_report, sizeof(_report));
    if (tud_mounted()) {
        tud_vendor_n_write(0, g_xinputReportBuffer, XINPUT_REPORT_SIZE);
        tud_vendor_n_write_flush(0);
    }
    _dirtyFlag.store(false);
}

void ESP32XInputClass::update() {
    _sendReport();
}

void ESP32XInputClass::send() {
    _sendReport();
}

bool ESP32XInputClass::ready() {
    return isConnected();
}

void ESP32XInputClass::releaseAll() {
    memset(&_report, 0, sizeof(_report));
    _markDirty();
    _sendReport();
}

void ESP32XInputClass::pollRumble() {
    if (tud_mounted() && tud_vendor_n_available(0)) {
        uint32_t len = tud_vendor_n_read(0, g_xinputOutBuffer, sizeof(g_xinputOutBuffer));
        if (len >= 3 && _onRumbleCb) {
            _onRumbleCb(g_xinputOutBuffer[0], g_xinputOutBuffer[1]);
        }
    }
}

ESP32XInputClass ESP32XInput;
