/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "function_flip.h"
#include "quantum_keycodes.h"
#include "../../firmware/zmk/function_flip/core.h"

static void paired_trace(uint16_t source, uint32_t hid, uint8_t expected_native, uint32_t expected_hid, uint8_t mods, bool mac, int8_t brightness) {
    gmk70_flip_state_t      qmk   = {0};
    struct gmk70_flip_state zmk   = {.enabled = true};
    gmk70_flip_output_t     qdown = gmk70_flip_event(&qmk, source, true, true, mac);
    struct gmk_flip_output  zdown = gmk_flip_core_event(&zmk, hid, true, mac);
    assert(qdown.handled && qdown.target.keycode == expected_native && qdown.target.modifiers == mods);
    assert(zdown.key == expected_hid && zdown.emit == (expected_hid != 0));
    assert(qdown.brightness == brightness && zdown.brightness == brightness);
    /* Changing settings/OS while a key is held must not change its release. */
    zmk.enabled                = false;
    gmk70_flip_output_t    qup = gmk70_flip_event(&qmk, source, false, false, !mac);
    struct gmk_flip_output zup = gmk_flip_core_event(&zmk, hid, false, !mac);
    assert(qup.handled && qup.target.keycode == expected_native && qup.target.modifiers == mods);
    assert(zup.key == expected_hid && zup.emit == (expected_hid != 0));
    assert(!qup.brightness && !zup.brightness);
}

static void clearing_native_hid_preserves_each_adapters_release_contract(void) {
    gmk70_flip_state_t      qmk = {0};
    struct gmk70_flip_state zmk = {.enabled = true};
    (void)gmk70_flip_event(&qmk, LALT(KC_1), true, true, false);
    (void)gmk_flip_core_event(&zmk, 0x0407001e, true, false);
    assert(gmk70_flip_held_modifiers(&qmk) == 4 && gmk_flip_core_modifiers(&zmk) == 4);
    gmk_flip_core_clear(&zmk, 7);
    assert(!gmk_flip_core_modifiers(&zmk));
    assert(!gmk_flip_core_event(&zmk, 0x0407001e, false, false).emit);
    /* QMK owns native clears at its report boundary; no native clear occurred
     * in this trace, so its original release still has to be delivered. */
    gmk70_flip_output_t up = gmk70_flip_event(&qmk, KC_1, false, false, true);
    assert(up.handled && up.target.keycode == KC_F1 && up.target.modifiers == 4);
    assert(!gmk70_flip_held_modifiers(&qmk));
}

int main(void) {
    paired_trace(KC_1, 0x7001e, KC_F1, 0x7003a, 0, false, 0);
    paired_trace(LALT(KC_1), 0x0407001e, KC_F1, 0x0407003a, 4, false, 0);
    paired_trace(KC_1, 0x7001e, KC_BRID, 0xc0070, 0, true, 0);
    paired_trace(KC_3, 0x70020, KC_TAB, 0x0807002b, 8, true, 0);
    paired_trace(KC_5, 0x70022, KC_NO, 0, 0, true, -1);
    paired_trace(KC_6, 0x70023, KC_NO, 0, 0, true, 1);
    paired_trace(KC_F7, 0x70040, KC_MPLY, 0xc00cd, 0, true, 0);
    clearing_native_hid_preserves_each_adapters_release_contract();
    puts("Shared flip adapter traces: native QMK/Vial keycodes and ZMK HID agree across held Alt, OS/settings changes, media, brightness and native clears");
}
