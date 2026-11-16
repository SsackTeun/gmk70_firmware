/* Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "policy.h"

struct gmk70_hold {
    uint32_t since;
    uint32_t position;
    uint8_t  action;
    bool     active;
};
static inline void gmk70_hold_start(struct gmk70_hold *hold, unsigned position, uint8_t action, uint32_t now) {
    *hold = (struct gmk70_hold){.since = now, .position = position, .action = action, .active = true};
}
static inline void gmk70_hold_release(struct gmk70_hold *hold, unsigned position) {
    if (hold->active && hold->position == position) hold->active = false;
}
static inline bool gmk70_hold_take(struct gmk70_hold *hold, uint32_t now, uint32_t duration, uint8_t *action) {
    if (!hold->active || !gmk70_elapsed(now, hold->since, duration)) return false;
    *action      = hold->action;
    hold->active = false;
    return true;
}
