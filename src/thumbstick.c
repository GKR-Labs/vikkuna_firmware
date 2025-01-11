// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022, Input Labs Oy.

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <pico/stdlib.h>
#include <hardware/adc.h>
#include "config.h"
#include "pin.h"
#include "button.h"
#include "thumbstick.h"
#include "common.h"
#include "hid.h"
#include "led.h"
#include "profile.h"
#include "logging.h"
#include "webusb.h"

float offset_lx = 0;
float offset_ly = 0;
float offset_rx = 0;
float offset_ry = 0;
float config_deadzone = 0;

// Daisywheel.
bool daisywheel_used = false;
Button daisy_a;
Button daisy_b;
Button daisy_x;
Button daisy_y;

float smoothed[4] = {0, 0, 0, 0};

float thumbstick_adc(uint8_t pin, float offset, bool smooth) {
    uint8_t index = pin - 26;
    adc_select_input(index);
    float value = ((float)adc_read() - BIT_11) / BIT_11;
    value = (value - offset) * THUMBSTICK_BASELINE_SATURATION;
    if (smooth) {
        float factor = 8 - (fabs(value - smoothed[index])) * 8;
        factor = constrain(factor, 1, 8);
        value = (value + (smoothed[index] * (factor - 1))) / factor;
        smoothed[index] = value;
    }
    return value;
}

void thumbstick_update_deadzone() {
    uint8_t preset = config_get_deadzone_preset();
    config_deadzone = config_get_deadzone_value(preset);
}

void thumbstick_update_offsets() {
    Config *config = config_read();
    offset_lx = config->offset_ts_lx;
    offset_ry = config->offset_ts_ly;
    offset_lx = config->offset_ts_rx;
    offset_ry = config->offset_ts_ry;
}

void thumbstick_calibrate_each(uint8_t pin_x, uint8_t pin_y, float *result_x, float *result_y) {
    info("Thumbstick: calibrating...\n");
    float x = 0;
    float y = 0;
    for(uint32_t i=0; i<CFG_CALIBRATION_SAMPLES_THUMBSTICK; i++) {
        x += thumbstick_adc(pin_x, 0.0, false);
        y += thumbstick_adc(pin_y, 0.0, false);
    }
    x /= CFG_CALIBRATION_SAMPLES_THUMBSTICK;
    y /= CFG_CALIBRATION_SAMPLES_THUMBSTICK;
    info("Thumbstick: calibration x=%f y=%f\n", x, y);
    *result_x = x;
    *result_y = y;
}

void thumbstick_calibrate() {
    float lx = 0;
    float ly = 0;
    float rx = 0;
    float ry = 0;
    thumbstick_calibrate_each(PIN_THUMBSTICK_LX, PIN_THUMBSTICK_LY, &lx, &ly);
    thumbstick_calibrate_each(PIN_THUMBSTICK_RX, PIN_THUMBSTICK_RY, &rx, &ry);
    config_set_thumbstick_offset(lx, ly, rx, ry);
    thumbstick_update_offsets();
}

void thumbstick_init() {
    info("INIT: Thumbstick\n");
    adc_init();
    adc_gpio_init(PIN_THUMBSTICK_LX);
    adc_gpio_init(PIN_THUMBSTICK_LY);
    adc_gpio_init(PIN_THUMBSTICK_RX);
    adc_gpio_init(PIN_THUMBSTICK_RY);
    thumbstick_update_offsets();
    thumbstick_update_deadzone();
    // Alternative usage of ABXY while doing daisywheel.
    Actions none = {0,};
    daisy_a = Button_(PIN_A, NORMAL, none, none, none);
    daisy_b = Button_(PIN_B, NORMAL, none, none, none);
    daisy_x = Button_(PIN_X, NORMAL, none, none, none);
    daisy_y = Button_(PIN_Y, NORMAL, none, none, none);
}

