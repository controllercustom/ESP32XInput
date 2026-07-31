// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include "tusb.h"

#define XINPUT_VID_DEFAULT 0x045EU
#define XINPUT_PID_DEFAULT 0x028EU

#define XINPUT_CONFIG_DESC_LEN 37U

static uint16_t g_xinputVid = XINPUT_VID_DEFAULT;
static uint16_t g_xinputPid = XINPUT_PID_DEFAULT;

static const uint8_t g_xinputDescDevice[] = {
    0x12, TUSB_DESC_DEVICE,
    0x00, 0x02,
    0xFF, 0xFF, 0xFF,
    64,
    uint8_t(XINPUT_VID_DEFAULT & 0xFF), uint8_t(XINPUT_VID_DEFAULT >> 8),
    uint8_t(XINPUT_PID_DEFAULT & 0xFF), uint8_t(XINPUT_PID_DEFAULT >> 8),
    0x00, 0x01,
    1, 2, 3
};

static const uint8_t g_xinputDescConfig[] = {
    0x09, TUSB_DESC_CONFIGURATION,
    XINPUT_CONFIG_DESC_LEN & 0xFF, (XINPUT_CONFIG_DESC_LEN >> 8) & 0xFF,
    1, 1, 0, 0xE0, 50 / 2U,

    9, TUSB_DESC_INTERFACE, 0, 0, 2, 0xFF, 0x5D, 0x01, 0,
    5, TUSB_DESC_CS_INTERFACE, 0x00, 0x5D, 0x01,
    7, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_INTERRUPT, 32 & 0xFF, (32 >> 8) & 0xFF, 4,
    7, TUSB_DESC_ENDPOINT, 0x01, TUSB_XFER_INTERRUPT, 32 & 0xFF, (32 >> 8) & 0xFF, 8
};

static const uint8_t g_xinputDescString0[] = {
    4, TUSB_DESC_STRING, 0x09, 0x04
};

void buildDescriptors(uint16_t vid, uint16_t pid);

uint16_t tusb_xinput_load_descriptor(uint8_t *dst, uint8_t *itf);
