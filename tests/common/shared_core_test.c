/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/common/core/radio_protocol.h"
#include "../../firmware/common/core/radio_tx.h"
#include "../../firmware/common/core/selection.h"
#include "../../firmware/common/core/indicators.h"
#include "../../firmware/common/power/usb.h"

static void physical_cable_is_independent_of_host_selection(void) {
    struct gmk70_switch cable = {0};
    assert(!gmk70_usb_action(&cable, true, false, UINT32_MAX - 20));
    assert(!gmk70_usb_action(&cable, true, false, 28));
    assert(gmk70_usb_action(&cable, true, false, 29) == 1);
    assert(gmk70_usb_action(&cable, true, false, 30) == 1); /* Native enable failure retries. */
    assert(!gmk70_usb_action(&cable, true, true, 31));
    assert(!gmk70_usb_action(&cable, false, true, 32));
    assert(!gmk70_usb_action(&cable, true, true, 40));
    assert(!gmk70_usb_action(&cable, true, true, 100));
    assert(!gmk70_usb_action(&cable, false, true, 101));
    assert(gmk70_usb_action(&cable, false, true, 151) == -1);
}

static unsigned feed(struct gmk70_radio_parser *parser, const uint8_t *bytes, size_t count, uint32_t now, unsigned policy) {
    unsigned events = 0;
    for (size_t i = 0; i < count; ++i)
        events |= gmk70_radio_receive(parser, bytes[i], now, NULL, 0, policy);
    return events;
}

static void raw_frames_cannot_inject_embedded_battery_or_ack_messages(void) {
    for (unsigned policy = 0; policy <= 3; ++policy) {
        for (unsigned size = 0; size <= 255; ++size) {
            struct gmk70_radio_parser parser     = {0};
            uint8_t                   bytes[259] = {0xaf, 0x60, (uint8_t)size};
            const uint8_t             embedded[] = {0x5c, 100, 0xc0, 0x61, 0x0d, 0x0a};
            unsigned                  sum        = bytes[0] + bytes[1] + bytes[2];
            for (unsigned i = 0; i < size; ++i) {
                bytes[i + 3] = embedded[i % sizeof(embedded)];
                sum += bytes[i + 3];
            }
            bytes[size + 3] = (uint8_t)sum;
            unsigned events = feed(&parser, bytes, size + 4, 1, policy);
            assert(!(events & GMK70_RX_ACK));
            assert(!!(events & GMK70_RX_FRAME) == (size <= 32));
            assert(!!(events & GMK70_RX_OVERSIZE) == (size > 32));
            const uint8_t battery[] = {0x5c, 73, 0xa5};
            assert(feed(&parser, battery, sizeof(battery), 2, policy) == GMK70_RX_FRAME);
            assert(parser.command == 0x5c && parser.value == 73);
        }
    }
}

static void timeout_and_fault_quarantine_survive_idle_polls_and_timer_wrap(void) {
    for (unsigned prefix = 1; prefix <= 2; ++prefix) {
        struct gmk70_radio_parser parser   = {0};
        const uint8_t             header[] = {0xaf, 0x60};
        assert(!feed(&parser, header, prefix, UINT32_MAX - 4, 0));
        assert(!gmk70_radio_expire(&parser, 4));
        assert(gmk70_radio_expire(&parser, 5));
        assert(parser.discard && parser.length == 259);
        for (unsigned n = 0; n < 10; ++n) {
            assert(!gmk70_radio_expire(&parser, 100 + n));
            gmk70_radio_fault(&parser);
        }
        uint8_t tail[258];
        memset(tail, 0x5c, sizeof(tail));
        assert(!feed(&parser, tail, 259 - prefix, 110, 0));
        const uint8_t battery[] = {0x5c, 12, 0x68};
        assert(feed(&parser, battery, sizeof(battery), 111, 0) == GMK70_RX_FRAME);
        assert(parser.value == 12);
    }
    struct gmk70_radio_parser parser = {0};
    const uint8_t             raw[]  = {0xaf, 0x60, 5, 0x61};
    (void)feed(&parser, raw, sizeof(raw), 1, 0);
    gmk70_radio_fault(&parser);
    const uint8_t remaining[] = {0x0d, 0x0a, 0x5c, 100, 0xc0};
    assert(!feed(&parser, remaining, sizeof(remaining), 20, 0));
    const uint8_t ack[] = {0x61, 0x0d, 0x0a};
    assert(feed(&parser, ack, sizeof(ack), 21, 0) == GMK70_RX_ACK);
}

static void adapter_policies_and_bounded_frame_storage_remain_explicit(void) {
    struct gmk70_radio_parser qmk = {0}, zmk = {0};
    const uint8_t             malformed[] = {0xaf, 0x5c, 73, 0xa5};
    assert(!(feed(&qmk, malformed, sizeof(malformed), 1, 0) & GMK70_RX_FRAME));
    assert(feed(&zmk, malformed, sizeof(malformed), 1, GMK70_RX_RESYNC_RAW | GMK70_RX_STRICT_ACK) & GMK70_RX_FRAME);
    qmk = zmk                       = (struct gmk70_radio_parser){0};
    const uint8_t nonstandard_ack[] = {0x61, 0, 0x61};
    assert(feed(&qmk, nonstandard_ack, sizeof(nonstandard_ack), 1, 0) == GMK70_RX_FRAME);
    assert(feed(&zmk, nonstandard_ack, sizeof(nonstandard_ack), 1, GMK70_RX_STRICT_ACK) == GMK70_RX_CHECKSUM);
    struct {
        uint8_t before, frame[3], after;
    } guarded           = {.before = 0x77, .after = 0x88};
    qmk                 = (struct gmk70_radio_parser){0};
    const uint8_t raw[] = {0xaf, 0x60, 1, 0x73, 0x83};
    for (unsigned i = 0; i < sizeof(raw); ++i)
        (void)gmk70_radio_receive(&qmk, raw[i], 2, guarded.frame, sizeof(guarded.frame), 0);
    assert(guarded.before == 0x77 && guarded.after == 0x88);
}

