/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint8_t state;
} USBDriver;
enum { USB_STOP, USB_READY, USB_ACTIVE, USB_SUSPENDED };
extern USBDriver USBD1;
#define USB_DRIVER USBD1
#define HS_BAT_CABLE_PIN 7
#define USB_POWER_EN_PIN 1
bool gpio_read_pin(unsigned pin);
void gpio_write_pin_high(unsigned pin);
void gpio_set_pin_output(unsigned pin);
void gpio_set_pin_input(unsigned pin);
void usbDisconnectBus(USBDriver *driver);
void usbStop(USBDriver *driver);
void restart_usb_driver(USBDriver *driver);
