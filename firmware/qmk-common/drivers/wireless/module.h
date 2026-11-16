// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../../core/policy.h"
#include "../../core/radio_commands.h"

// device index
enum {
    DEVS_USB = GMK_HOST_USB,
    DEVS_BT1 = GMK_HOST_BT1,
    DEVS_BT2 = GMK_HOST_BT2,
    DEVS_BT3 = GMK_HOST_BT3,
    DEVS_BT4 = GMK_HOST_BT4,
    DEVS_BT5 = GMK_HOST_BT5,
    DEVS_2G4 = GMK_HOST_DONGLE,
};

enum {
    MD_STATE_NONE         = GMK_RADIO_UNKNOWN,
    MD_STATE_PAIRING      = GMK_RADIO_PAIRING,
    MD_STATE_CONNECTED    = GMK_RADIO_CONNECTED,
    MD_STATE_DISCONNECTED = GMK_RADIO_DISCONNECTED,
    MD_STATE_REJECT       = GMK_RADIO_REJECTED,
};

void md_init(void);
void md_main_task(void);
void md_wait_for_commands(void);
void md_send_kb(uint8_t *data);
void md_send_devinfo(const char *name);
void md_send_nkro(uint8_t *data);
void md_send_consumer(uint8_t *data);
void md_send_system(uint8_t *data);
void md_send_fn(uint8_t *data);
void md_send_mouse(uint8_t *data);
void md_send_devctrl(uint8_t cmd);
void md_send_manufacturer(char *str, uint8_t len);
void md_send_product(char *str, uint8_t len);
void md_send_vpid(uint16_t vid, uint16_t pid);
void md_send_raw(uint8_t *data, uint8_t length);
void md_send_pkt(uint8_t *data, uint32_t len);
bool md_receive_process_user(uint8_t *pdata, uint8_t len);
void md_devs_change(uint8_t devs, bool reset);
bool md_inquire_bat(void);
// Coalesce an explicit display refresh; the existing UART owner sends it.
void    md_request_battery(void);
uint8_t md_get_version(void);
bool    md_reports_inhibited(void);
// Consume only from the keyboard's main context, before native input/report work.
bool     md_take_report_reset(void);
bool     md_battery_is_fresh(void);
uint8_t *md_getp_state(void);
// Accepted notifications, including repeats; optional state is the same snapshot.
uint32_t md_connection_generation(uint8_t *state);
uint8_t *md_getp_bat(void);
uint8_t *md_getp_indicator(void);
