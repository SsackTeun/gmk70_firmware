/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <zephyr/settings/settings.h>

/* Native persistence stays behind the existing storage admission wrapper. */
extern __typeof__(settings_save_one) gmk_native_settings_save_one;
int                                  settings_save_one(const char *name, const void *value, size_t length) {
    return gmk_native_settings_save_one(name, value, length);
}
