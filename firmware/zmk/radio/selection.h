/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../../qmk-common/core/selection.h"

/* Zero initialization is sufficient; all times use wrapping millisecond ticks. */
struct gmk_radio_selector {
    struct gmk70_switch mode;
    struct gmk70_hold   hold;
};
/* Invalid raw values cancel pairing and restart debounce without emitting. */
bool gmk_radio_selector_switch(struct gmk_radio_selector *selector, uint8_t raw, uint32_t now, uint8_t last_bt, uint8_t *out);
int  gmk_radio_selector_press(struct gmk_radio_selector *selector, uint8_t channel, uint32_t position, uint32_t now);
void gmk_radio_selector_release(struct gmk_radio_selector *selector, uint32_t position);
void gmk_radio_selector_cancel(struct gmk_radio_selector *selector);
bool gmk_radio_selector_pair(struct gmk_radio_selector *selector, uint32_t now, uint8_t *out);
bool gmk_radio_selector_holding(const struct gmk_radio_selector *selector, uint32_t *position, uint8_t *channel);
