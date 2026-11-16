/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "quantum.h"
#include "usb_main.h"
#include "wireless.h"
#include "usb_power.h"
#include "../../../../common/power/usb.h"
#ifdef PROTOCOL_CHIBIOS
_Static_assert(WB32_USBCLK == 48000000, "USB must retain its 48 MHz clock");
#endif

static struct gmk70_switch cable;
void                       __real_init_usb_driver(USBDriver *driver);

static void stop_usb(USBDriver *driver) {
    usb_power_disconnect();
    usbDisconnectBus(driver);
    usbStop(driver);
}

void __wrap_init_usb_driver(USBDriver *driver) {
    gpio_write_pin_high(USB_POWER_EN_PIN);
    gpio_set_pin_output(USB_POWER_EN_PIN);
    gpio_set_pin_input(HS_BAT_CABLE_PIN);
    /* Native initialization creates the endpoint queues exactly once. Native
     * restart owns their cancellation/reinitialization after cable insertion. */
    __real_init_usb_driver(driver);
    if (!gpio_read_pin(HS_BAT_CABLE_PIN))
        stop_usb(driver);
    else
        usb_power_connect();
}

void gmk70_usb_power_poll(void) {
    if (lpwr_get_state() != LPWR_NORMAL) return;
    int action = gmk70_usb_action(&cable, gpio_read_pin(HS_BAT_CABLE_PIN), USB_DRIVER.state != USB_STOP, timer_read32());
    if (action < 0) {
        stop_usb(&USB_DRIVER);
    } else if (action > 0) {
        restart_usb_driver(&USB_DRIVER);
        usb_power_connect();
    }
}
