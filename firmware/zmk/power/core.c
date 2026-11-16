/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.h"
#include "../../qmk-common/core/policy.h"

static bool critical_reading(const struct gmk_power_inputs *inputs) {
    return gmk70_battery_warning(inputs->battery_valid, inputs->battery_percentage, inputs->battery_age_ms, inputs->cable);
}
static bool critical_elapsed(const struct gmk_power_core *core, const struct gmk_power_inputs *inputs) {
    return core->critical && gmk70_critical_due(inputs->now_ms, core->critical_since, inputs->battery_percentage);
}
bool gmk_power_core_critical_ready(const struct gmk_power_core *core, const struct gmk_power_inputs *inputs) {
    return critical_reading(inputs) && critical_elapsed(core, inputs);
}

void gmk_power_core_activity(struct gmk_power_core *core, bool active, uint32_t transition_ms) {
    if (active)
        core->inactive = false;
    else if (!core->inactive) {
        core->inactive       = true;
        core->inactive_since = transition_ms;
    }
}
/* Preserve the linked safety boundary under LTO. */
__attribute__((noipa)) enum gmk_power_request gmk_power_core_decide(struct gmk_power_core *core, const struct gmk_power_inputs *inputs) {
    gmk70_critical_update(&core->critical_since, &core->critical, inputs->now_ms, critical_reading(inputs), inputs->battery_percentage);
    if (inputs->busy) return GMK_POWER_NONE;
    /* The existing QMK critical policy bypasses held keys after qualification.
     * The hardware coordinator must still clear reports and quiesce all work. */
    if (critical_elapsed(core, inputs)) return GMK_POWER_CRITICAL;
    if (!core->inactive || !inputs->timeout_ms || inputs->keys_held || (inputs->usb_powered && !inputs->usb_sleep_allowed)) return GMK_POWER_NONE;
    return gmk70_inactivity_due(inputs->now_ms, core->inactive_since, inputs->timeout_ms) ? GMK_POWER_INACTIVITY : GMK_POWER_NONE;
}