void thumbstick_report_axis(uint8_t axis, float value) {
    if      (axis == GAMEPAD_AXIS_LX)     hid_gamepad_axis(LX, value);
    else if (axis == GAMEPAD_AXIS_LY)     hid_gamepad_axis(LY, value);
    else if (axis == GAMEPAD_AXIS_RX)     hid_gamepad_axis(RX, value);
    else if (axis == GAMEPAD_AXIS_RY)     hid_gamepad_axis(RY, value);
    else if (axis == GAMEPAD_AXIS_LX_NEG) hid_gamepad_axis(LX, -value);
    else if (axis == GAMEPAD_AXIS_LY_NEG) hid_gamepad_axis(LY, -value);
    else if (axis == GAMEPAD_AXIS_RX_NEG) hid_gamepad_axis(RX, -value);
    else if (axis == GAMEPAD_AXIS_RY_NEG) hid_gamepad_axis(RY, -value);
    else if (axis == GAMEPAD_AXIS_LZ)     hid_gamepad_axis(LZ, value);
    else if (axis == GAMEPAD_AXIS_RZ)     hid_gamepad_axis(RZ, value);
}

uint8_t thumbstick_get_direction(float angle, float overlap) {
    float a = 45 * (1 - overlap);
    float b = 180 - a;
    uint8_t mask = 0;
    if (is_between(angle, -b, -a)) mask += DIR4_MASK_LEFT;
    if (is_between(angle, a, b)) mask += DIR4_MASK_RIGHT;
    if (fabs(angle) <= (90 - a)) mask += DIR4_MASK_UP;
    if (fabs(angle) >= (90 + a)) mask += DIR4_MASK_DOWN;
    return mask;
}

// ============================================================================
// Class.

void Thumbstick__config_4dir(
    Thumbstick *self,
    Button left,
    Button right,
    Button up,
    Button down,
    Button push,
    Button inner,
    Button outer
) {
    self->left = left;
    self->right = right;
    self->up = up;
    self->down = down;
    self->push = push;
    self->inner = inner;
    self->outer = outer;
}

void Thumbstick__config_8dir(
    Thumbstick *self,
    Button left,
    Button right,
    Button up,
    Button down,
    Button ul,
    Button ur,
    Button dl,
    Button dr,
    Button push
) {
    self->left = left;
    self->right = right;
    self->up = up;
    self->down = down;
    self->ul = ul;
    self->ur = ur;
    self->dl = dl;
    self->dr = dr;
    self->push = push;
}

void Thumbstick__report_4dir_axial(
    Thumbstick *self,
    ThumbstickPosition pos
) {
    // Evaluate virtual buttons.
    if (pos.radius > THUMBSTICK_ADDITIONAL_DEADZONE_FOR_BUTTONS) {
        if (pos.radius < THUMBSTICK_INNER_RADIUS) self->inner.virtual_press = true;
        else self->outer.virtual_press = true;
        uint8_t direction = thumbstick_get_direction(pos.angle, self->overlap);
        if (direction & DIR4_MASK_LEFT)  self->left.virtual_press = true;
        if (direction & DIR4_MASK_RIGHT) self->right.virtual_press = true;
        if (direction & DIR4_MASK_UP)    self->up.virtual_press = true;
        if (direction & DIR4_MASK_DOWN)  self->down.virtual_press = true;
    }
    // Report directional virtual buttons or axis.
    //// Left.
    if (!hid_is_axis(self->left.actions[0])) self->left.report(&self->left);
    else thumbstick_report_axis(self->left.actions[0], -constrain(pos.x, -1, 0));
    //// Right.
    if (!hid_is_axis(self->right.actions[0])) self->right.report(&self->right);
    else thumbstick_report_axis(self->right.actions[0], constrain(pos.x, 0, 1));
    //// Up.
    if (!hid_is_axis(self->up.actions[0])) self->up.report(&self->up);
    else thumbstick_report_axis(self->up.actions[0], -constrain(pos.y, -1, 0));
    //// Down.
    if (!hid_is_axis(self->down.actions[0])) self->down.report(&self->down);
    else thumbstick_report_axis(self->down.actions[0], constrain(pos.y, 0, 1));
    // Report inner and outer.
    self->inner.report(&self->inner);
    self->outer.report(&self->outer);
    // Report push.
    self->push.report(&self->push);
}

