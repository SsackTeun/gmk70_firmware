/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.h"

uint8_t gmk_flip_core_modifiers(const struct gmk70_flip_state *core) {
    return gmk70_flip_modifiers(core);
}
void gmk_flip_core_clear(struct gmk70_flip_state *core, uint16_t page) {
    gmk70_flip_clear(core, page);
}
struct gmk_flip_output gmk_flip_core_event(struct gmk70_flip_state *core, uint32_t encoded, bool pressed, bool mac) {
    struct gmk70_flip_result out = gmk70_flip_transition(core, encoded, pressed, core->enabled, mac, GMK70_FLIP_RELEASE_MARKER);
    return (struct gmk_flip_output){.key = out.key, .previous = out.previous, .brightness = out.brightness, .remaining_modifiers = out.remaining_modifiers, .source = out.source, .emit = out.emit, .release_previous = out.release_previous, .custom = out.custom, .previous_custom = out.previous_custom};
}
