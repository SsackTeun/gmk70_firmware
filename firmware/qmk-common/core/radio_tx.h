/* Copyright 2024 Su (@isuua)
 * Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "policy.h"

enum gmk70_command_phase { GMK70_COMMAND_IDLE, GMK70_COMMAND_WAITING, GMK70_COMMAND_RETRY, GMK70_COMMAND_REPLIED };
struct gmk70_command_step {
    uint8_t phase;
    uint8_t retries;
    bool    retire, send;
};

static inline bool gmk70_command_pending(uint32_t now, uint32_t sent, uint32_t timeout, bool strict) {
    uint32_t elapsed = now - sent;
    return strict ? elapsed <= timeout : elapsed < timeout;
}
static inline bool gmk70_command_exhausted(uint8_t retries, uint8_t limit) {
    return retries >= limit;
}
static inline uint8_t gmk70_command_retry(uint8_t retries) {
    return (uint8_t)(retries + 1);
}

/* Task-phase adapter over the shared deadline/budget decisions above. QMK
 * advances over separate passes at a strict timeout; Zephyr uses the decisions
 * directly around its complete-frame owner at an inclusive deadline. Queue
 * ownership, stale ACK drain and the effect of exhaustion remain adapters. */
static inline struct gmk70_command_step gmk70_command_poll(enum gmk70_command_phase phase, uint8_t retries, uint8_t limit, uint32_t now, uint32_t sent, uint32_t timeout, bool strict, bool deferred) {
    struct gmk70_command_step step = {.phase = (uint8_t)phase, .retries = retries};
    if (phase == GMK70_COMMAND_WAITING) {
        if (gmk70_command_pending(now, sent, timeout, strict)) return step;
        step.phase = GMK70_COMMAND_RETRY;
        if (deferred) return step;
        phase = GMK70_COMMAND_RETRY;
    }
    if (phase == GMK70_COMMAND_RETRY) {
        step.retire  = gmk70_command_exhausted(retries, limit);
        step.retries = step.retire ? 0 : gmk70_command_retry(retries);
        step.phase   = GMK70_COMMAND_IDLE;
        step.send    = !deferred && !step.retire;
    } else if (phase == GMK70_COMMAND_REPLIED) {
        step.retire  = true;
        step.retries = 0;
        step.phase   = GMK70_COMMAND_IDLE;
        step.send    = true;
    } else if (phase == GMK70_COMMAND_IDLE) {
        step.send = true;
    }
    return step;
}
