// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022, Input Labs Oy.

#pragma once

// PICO PINS.
#define PIN_GROUP_PICO 1
#define PIN_GROUP_PICO_END 99
#define PIN_HOME 13
#define PIN_LED_PICO 25
#define PIN_LED_UP 2
#define PIN_LED_RIGHT 5
#define PIN_LED_DOWN 4
#define PIN_LED_LEFT 3
#define PIN_TOUCH_OUT 6
#define PIN_TOUCH_IN 7
#define PIN_ROTARY_A 9
#define PIN_ROTARY_B 8
#define PIN_SDA 14
#define PIN_SCL 15
#define PIN_SPI_CK 10
#define PIN_SPI_TX 11
#define PIN_SPI_RX 12
#define PIN_SPI_CS0 0
#define PIN_SPI_CS1 1
#define PIN_TX 27
#define PIN_TY 26
#define PIN_RIGHT_THUMBSTICK_X 29
#define PIN_RIGHT_THUMBSTICK_Y 28

// IO EXPANSION 1.
#define PIN_GROUP_IO_0 100
#define PIN_GROUP_IO_0_END 199
#define PIN_SELECT_1 114
#define PIN_SELECT_2 113
#define PIN_DPAD_LEFT 104
#define PIN_DPAD_RIGHT 101
#define PIN_DPAD_UP 103
#define PIN_DPAD_DOWN 100
#define PIN_L1 102
#define PIN_L2 115
#define PIN_L3 112
#define PIN_L4 109
#define PIN_PCBGEN_0 111
#define PIN_PCBGEN_1 110

// IO EXPANSION 2.
#define PIN_GROUP_IO_1 200
#define PIN_GROUP_IO_1_END 249
#define PIN_START_1 200
#define PIN_START_2 201
#define PIN_A 215
#define PIN_B 210
#define PIN_X 213
#define PIN_Y 211
#define PIN_R1 212
#define PIN_R2 214
#define PIN_R3 202
#define PIN_R4 207

// SPECIAL PINS.
#define PIN_GROUP_SPECIAL 250
#define PIN_NONE 255
#define PIN_VIRTUAL 254  // Buttons without any hardware associated to them.
