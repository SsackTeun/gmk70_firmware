/* Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "policy.h"

struct gmk70_color {
    uint8_t r, g, b;
};

/* A logical segment is converted to a native LED index by the adapter. */
static inline struct gmk70_color gmk70_battery_segment(uint8_t percentage, bool valid, unsigned segment, uint8_t brightness) {
    if (!valid || percentage > 100) return (struct gmk70_color){24, 0, 24};
    if (segment && segment >= percentage / 10U) return (struct gmk70_color){0};
    return percentage < 30 ? (struct gmk70_color){brightness, 0, 0} : (struct gmk70_color){0, brightness, 0};
}
static inline struct gmk70_color gmk70_charging_color(bool full) {
    return full ? (struct gmk70_color){0, 80, 0} : (struct gmk70_color){80, 0, 0};
}
static inline unsigned gmk70_radio_led(uint8_t channel) {
    return channel >= GMK_HOST_BT1 && channel <= GMK_HOST_BT3 ? 15U - channel : channel >= GMK_HOST_BT4 && channel <= GMK_HOST_BT5 ? 37U + channel : channel == GMK_HOST_DONGLE ? GMK70_DONGLE_LED : UINT8_MAX;
}
static inline struct gmk70_color gmk70_radio_color(uint8_t channel, uint8_t brightness) {
    return channel == GMK_HOST_DONGLE ? (struct gmk70_color){0, brightness, 0} : (struct gmk70_color){0, 0, brightness};
}
static inline uint32_t gmk70_radio_blink_interval(bool pairing) {
    return pairing ? 200U : 500U;
}
/* 0 ends the overlay; 1 and 2 explicitly draw lit/dark phases. */
static inline uint8_t gmk70_blink_phase(uint32_t elapsed, uint32_t interval, uint32_t duration) {
    return duration && elapsed >= duration ? 0 : (uint8_t)(1U + elapsed / interval % 2U);
}
