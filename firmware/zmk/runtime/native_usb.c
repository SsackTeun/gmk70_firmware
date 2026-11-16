/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <zmk/usb.h>
#include <zephyr/usb/class/usb_hid.h>

/* Keep the physical-cable wrapper outside the native implementation's LTO. */
enum zmk_usb_conn_state gmk_native_usb_get_conn_state(void);
enum zmk_usb_conn_state zmk_usb_get_conn_state(void) {
    return gmk_native_usb_get_conn_state();
}

extern __typeof__(usb_hid_register_device) gmk_native_usb_hid_register_device;
void                                       usb_hid_register_device(const struct device *device, const uint8_t *descriptor, size_t size, const struct hid_ops *operations) {
    gmk_native_usb_hid_register_device(device, descriptor, size, operations);
}
