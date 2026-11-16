/* Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Canonical HID page/usage with modifiers in the high byte. */
struct gmk70_flip_state {
    uint16_t held[24];
    bool     enabled;
};
struct gmk70_flip_result {
    uint32_t key, previous;
    int8_t   brightness;
    uint8_t  remaining_modifiers, source;
    bool     emit, release_previous, custom, previous_custom, recognized, latched;
};
enum { GMK70_FLIP_NATIVE_WINDOWS_F = 1, GMK70_FLIP_RELEASE_MARKER = 2 };

static inline unsigned gmk70_flip_index(uint32_t key) {
    return key >= 0x7001e && key <= 0x70027 ? key - 0x7001e : key >= 0x7002d && key <= 0x7002e ? key - 0x7002d + 10 : key >= 0x7003a && key <= 0x70045 ? key - 0x7003a + 12 : 24;
}
static inline uint32_t gmk70_flip_mapped(unsigned index, unsigned context, uint8_t mods) {
    static const uint8_t  numbers[12]   = {0x70, 0x6f, 0x2b, 0x08, 0, 0, 0xb6, 0xcd, 0xb5, 0xe2, 0xea, 0xe9};
    static const uint16_t functions[12] = {0x183, 0xea, 0xe9, 0xe2, 0xb7, 0xb6, 0xcd, 0xb5, 0x18a, 0x223, 0x192, 0x221};
    uint32_t              key           = 0x70000U | (index < 10 ? 0x1eU + index : index < 12 ? 0x2dU + index - 10 : 0x3aU + index - 12);
    if (context == 2 && index < 12) key = 0x7003aU + index;
    if (context == 3) {
        if (index == 4 || index == 5) return 0;
        bool keyboard = index == 2 || index == 3;
        key           = (keyboard ? 0x08070000U : 0xc0000U) | (index < 12 ? numbers[index] : functions[index - 12]);
    }
    return key ? key | ((uint32_t)mods << 24) : 0;
}
static inline uint8_t gmk70_flip_modifiers(const struct gmk70_flip_state *core) {
    uint8_t modifiers = 0;
    for (unsigned i = 0; i < 24; ++i) {
        unsigned context = core->held[i] >> 8;
        if (context == 2 || context == 3) modifiers |= (uint8_t)(gmk70_flip_mapped(i, context, (uint8_t)core->held[i]) >> 24);
    }
    return modifiers;
}
static inline void gmk70_flip_clear(struct gmk70_flip_state *core, uint16_t page) {
    for (unsigned i = 0; i < 24; ++i) {
        unsigned context = core->held[i] >> 8;
        if (context != 2 && context != 3) continue;
        unsigned target = (gmk70_flip_mapped(i, context, (uint8_t)core->held[i]) >> 16) & 0xffU;
        if (target == page || (page == 7 && !target))
            core->held[i] = 4U << 8;
        else if (page == 7)
            core->held[i] &= 0xff00U;
    }
}
static inline struct gmk70_flip_result gmk70_flip_transition(struct gmk70_flip_state *core, uint32_t encoded, bool pressed, bool enabled, bool mac, unsigned policy) {
    struct gmk70_flip_result out   = {.key = encoded, .emit = true, .source = 24};
    unsigned                 index = gmk70_flip_index(encoded & 0xffffffU);
    if (index == 24) return out;
    out.recognized          = true;
    out.source              = (uint8_t)index;
    unsigned context        = core->held[index] >> 8;
    uint8_t  mods           = (uint8_t)core->held[index];
    out.latched             = context != 0;
    core->held[index]       = 0;
    out.remaining_modifiers = gmk70_flip_modifiers(core);
    if (pressed) {
        if (context && context != 4) {
            out.previous         = gmk70_flip_mapped(index, context, mods);
            out.release_previous = out.previous != 0;
            out.previous_custom  = context != 1;
        }
        context           = !enabled || (!mac && index >= 12 && (policy & GMK70_FLIP_NATIVE_WINDOWS_F)) ? 1 : mac ? 3 : 2;
        mods              = (uint8_t)(encoded >> 24);
        core->held[index] = (uint16_t)((context << 8) | mods);
    } else {
        if (context == 4) {
            core->held[index] = 4U << 8;
            out.emit          = false;
            return out;
        }
        core->held[index] = context > 1 && (policy & GMK70_FLIP_RELEASE_MARKER) ? 4U << 8 : 0;
        if (!context) return out;
    }
    out.key    = gmk70_flip_mapped(index, context, mods);
    out.emit   = out.key != 0;
    out.custom = context > 1;
    if (pressed && context == 3 && (index == 4 || index == 5)) out.brightness = index == 4 ? -1 : 1;
    return out;
}
