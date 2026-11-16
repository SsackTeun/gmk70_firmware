/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include "../../../../common/power/usb.h"
#include "usb_power_gmk70.h"
#ifdef CONFIG_GMK70_POWER
#    include "../../../power/power.h"
#endif

/* qmk-common/keyboards/zuoya/gmk70/config.h: USB_POWER_EN_PIN B1.
 * qmk-common/drivers/wireless/transport.c identifies this active-low control
 * as power to the external USB D+ pull-up resistor. */
#define GMK70_USB_POWER_PIN 1
static const struct device *const port       = DEVICE_DT_GET(DT_NODELABEL(gpiob));
static const struct device *const cable_port = DEVICE_DT_GET(DT_NODELABEL(gpioa));
static struct gmk70_switch        cable;
/* Native ZMK initializes the core/controller before static workers start. */
static bool enabled = true;

static bool cable_present(void) {
#ifdef CONFIG_GMK70_POWER
    return gmk_power_usb_powered();
#else
    return gpio_pin_get_raw(cable_port, 7) > 0;
#endif
}

__attribute__((noipa)) void gmk_usb_power_poll(void) {
    int action = gmk70_usb_action(&cable, cable_present(), enabled, k_uptime_get_32());
    if (action > 0) {
        if (!usb_dc_attach()) {
            enabled = true;
        }
    } else if (action < 0) {
        /* Remove the external pull-up before the controller parks D+ low. */
        gpio_pin_set_raw(port, GMK70_USB_POWER_PIN, 1);
        /* Native DISCONNECTED handling cancels transfers and disables class
         * endpoints. Retain the initialized core/EP0 callbacks for reattach. */
        if (!usb_dc_detach()) enabled = false;
    }
    /* Startup leaves the pull-up off until cable presence is stable, even
     * though the unchanged native initializer has already attached USB. */
    if (enabled && cable.have_stable && cable.stable_raw) gpio_pin_set_raw(port, GMK70_USB_POWER_PIN, 0);
}

static int usb_power_init(void) {
    if (!device_is_ready(port) || !device_is_ready(cable_port)) return -ENODEV;
    int result = gpio_pin_configure(cable_port, 7, GPIO_INPUT);
    if (result) return result;
    return gpio_pin_configure(port, GMK70_USB_POWER_PIN, GPIO_OUTPUT_HIGH);
}

SYS_INIT(usb_power_init, POST_KERNEL, 88);
