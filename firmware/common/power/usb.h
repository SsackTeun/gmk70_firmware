/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../core/selection.h"

/* Physical cable policy only. Native adapters own enumeration, queues, clocks
 * and the external pull-up. Never infer cable presence from the output mode. */
static inline int gmk70_usb_action(struct gmk70_switch *cable, bool present, bool enabled, uint32_t now) {
    unsigned transition = gmk70_switch_update(cable, present, now, GMK70_SWITCH_DEBOUNCE_MS);
    if ((transition & GMK70_SWITCH_CANDIDATE) || !cable->have_stable || cable->candidate_raw != cable->stable_raw) return 0;
    if ((cable->stable_raw != 0) == enabled) return 0;
    return cable->stable_raw ? 1 : -1;
}
