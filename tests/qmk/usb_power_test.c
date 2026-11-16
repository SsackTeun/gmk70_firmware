/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "usb_main.h"
#include "wireless.h"
#include "usb_power.h"

USBDriver           USBD1;
static uint32_t     now;
static bool         cable, pullup, output, input;
static lpwr_state_t power;
static unsigned     initialized, restarted, stopped;
void                __wrap_init_usb_driver(USBDriver *driver);
uint32_t            timer_read32(void) {
    return now;
}
bool gpio_read_pin(unsigned pin) {
    assert(pin == HS_BAT_CABLE_PIN);
    return cable;
}
void gpio_write_pin_high(unsigned pin) {
    assert(pin == USB_POWER_EN_PIN && !initialized);
    pullup = false;
}
void gpio_set_pin_output(unsigned pin) {
    assert(pin == USB_POWER_EN_PIN && !pullup && !initialized);
    output = true;
}
void gpio_set_pin_input(unsigned pin) {
    assert(pin == HS_BAT_CABLE_PIN && output && !initialized);
    input = true;
}
lpwr_state_t lpwr_get_state(void) {
    return power;
}
void usb_power_connect(void) {
    assert(cable);
    pullup = true;
}
void usb_power_disconnect(void) {
    pullup = false;
}
void usbDisconnectBus(USBDriver *driver) {
    assert(driver == &USBD1 && !pullup);
}
void usbStop(USBDriver *driver) {
    assert(driver == &USBD1);
    driver->state = USB_STOP;
    ++stopped;
}
void __real_init_usb_driver(USBDriver *driver) {
    assert(input && output && !pullup);
    ++initialized;
    driver->state = USB_READY;
}
void restart_usb_driver(USBDriver *driver) {
    assert(initialized == 1 && !pullup);
    ++restarted;
    driver->state = USB_READY;
}

int main(int argc, char **argv) {
    (void)argv;
    cable = argc > 1;
    __wrap_init_usb_driver(&USBD1);
    if (cable) {
        assert(initialized == 1 && !stopped && USBD1.state == USB_READY && pullup);
        cable = false;
        gmk70_usb_power_poll();
        now += 50;
        gmk70_usb_power_poll();
    }
    assert(initialized == 1 && stopped == 1 && USBD1.state == USB_STOP && !pullup);
    for (now = 0; now < 10000; ++now)
        gmk70_usb_power_poll();
    assert(!restarted);
    cable = true;
    gmk70_usb_power_poll();
    now += 49;
    gmk70_usb_power_poll();
    assert(!restarted);
    ++now;
    gmk70_usb_power_poll();
    assert(restarted == 1 && pullup);
    /* Output mode is not an admission input; USB stays usable in wireless mode. */
    for (unsigned mode = 0; mode < 7; ++mode) {
        ++now;
        gmk70_usb_power_poll();
    }
    assert(restarted == 1 && stopped == 1);
    cable = false;
    gmk70_usb_power_poll();
    now += 10;
    cable = true;
    gmk70_usb_power_poll();
    now += 50;
    gmk70_usb_power_poll();
    assert(stopped == 1);
    cable = false;
    gmk70_usb_power_poll();
    now += 50;
    gmk70_usb_power_poll();
    assert(stopped == 2 && !pullup);
    power = LPWR_STOP;
    cable = true;
    now += 100;
    gmk70_usb_power_poll();
    assert(restarted == 1);
    power = LPWR_NORMAL;
    now   = UINT32_MAX - 20;
    gmk70_usb_power_poll();
    now += 50;
    gmk70_usb_power_poll();
    assert(restarted == 2 && pullup);
    puts("QMK USB lifecycle: cable-only admission, bounce, wrap, native restart and full-sleep exclusion passed");
}