void Thumbstick__report_8dir(
    Thumbstick *self,
    ThumbstickPosition pos
) {
    // Evaluate virtual buttons.
    if (pos.radius > THUMBSTICK_ADDITIONAL_DEADZONE_FOR_BUTTONS) {
        uint8_t direction = thumbstick_get_direction(pos.angle, self->overlap);
        if      (direction == DIR4_MASK_LEFT)  self->left.virtual_press = true;
        else if (direction == DIR4_MASK_RIGHT) self->right.virtual_press = true;
        else if (direction == DIR4_MASK_UP)    self->up.virtual_press = true;
        else if (direction == DIR4_MASK_DOWN)  self->down.virtual_press = true;
        else if (direction == (DIR4_MASK_UP   + DIR4_MASK_LEFT))  self->ul.virtual_press = true;
        else if (direction == (DIR4_MASK_UP   + DIR4_MASK_RIGHT)) self->ur.virtual_press = true;
        else if (direction == (DIR4_MASK_DOWN + DIR4_MASK_LEFT))  self->dl.virtual_press = true;
        else if (direction == (DIR4_MASK_DOWN + DIR4_MASK_RIGHT)) self->dr.virtual_press = true;
    }
    // Report directional virtual buttons.
    self->left.report(&self->left);
    self->right.report(&self->right);
    self->up.report(&self->up);
    self->down.report(&self->down);
    self->ul.report(&self->ul);
    self->ur.report(&self->ur);
    self->dl.report(&self->dl);
    self->dr.report(&self->dr);
    // Report push.
    self->push.report(&self->push);
}

void Thumbstick__report_4dir_radial(Thumbstick *self, ThumbstickPosition pos) {
    uint8_t direction = thumbstick_get_direction(pos.angle, self->overlap);
    thumbstick_report_axis(self->left.actions[0],  (direction & DIR4_MASK_LEFT)  ? pos.radius : 0);
    thumbstick_report_axis(self->right.actions[0], (direction & DIR4_MASK_RIGHT) ? pos.radius : 0);
    thumbstick_report_axis(self->up.actions[0],    (direction & DIR4_MASK_UP)    ? pos.radius : 0);
    thumbstick_report_axis(self->down.actions[0],  (direction & DIR4_MASK_DOWN)  ? pos.radius : 0);
    self->push.report(&self->push);
}

void Thumbstick__config_glyphstick(Thumbstick *self, Actions actions, Glyph glyph) {
    uint8_t index = self->glyphstick_index;
    memcpy(self->glyphstick_actions[index], actions, 4);
    memcpy(self->glyphstick_glyphs[index], glyph, 5);
    self->glyphstick_index += 1;
}

void Thumbstick__report_glyphstick(Thumbstick *self, Glyph input) {
    bool matched = false;
    // Iterate over all defined glyphs.
    uint8_t nglyphs = self->glyphstick_index;
    for(uint8_t i=0; i<nglyphs; i++) {
        // Pattern match user input against glyph.
        bool match = true;
        for(uint8_t j=0; j<5; j++) {
            if (input[j] != self->glyphstick_glyphs[i][j]) {
                match = false;
                break;
            }
        }
        // Trigger actions if matches.
        if (match) {
            hid_press_multiple(self->glyphstick_actions[i]);
            hid_release_multiple_later(self->glyphstick_actions[i], 100);
            break;
        }
    }
}

void Thumbstick__config_daisywheel(Thumbstick *self, uint8_t dir, uint8_t button, Actions actions) {
    memcpy(self->daisywheel[dir][button], actions, 4);
}

