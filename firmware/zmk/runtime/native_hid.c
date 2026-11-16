/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <zmk/keys.h>
#include <zmk/hid.h>

/* Non-LTO public ABI thunks preserve the existing GNU --wrap boundaries.
 * Reports and modifier algorithms remain in the unmodified native source. */
void gmk_native_hid_keyboard_clear(void);
void zmk_hid_keyboard_clear(void) {
    gmk_native_hid_keyboard_clear();
}
void gmk_native_hid_consumer_clear(void);
void zmk_hid_consumer_clear(void) {
    gmk_native_hid_consumer_clear();
}
int gmk_native_hid_implicit_modifiers_press(zmk_mod_flags_t modifiers);
int zmk_hid_implicit_modifiers_press(zmk_mod_flags_t modifiers) {
    return gmk_native_hid_implicit_modifiers_press(modifiers);
}
int gmk_native_hid_implicit_modifiers_release(void);
int zmk_hid_implicit_modifiers_release(void) {
    return gmk_native_hid_implicit_modifiers_release();
}
int gmk_native_hid_press(uint32_t usage);
int zmk_hid_press(uint32_t usage) {
    return gmk_native_hid_press(usage);
}
int gmk_native_hid_release(uint32_t usage);
int zmk_hid_release(uint32_t usage) {
    return gmk_native_hid_release(usage);
}
struct zmk_hid_keyboard_report *gmk_native_hid_get_keyboard_report(void);
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) {
    return gmk_native_hid_get_keyboard_report();
}
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT) && !IS_ENABLED(CONFIG_GMK70_NKRO)
/* GMK70 NKRO owns its existing bitmap-based boot projection and never calls
 * the native held-counter projection. Do not retain that unused path. */
zmk_hid_boot_report_t *gmk_native_hid_get_boot_report(void);
zmk_hid_boot_report_t *zmk_hid_get_boot_report(void) {
    return gmk_native_hid_get_boot_report();
}
#endif
#if IS_ENABLED(CONFIG_ZMK_POINTING)
void gmk_native_hid_mouse_clear(void);
void zmk_hid_mouse_clear(void) {
    gmk_native_hid_mouse_clear();
}
#endif
