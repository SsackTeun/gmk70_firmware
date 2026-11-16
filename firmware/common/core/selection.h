/* Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "policy.h"
#include "hold.h"

struct gmk70_switch {
    uint32_t candidate_at;
    uint8_t  candidate_raw, stable_raw;
    bool     have_candidate, have_stable;
};
enum { GMK70_SWITCH_CANDIDATE = 1, GMK70_SWITCH_STABLE = 2 };

/* Raw PCB selector: 3 USB, 2 Bluetooth, 1 dongle, 0 disconnected/off.
 * Return transitions, not a command: adapters own native host switching. */
static inline unsigned gmk70_switch_update(struct gmk70_switch *state, uint8_t raw, uint32_t now, uint32_t debounce) {
    if (raw > 3) {
        state->have_candidate = false;
        return GMK70_SWITCH_CANDIDATE;
    }
    if (!state->have_candidate || state->candidate_raw != raw) {
        state->have_candidate = true;
        state->candidate_raw  = raw;
        state->candidate_at   = now;
        return GMK70_SWITCH_CANDIDATE;
    }
    if (!gmk70_elapsed(now, state->candidate_at, debounce) || (state->have_stable && state->stable_raw == raw)) return 0;
    state->have_stable = true;
    state->stable_raw  = raw;
    return GMK70_SWITCH_STABLE;
}
