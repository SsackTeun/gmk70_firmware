// SPDX-License-Identifier: GPL-2.0-or-later
#include "function_flip.h"
#include "quantum_keycodes.h"

/* QMK consumer keycodes are not HID usages. Keep only native conversion here;
 * stock mapping and held/release state are implemented by the shared core. */
static gmk70_flip_target_t native_target(uint32_t encoded) {
    uint8_t keycode = (uint8_t)encoded;
    if (((encoded >> 16) & 0xffU) == 0x0c) {
        static const struct {
            uint16_t usage;
            uint8_t  keycode;
        } consumers[] = {
            {0x70, KC_BRID}, {0x6f, KC_BRIU}, {0xb6, KC_MPRV}, {0xcd, KC_MPLY}, {0xb5, KC_MNXT}, {0xe2, KC_MUTE}, {0xea, KC_VOLD}, {0xe9, KC_VOLU}, {0x183, KC_MSEL}, {0xb7, KC_MSTP}, {0x18a, KC_MAIL}, {0x223, KC_WHOM}, {0x192, KC_CALC}, {0x221, KC_WSCH},
        };
        keycode = KC_NO;
        for (unsigned i = 0; i < sizeof(consumers) / sizeof(*consumers); ++i)
            if (consumers[i].usage == (uint16_t)encoded) {
                keycode = consumers[i].keycode;
                break;
            }
    }
    return (gmk70_flip_target_t){.keycode = keycode, .modifiers = (uint8_t)(encoded >> 24)};
}

gmk70_flip_output_t gmk70_flip_event(gmk70_flip_state_t *state, uint16_t keycode, bool pressed, bool enabled, bool mac) {
    if (keycode > QK_MODS_MAX) return (gmk70_flip_output_t){0};
    uint8_t                  mods    = QK_MODS_GET_MODS(keycode);
    uint32_t                 encoded = 0x70000U | QK_MODS_GET_BASIC_KEYCODE(keycode) | ((uint32_t)((mods & 0x0f) << ((mods & 0x10) ? 4 : 0)) << 24);
    struct gmk70_flip_result out     = gmk70_flip_transition(state, encoded, pressed, enabled, mac, GMK70_FLIP_NATIVE_WINDOWS_F);
    if (!out.recognized || (!pressed && !out.latched)) return (gmk70_flip_output_t){0};
    return (gmk70_flip_output_t){.target = native_target(out.key), .previous = native_target(out.previous), .brightness = out.brightness, .handled = true, .passthrough = !out.custom, .release_previous = out.release_previous};
}

uint8_t gmk70_flip_held_modifiers(const gmk70_flip_state_t *state) {
    return gmk70_flip_modifiers(state);
}
