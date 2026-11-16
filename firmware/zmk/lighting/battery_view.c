/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../wired/virtual_keys.h"
#include "battery_view.h"
#include "../../common/core/indicators.h"
#include "../battery/battery.h"
#include <errno.h>
#include <string.h>

static bool held(const struct gmk_battery_view *view, enum gmk_battery_indicator kind) {
    return gmk_battery_holds_active(&view->holds, kind == GMK_BATTERY_LEVEL ? GMK_BATTERY_ACTION_LEVEL : GMK_BATTERY_ACTION_CHARGING);
}

int gmk_battery_view_set(struct gmk_battery_view *view, enum gmk_battery_indicator kind, uint32_t position, bool pressed) {
    if ((unsigned)kind > GMK_BATTERY_CHARGING || position >= GMK_KEY_POSITIONS) return -EINVAL;
    (void)gmk_battery_holds_set(&view->holds, kind == GMK_BATTERY_LEVEL ? GMK_BATTERY_ACTION_LEVEL : GMK_BATTERY_ACTION_CHARGING, position, pressed);
    return 0;
}

bool gmk_battery_view_active(const struct gmk_battery_view *view) {
    return held(view, GMK_BATTERY_LEVEL) || held(view, GMK_BATTERY_CHARGING);
}

bool gmk_battery_view_release(struct gmk_battery_view *view, uint32_t position) {
    if (position >= GMK_KEY_POSITIONS) return false;
    return gmk_battery_holds_release(&view->holds, position);
}

static void color(uint8_t *rgb, unsigned index, uint8_t r, uint8_t g, uint8_t b) {
    rgb[3 * index]     = r;
    rgb[3 * index + 1] = g;
    rgb[3 * index + 2] = b;
}
bool gmk_battery_warning_active(const struct gmk_battery_status *status) {
    return gmk70_battery_warning(status->valid, status->percentage, status->age_ms, status->cable);
}
void gmk_battery_warning_overlay(const struct gmk_battery_status *status, uint8_t pixels[3 * GMK_LIGHT_PIXELS], uint32_t now) {
    if (gmk_battery_warning_active(status)) color(pixels, GMK70_BATTERY_LED, (now / 250U) % 2U == 0 ? 80 : 0, 0, 0);
}

void gmk_battery_view_overlay(const struct gmk_battery_view *view, const struct gmk_battery_status *status, uint8_t pixels[3 * GMK_LIGHT_PIXELS]) {
    if (held(view, GMK_BATTERY_LEVEL)) {
        static const uint8_t indices[10] = GMK70_BATTERY_LED_INDICES;
        memset(pixels, 0, 3 * GMK_LIGHT_PIXELS);
        for (unsigned i = 0; i < 10; ++i) {
            struct gmk70_color segment = gmk70_battery_segment(status->percentage, status->valid, i, 80);
            color(pixels, indices[i], segment.r, segment.g, segment.b);
        }
    }
    if (held(view, GMK_BATTERY_CHARGING) && status->cable) {
        struct gmk70_color charging = gmk70_charging_color(status->full);
        color(pixels, GMK70_BATTERY_LED, charging.r, charging.g, charging.b);
    }
}
