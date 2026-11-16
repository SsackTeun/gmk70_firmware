/* Copyright 2024 Su (@isuua)
 * Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "policy.h"
#include <stddef.h>
#include <string.h>

enum {
    GMK70_RX_FRAME      = 1,
    GMK70_RX_ACK        = 2,
    GMK70_RX_UNKNOWN    = 4,
    GMK70_RX_RAW_HEADER = 8,
    GMK70_RX_OVERSIZE   = 16,
    GMK70_RX_CHECKSUM   = 32,
    GMK70_RX_BAD_RAW    = 64,
};
enum {
    /* Preserve each existing adapter's malformed-header behavior. */
    GMK70_RX_RESYNC_RAW = 1,
    GMK70_RX_STRICT_ACK = 2,
};
struct gmk70_radio_parser {
    uint32_t last_byte_ms;
    uint16_t count, length;
    uint8_t  command, value, sum;
    bool     discard;
};
static inline uint8_t gmk70_radio_checksum(const uint8_t *data, size_t size) {
    uint8_t sum = 0;
    for (size_t i = 0; i < size; ++i)
        sum = (uint8_t)(sum + data[i]);
    return sum;
}
static inline bool gmk70_radio_command(uint8_t byte) {
    return byte == MD_REV_CMD_INDICATOR || byte == MD_REV_CMD_DEVCTRL || byte == MD_REV_CMD_BATVOL || byte == MD_REV_CMD_MD_FW_VERSION || byte == MD_REV_CMD_HOST_STATE || byte == 0x61 || byte == MD_REV_CMD_RAW;
}
/* Keep the finite raw-tail budget even across empty timeout/fault polls. */
static inline bool gmk70_radio_quarantine(struct gmk70_radio_parser *parser) {
    if (!parser->count || (parser->command != MD_REV_CMD_RAW && !parser->discard)) return false;
    parser->discard = true;
    if (parser->command == MD_REV_CMD_RAW && parser->count < 3) {
        parser->command = 0;
        parser->length  = 259;
    }
    return true;
}
static inline void gmk70_radio_fault(struct gmk70_radio_parser *parser) {
    if (!gmk70_radio_quarantine(parser)) *parser = (struct gmk70_radio_parser){0};
}
static inline bool gmk70_radio_expire(struct gmk70_radio_parser *parser, uint32_t now) {
    if (!parser->count || parser->discard || !gmk70_elapsed(now, parser->last_byte_ms, GMK70_RADIO_TIMEOUT_MS)) return false;
    gmk70_radio_fault(parser);
    return true;
}
static inline unsigned gmk70_radio_begin(struct gmk70_radio_parser *parser, uint8_t byte, uint32_t now, uint8_t *frame, size_t capacity) {
    *parser = (struct gmk70_radio_parser){0};
    if (!gmk70_radio_command(byte)) return GMK70_RX_UNKNOWN;
    *parser = (struct gmk70_radio_parser){.last_byte_ms = now, .count = 1, .length = 3, .command = byte, .sum = byte};
    if (frame && capacity) frame[0] = byte;
    return 0;
}
/* Pure streaming decoder. No allocation, hardware access, callbacks or locks.
 * Optional frame storage belongs to the caller; ZMK needs no raw buffer.
 * A completed frame retains command/value/length until the next input byte. */
static inline unsigned gmk70_radio_receive(struct gmk70_radio_parser *parser, uint8_t byte, uint32_t now, uint8_t *frame, size_t capacity, unsigned policy) {
    if (!parser->count) return gmk70_radio_begin(parser, byte, now, frame, capacity);
    parser->last_byte_ms = now;
    if (parser->count == 1 && parser->command == MD_REV_CMD_RAW && byte != MD_REV_CMD_RAW_OUT) {
        *parser = (struct gmk70_radio_parser){0};
        if (policy & GMK70_RX_RESYNC_RAW) (void)gmk70_radio_begin(parser, byte, now, frame, capacity);
        return GMK70_RX_BAD_RAW;
    }
    if (frame && parser->count < capacity) frame[parser->count] = byte;
    unsigned events = 0;
    if (parser->count == 1) parser->value = byte;
    if (parser->count == 2 && parser->command == MD_REV_CMD_RAW) {
        parser->length  = (uint16_t)byte + 4;
        parser->discard = byte > 32;
        events          = GMK70_RX_RAW_HEADER | (parser->discard ? GMK70_RX_OVERSIZE : 0);
    }
    if (parser->count + 1U < parser->length) {
        parser->sum = (uint8_t)(parser->sum + byte);
        ++parser->count;
        return events;
    }
    bool ack   = parser->command == 0x61 && parser->value == 0x0d && byte == 0x0a;
    bool valid = ack || (!(parser->command == 0x61 && (policy & GMK70_RX_STRICT_ACK)) && parser->sum == byte);
    if (!parser->discard) events |= valid ? (ack ? GMK70_RX_ACK : GMK70_RX_FRAME) : GMK70_RX_CHECKSUM;
    parser->count = 0;
    return events;
}
static inline void gmk70_radio_control(uint8_t frame[3], uint8_t command) {
    frame[0] = MD_SND_CMD_DEVCTRL;
    frame[1] = command;
    frame[2] = (uint8_t)(frame[0] + frame[1]);
}
static inline void gmk70_radio_report(uint8_t *frame, uint8_t command, const uint8_t *payload, size_t size) {
    frame[0] = command;
    memcpy(frame + 1, payload, size);
    frame[size + 1] = gmk70_radio_checksum(frame, size + 1);
}
