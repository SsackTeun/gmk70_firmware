// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../../core/radio_tx.h"

typedef enum { smsg_state_free = GMK70_COMMAND_IDLE, smsg_state_busy = GMK70_COMMAND_WAITING, smsg_state_retry = GMK70_COMMAND_RETRY, smsg_state_replied = GMK70_COMMAND_REPLIED } smsg_states_t;

void          smsg_init(void);
bool          smsg_push(uint8_t *buf, uint32_t size);
uint32_t      smsg_peek(uint8_t *buf);
void          smsg_pop(void);
smsg_states_t smsg_get_state(void);
void          smsg_set_state(smsg_states_t state);
bool          smsg_is_busy(void);