void Thumbstick__report_daisywheel(Thumbstick *self, Dir8 dir) {
    dir -= 1;  // Shift zero since not using center direction here.
    if (daisy_a.is_pressed(&daisy_a)) {
        hid_press_multiple(self->daisywheel[dir][0]);
        hid_release_multiple_later(self->daisywheel[dir][0], 10);
        daisywheel_used=true;
    }
    else if (daisy_b.is_pressed(&daisy_b)) {
        hid_press_multiple(self->daisywheel[dir][1]);
        hid_release_multiple_later(self->daisywheel[dir][1], 10);
        daisywheel_used=true;
    }
    else if (daisy_x.is_pressed(&daisy_x)) {
        hid_press_multiple(self->daisywheel[dir][2]);
        hid_release_multiple_later(self->daisywheel[dir][2], 10);
        daisywheel_used=true;
    }
    else if (daisy_y.is_pressed(&daisy_y)) {
        hid_press_multiple(self->daisywheel[dir][3]);
        hid_release_multiple_later(self->daisywheel[dir][3], 10);
        daisywheel_used=true;
    }
}

void Thumbstick__report_alphanumeric(Thumbstick *self, ThumbstickPosition pos) {
    static Glyph input = {0};
    static uint8_t input_index = 0;
    static float CUT4 = 45;
    static float CUT4X = 135;  // 180-45
    static float CUT8 = 22.5;
    Dir4 dir4 = 0;
    Dir8 dir8 = 0;
    if (pos.radius > 0.7) {
        profile_enable_abxy(false);
        // Detect direction 4.
        if      (is_between(pos.angle, -CUT4X, -CUT4)) dir4 = DIR4_LEFT;
        else if (is_between(pos.angle,  CUT4,  CUT4X)) dir4 = DIR4_RIGHT;
        else if (fabs(pos.angle) <= 90 - CUT4)         dir4 = DIR4_UP;
        else if (fabs(pos.angle) >= 90 + CUT4)         dir4 = DIR4_DOWN;
        // Detect direction 8.
        if      (is_between(pos.angle, -CUT8*1,  CUT8*1)) dir8 = DIR8_UP;
        else if (is_between(pos.angle,  CUT8*1,  CUT8*3)) dir8 = DIR8_UP_RIGHT;
        else if (is_between(pos.angle,  CUT8*3,  CUT8*5)) dir8 = DIR8_RIGHT;
        else if (is_between(pos.angle,  CUT8*5,  CUT8*7)) dir8 = DIR8_DOWN_RIGHT;
        else if (is_between(pos.angle, -CUT8*7, -CUT8*5)) dir8 = DIR8_DOWN_LEFT;
        else if (is_between(pos.angle, -CUT8*5, -CUT8*3)) dir8 = DIR8_LEFT;
        else if (is_between(pos.angle, -CUT8*3, -CUT8*1)) dir8 = DIR8_UP_LEFT;
        else if (fabs(pos.angle) >= CUT8*7)               dir8 = DIR8_DOWN;
        // Record direction 4.
        if (input_index == 0 || dir4 != input[input_index-1]) {
            input[input_index] = dir4;
            input_index += 1;
        }
        // Report daisy keyboard.
        self->report_daisywheel(self, dir8);
    } else {
        if (input_index > 0) {
            // Glyph-stick match.
            if (!daisywheel_used) {
                self->report_glyphstick(self, input);
            }
            // Glyph-stick reset.
            memset(input, 0, 5);
            input_index = 0;
            // Daisywheel reset.
            daisywheel_used = false;
            profile_enable_abxy(true);
        }
    }
}

