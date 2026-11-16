/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../../common/core/function_flip.h"

struct gmk_flip_output {
    uint32_t key, previous;
    int8_t   brightness;
    uint8_t  remaining_modifiers;
    uint8_t  source;
    bool     emit, release_previous, custom, previous_custom;
};
struct gmk_flip_output gmk_flip_core_event(struct gmk70_flip_state *core, uint32_t encoded, bool pressed, bool mac);
uint8_t                gmk_flip_core_modifiers(const struct gmk70_flip_state *core);
void                   gmk_flip_core_clear(struct gmk70_flip_state *core, uint16_t page);
