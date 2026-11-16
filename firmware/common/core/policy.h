/* Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "board.h"
#include "radio_commands.h"

/* Board contracts, independent of kernel tick rate and native keycodes. */
enum {
    GMK70_RADIO_BAUD            = 115200,
    GMK70_RADIO_TIMEOUT_MS      = 10,
    GMK70_RADIO_RETRIES         = 50,
    GMK70_BATTERY_QUERY_MS      = 3000,
    GMK70_BATTERY_FRESH_MS      = 10000,
    GMK70_CHARGER_REFRESH_MS    = 1000,
    GMK70_SWITCH_DEBOUNCE_MS    = 50,
    GMK70_HOLD_MS               = 3000,
    GMK70_RECONNECT_MS          = 5000,
    GMK70_RECONNECT_ATTEMPTS    = 3,
    GMK70_DISCONNECTED_SLEEP_MS = 30000,
    GMK70_CRITICAL_HOLD_MS      = 60000,
    GMK70_LOW_BATTERY           = 15,
    GMK70_PHYSICAL_ROWS         = 10,
    GMK70_PHYSICAL_COLS         = 9,
};

enum gmk_host_channel { GMK_HOST_USB, GMK_HOST_BT1, GMK_HOST_BT2, GMK_HOST_BT3, GMK_HOST_BT4, GMK_HOST_BT5, GMK_HOST_DONGLE };
enum gmk_radio_connection { GMK_RADIO_UNKNOWN, GMK_RADIO_PAIRING, GMK_RADIO_CONNECTED, GMK_RADIO_DISCONNECTED, GMK_RADIO_REJECTED };

static inline bool gmk70_elapsed(uint32_t now, uint32_t since, uint32_t interval) {
    return (uint32_t)(now - since) >= interval;
}
static inline bool gmk70_battery_fresh(bool valid, uint8_t percentage, uint32_t age) {
    return valid && percentage <= 100 && age <= GMK70_BATTERY_FRESH_MS;
}
static inline bool gmk70_battery_warning(bool valid, uint8_t percentage, uint32_t age, bool cable) {
    return gmk70_battery_fresh(valid, percentage, age) && percentage <= GMK70_LOW_BATTERY && !cable;
}
static inline uint8_t gmk70_charger_command(bool cable, bool full) {
    return cable ? (full ? MD_SND_CMD_DEVCTRL_CHARGING_DONE : MD_SND_CMD_DEVCTRL_CHARGING) : MD_SND_CMD_DEVCTRL_CHARGING_STOP;
}
static inline uint8_t gmk70_host_command(uint8_t channel) {
    return channel == GMK_HOST_USB ? MD_SND_CMD_DEVCTRL_USB : channel == GMK_HOST_DONGLE ? MD_SND_CMD_DEVCTRL_2G4 : (uint8_t)(MD_SND_CMD_DEVCTRL_2G4 + channel);
}
static inline uint8_t gmk70_switch_host(uint8_t raw, uint8_t last_bt) {
    return raw == 3 ? GMK_HOST_USB : raw == 1 ? GMK_HOST_DONGLE : raw == 2 ? (last_bt >= GMK_HOST_BT1 && last_bt <= GMK_HOST_BT5 ? last_bt : GMK_HOST_BT1) : UINT8_MAX;
}
static inline bool gmk70_inactivity_due(uint32_t now, uint32_t since, uint32_t timeout) {
    return timeout && gmk70_elapsed(now, since, timeout);
}
/* Callers keep their original start-sentinel and admission semantics. */
static inline bool gmk70_critical_due(uint32_t now, uint32_t since, uint8_t percentage) {
    return since && percentage == GMK70_BATTERY_STOP_LEVEL && (uint32_t)(now - since) > GMK70_CRITICAL_HOLD_MS;
}
static inline uint8_t gmk70_radio_connection(uint8_t status) {
    return status >= MD_REV_CMD_DEVCTRL_PAIRING && status <= MD_REV_CMD_DEVCTRL_DISCONNECTED ? (uint8_t)(status - MD_SND_CMD_DEVCTRL_2G4) : status == MD_REV_CMD_DEVCTRL_REJECT ? GMK_RADIO_REJECTED : UINT8_MAX;
}
static inline void gmk70_critical_update(uint32_t *since, bool *active, uint32_t now, bool warning, uint8_t percentage) {
    if (!warning)
        *active = false;
    else if ((!*active || !*since) && percentage == GMK70_BATTERY_STOP_LEVEL) {
        *active = true;
        *since  = now;
    }
}
