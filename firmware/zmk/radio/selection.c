/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../wired/virtual_keys.h"
#include "selection.h"
#include "core.h"
#include <errno.h>

bool gmk_radio_selector_switch(struct gmk_radio_selector *selector, uint8_t raw, uint32_t now, uint8_t last_bt, uint8_t *out) {
    unsigned transition = gmk70_switch_update(&selector->mode, raw, now, GMK70_SWITCH_DEBOUNCE_MS);
    if (transition & GMK70_SWITCH_CANDIDATE) gmk_radio_selector_cancel(selector);
    if (!(transition & GMK70_SWITCH_STABLE)) return false;
    *out = gmk70_switch_host(raw, last_bt);
    return true;
}
int gmk_radio_selector_press(struct gmk_radio_selector *selector, uint8_t channel, uint32_t position, uint32_t now) {
    if (channel > GMK_HOST_DONGLE || position >= GMK_KEY_POSITIONS) return -EINVAL;
    gmk70_hold_start(&selector->hold, position, channel, now);
    selector->hold.active = channel != GMK_HOST_USB;
    return 0;
}
void gmk_radio_selector_release(struct gmk_radio_selector *selector, uint32_t position) {
    gmk70_hold_release(&selector->hold, position);
}
void gmk_radio_selector_cancel(struct gmk_radio_selector *selector) {
    selector->hold.active = false;
}
bool gmk_radio_selector_pair(struct gmk_radio_selector *selector, uint32_t now, uint8_t *out) {
    return gmk70_hold_take(&selector->hold, now, GMK70_HOLD_MS, out);
}
bool gmk_radio_selector_holding(const struct gmk_radio_selector *selector, uint32_t *position, uint8_t *channel) {
    if (!selector->hold.active) return false;
    *position = selector->hold.position;
    *channel  = selector->hold.action;
    return true;
}