static void selector_holds_freshness_and_indicators_keep_board_contracts(void) {
    struct gmk70_switch selector = {0};
    assert(gmk70_switch_update(&selector, 2, UINT32_MAX - 24, 50) == GMK70_SWITCH_CANDIDATE);
    assert(!gmk70_switch_update(&selector, 2, 24, 50));
    assert(gmk70_switch_update(&selector, 2, 25, 50) == GMK70_SWITCH_STABLE);
    assert(gmk70_switch_host(2, 5) == GMK_HOST_BT5);
    assert(gmk70_switch_host(2, 99) == GMK_HOST_BT1);
    assert(gmk70_switch_host(3, 5) == GMK_HOST_USB);
    assert(gmk70_switch_host(1, 5) == GMK_HOST_DONGLE);
    struct gmk70_hold hold   = {0};
    uint8_t           action = 0;
    gmk70_hold_start(&hold, 71, GMK_HOST_BT3, UINT32_MAX - 1499);
    gmk70_hold_release(&hold, 70);
    assert(!gmk70_hold_take(&hold, 1499, 3000, &action));
    assert(gmk70_hold_take(&hold, 1500, 3000, &action) && action == GMK_HOST_BT3);
    assert(!gmk70_hold_take(&hold, 1501, 3000, &action));
    assert(gmk70_battery_fresh(true, 0, 10000));
    assert(!gmk70_battery_fresh(true, 0, 10001));
    assert(!gmk70_battery_fresh(true, 255, 0));
    assert(gmk70_battery_warning(true, 15, 0, false));
    assert(!gmk70_battery_warning(true, 0, 0, true));
    assert(gmk70_battery_segment(0, true, 0, 150).r == 150);
    assert(gmk70_battery_segment(60, true, 5, 80).g == 80);
    assert(!gmk70_battery_segment(60, true, 6, 80).g);
    assert(gmk70_battery_segment(255, false, 9, 150).b == 24);
    assert(gmk70_radio_led(GMK_HOST_BT4) == 41);
    assert(gmk70_radio_led(GMK_HOST_DONGLE) == 11);
    assert(gmk70_radio_led(GMK_HOST_USB) == UINT8_MAX);
}

static void retry_owners_keep_exact_deadlines_and_finite_budgets(void) {
    struct gmk70_command_step qmk = gmk70_command_poll(GMK70_COMMAND_WAITING, 0, 50, 10, 0, 10, true, true);
    struct gmk70_command_step zmk = gmk70_command_poll(GMK70_COMMAND_WAITING, 0, 50, 10, 0, 10, false, false);
    assert(qmk.phase == GMK70_COMMAND_WAITING && !qmk.send && !qmk.retire);
    assert(zmk.phase == GMK70_COMMAND_IDLE && zmk.send && zmk.retries == 1);
    qmk = gmk70_command_poll(qmk.phase, qmk.retries, 50, 11, 0, 10, true, true);
    assert(qmk.phase == GMK70_COMMAND_RETRY && !qmk.send);
    qmk = gmk70_command_poll(qmk.phase, qmk.retries, 50, 12, 0, 10, true, true);
    assert(qmk.phase == GMK70_COMMAND_IDLE && qmk.retries == 1 && !qmk.send);
    qmk = gmk70_command_poll(qmk.phase, qmk.retries, 50, 13, 0, 10, true, true);
    assert(qmk.send && !qmk.retire);
    for (unsigned retries = 0; retries <= 50; ++retries) {
        zmk = gmk70_command_poll(GMK70_COMMAND_WAITING, (uint8_t)retries, 50, 6, UINT32_MAX - 4, 10, false, false);
        assert(zmk.retire == (retries == 50));
        assert(zmk.send == (retries < 50));
    }
    qmk = gmk70_command_poll(GMK70_COMMAND_REPLIED, 49, 50, 1, 0, 10, true, true);
    assert(qmk.retire && qmk.send && !qmk.retries);
    zmk = gmk70_command_poll(GMK70_COMMAND_WAITING, 1, 1, 10, 0, 10, false, false);
    assert(zmk.retire && !zmk.send); /* Battery inquiry has one retry. */
    uint32_t since    = 0;
    bool     critical = false;
    gmk70_critical_update(&since, &critical, 10, true, 0);
    assert(critical && since == 10);
    assert(!gmk70_critical_due(60010, since, 0));
    assert(gmk70_critical_due(60011, since, 0));
    gmk70_critical_update(&since, &critical, 60012, false, 0);
    assert(!critical); /* Cable/invalid/freshness admission is caller input. */
}

int main(void) {
    physical_cable_is_independent_of_host_selection();
    raw_frames_cannot_inject_embedded_battery_or_ack_messages();
    timeout_and_fault_quarantine_survive_idle_polls_and_timer_wrap();
    adapter_policies_and_bounded_frame_storage_remain_explicit();
    selector_holds_freshness_and_indicators_keep_board_contracts();
    retry_owners_keep_exact_deadlines_and_finite_budgets();
    puts("Shared GMK70 core: all raw lengths, quarantine/fault/wrap, adapter policies, guarded buffers, selection, holds and battery contracts passed");
}
