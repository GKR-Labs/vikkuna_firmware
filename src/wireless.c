// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022, Input Labs Oy.

#include <stdio.h>
#include <string.h>
#include <pico/time.h>
#include <hardware/uart.h>
#include "wireless.h"
#include "config.h"
#include "pin.h"
#include "led.h"
#include "bus.h"
#include "hid.h"
#include "loop.h"
#include "logging.h"
#include "common.h"
#include "uart.h"
#include "esp.h"

static bool uart_data_mode = false;

void wireless_send_hid(uint8_t report_id, void *payload, uint8_t len) {
    uint8_t message[36] = {UART_CONTROL_BYTES, AT_HID, report_id,};
    memcpy(&message[5], payload, len);
    uart_write_blocking(ESP_UART, message, 36);
}

void wireless_set_uart_data_mode(bool mode) {
    info("RF: data_mode=%i\n", mode);
    uart_data_mode = mode;
    if (mode) {
        esp_restart();
        uart_deinit(ESP_UART);
        uart_init(ESP_UART, ESP_DATA_BAUD);
        info("RF: UART1 init (%i)\n", ESP_DATA_BAUD);
        uart_rx_buffer_init();
        irq_set_exclusive_handler(UART1_IRQ, uart_rx_irq_callback);
        irq_set_enabled(UART1_IRQ, true);
        uart_set_irq_enables(ESP_UART, true, false);  // RX - TX
    } else {
        uart_deinit(ESP_UART);
        uart_init(ESP_UART, ESP_BOOTLOADER_BAUD);
        info("RF: UART1 init (%i)\n", ESP_BOOTLOADER_BAUD);
    }
}

void wireless_init() {
    #ifdef DEVICE_HAS_MARMOTA
        info("RF: Init\n");
        // Prepare ESP.
        esp_init();
        // Secondary UART.
        info("RF: UART1 init (%i)\n", ESP_BOOTLOADER_BAUD);
        uart_init(ESP_UART, ESP_BOOTLOADER_BAUD);
        gpio_set_function(PIN_UART1_TX, GPIO_FUNC_UART);
        gpio_set_function(PIN_UART1_RX, GPIO_FUNC_UART);
    #endif
}

void wireless_cross_logging() {
    // Redirect incomming ESP UART logging to main UART.
    while(uart_is_readable(ESP_UART)) {
        char c = uart_getc(ESP_UART);
        info("%c", c);
    }
}

void wireless_uart_commands() {
    static uint8_t i = 0;
    static uint8_t command = 0;
    static uint8_t payload[68] = {0,};
    while(!uart_rx_buffer_is_empty()) {
        char c = uart_rx_buffer_getc();
        // Check control bytes.
        if (i < 3) {
            if ((i==0 && c==30) || (i==1 && c==29) || (i==2 && c==28))  {
                i += 1;
            } else {
                i = 0;
                // Redirect to RP2040 uart log.
                info("%c", c);
            }
        }
        // Get AT command.
        else if (i == 3) {
            if (c >= AT_HID && c <= AT_BATTERY) {
                command = c;
                i += 1;
            } else {
                i = 0;
                warn("UART: AT command unknown %i\n", c);
            }
        }
        // Get payload.
        else {
            payload[i-4] = c;
            i += 1;
            // Payload complete.
            if (command==AT_HID && i==4+32) {
                hid_report_dongle(payload[0], &payload[1]);
                i = 0;
            }
            else if (command==AT_WEBUSB && i==4+64) {
                // hid_report_dongle(payload[0], &payload[1]);
                i = 0;
            }
            else if (command==AT_BATTERY && i==4+4) {
                #ifdef DEVICE_ALPAKKA_V1
                    uint32_t battery_level = 0;
                    memcpy(&battery_level, payload, 4);
                    // info("BATT %lu\n", battery_level);
                    if (battery_level < BATTERY_LOW_THRESHOLD) {
                        loop_set_battery_low(true);
                        static bool battery_low_was_triggered = false;
                        if (!battery_low_was_triggered) {
                            config_set_problem_battery_low(true);
                            battery_low_was_triggered = true;
                        }
                    }
                #endif
                i = 0;
            }
            else if (i==4+64) {
                i = 0;
            }
        }
    }
}

void wireless_controller_task() {
    hid_report_wireless();
    // if (!uart_data_mode) wireless_cross_logging();
    wireless_uart_commands();
}

void wireless_dongle_task() {
    // led_task();
    if (!uart_data_mode) wireless_cross_logging();
    else wireless_uart_commands();
}