void Thumbstick__report(Thumbstick *self) {
    float offset_x = self->index==1 ? offset_lx : offset_rx;
    float offset_y = self->index==1 ? offset_ly : offset_ry;
    // Do not report if not calibrated.
    if (offset_x == 0 && offset_y == 0) return;
    // Get values from ADC.
    float x = thumbstick_adc(self->pin_x, offset_x, true);
    float y = thumbstick_adc(self->pin_y, offset_y, true);
    x /= self->saturation;
    y /= self->saturation;
    x = constrain(x, -1, 1) * (self->invert_x? -1 : 1);
    y = constrain(y, -1, 1) * (self->invert_y? -1 : 1);
    // Get correct deadzone.
    float deadzone = self->deadzone_override ? self->deadzone : config_deadzone;
    // Calculate trigonometry.
    float angle = atan2(x, -y) * (180 / M_PI);
    float radius = sqrt(powf(x, 2) + powf(y, 2));
    radius = constrain(radius, 0, 1);
    if (radius < deadzone) {
        radius = 0;
    } else {
        radius = ramp_low(radius, deadzone);
        radius = ramp_inv(radius, self->antideadzone);
    }
    x = sin(radians(angle)) * radius;
    y = -cos(radians(angle)) * radius;
    ThumbstickPosition pos = {x, y, angle, radius};
    // Report.
    if (self->mode == THUMBSTICK_MODE_4DIR) {
        if (self->distance_mode == THUMBSTICK_DISTANCE_AXIAL) {
            self->report_4dir_axial(self, pos);
        }
        if (self->distance_mode == THUMBSTICK_DISTANCE_RADIAL) {
            self->report_4dir_radial(self, pos);
        }
    }
    else if (self->mode == THUMBSTICK_MODE_8DIR) {
        self->report_8dir(self, pos);
    }
    else if (self->mode == THUMBSTICK_MODE_ALPHANUMERIC) {
        self->report_alphanumeric(self, pos);
    }
}

void Thumbstick__reset(Thumbstick *self) {
    if (self->mode == THUMBSTICK_MODE_4DIR) {
        self->left.reset(&self->left);
        self->right.reset(&self->right);
        self->up.reset(&self->up);
        self->down.reset(&self->down);
        self->push.reset(&self->push);
        self->inner.reset(&self->inner);
        self->outer.reset(&self->outer);
    }
}

Thumbstick Thumbstick_ (
    uint8_t index,
    uint8_t pin_x,
    uint8_t pin_y,
    bool invert_x,
    bool invert_y,
    ThumbstickMode mode,
    ThumbstickDistance distance_mode,
    bool deadzone_override,
    float deadzone,
    float antideadzone,
    float overlap,
    float saturation
) {
    Thumbstick thumbstick;
    // Methods.
    thumbstick.report = Thumbstick__report;
    thumbstick.report_4dir_axial = Thumbstick__report_4dir_axial;
    thumbstick.report_4dir_radial = Thumbstick__report_4dir_radial;
    thumbstick.report_8dir = Thumbstick__report_8dir;
    thumbstick.report_alphanumeric = Thumbstick__report_alphanumeric;
    thumbstick.reset = Thumbstick__reset;
    thumbstick.config_4dir = Thumbstick__config_4dir;
    thumbstick.config_8dir = Thumbstick__config_8dir;
    thumbstick.config_glyphstick = Thumbstick__config_glyphstick;
    thumbstick.report_glyphstick = Thumbstick__report_glyphstick;
    thumbstick.config_daisywheel = Thumbstick__config_daisywheel;
    thumbstick.report_daisywheel = Thumbstick__report_daisywheel;
    // Attributes.
    thumbstick.index = index;
    thumbstick.pin_x = pin_x;
    thumbstick.pin_y = pin_y;
    thumbstick.invert_x = invert_x;
    thumbstick.invert_y = invert_y;
    thumbstick.mode = mode;
    thumbstick.distance_mode = distance_mode;
    thumbstick.deadzone_override = deadzone_override;
    thumbstick.deadzone = deadzone;
    thumbstick.antideadzone = antideadzone;
    thumbstick.overlap = overlap;
    thumbstick.saturation = saturation;
    thumbstick.glyphstick_index = 0;
    return thumbstick;
}
