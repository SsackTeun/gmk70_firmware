// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "module.h"
#include "../../../common/core/radio_protocol.h"
#include "smsg.h"
#include "uart.h"
#include "radio_capture.h"
#ifdef PROTOCOL_CHIBIOS
#    include "hal.h"
static event_listener_t md_uart_errors;
static bool             md_uart_errors_registered;
#endif
#ifdef GMK70_RADIO_THREAD
#    include "ch.h"
static MUTEX_DECL(md_uart_mutex);
#endif

#ifndef MD_BAUD_RATE
#    define MD_BAUD_RATE GMK70_RADIO_BAUD
#endif

#ifndef MD_SNED_PKT_TIMEOUT
#    define MD_SNED_PKT_TIMEOUT GMK70_RADIO_TIMEOUT_MS
#endif

#ifndef MD_SEND_PKT_RETRY
#    define MD_SEND_PKT_RETRY GMK70_RADIO_RETRIES
#endif

#ifndef MD_SEND_PKT_PAYLOAD_MAX
#    define MD_SEND_PKT_PAYLOAD_MAX ((MD_RAW_SIZE) + 4)
#endif

#ifndef MD_BT1_NAME
#    define MD_BT1_NAME PRODUCT " BT1"
#endif

#ifndef MD_BT2_NAME
#    define MD_BT2_NAME PRODUCT " BT2"
#endif

#ifndef MD_BT3_NAME
#    define MD_BT3_NAME PRODUCT " BT3"
#endif

#ifndef MD_BT4_NAME
#    define MD_BT4_NAME PRODUCT " BT4"
#endif

#ifndef MD_BT5_NAME
#    define MD_BT5_NAME PRODUCT " BT5"
#endif

#ifndef MD_DONGLE_MANUFACTURER
#    define MD_DONGLE_MANUFACTURER MANUFACTURER
#endif

#ifndef MD_DONGLE_PRODUCT
#    define MD_DONGLE_PRODUCT PRODUCT " Dongle"
#endif

#ifndef MD_RAW_SIZE
#    define MD_RAW_SIZE 32
#endif

#define MD_BATTERY_FRESH_MS GMK70_BATTERY_FRESH_MS

#define USBCONCAT(a, b) a##b
#define USBSTR(s) USBCONCAT(L, s)

typedef struct {
    uint8_t  state;
    uint8_t  indicator;
    uint8_t  version;
    uint8_t  bat;
    bool     inhibit_reports;
    bool     battery_valid;
    uint32_t battery_sample_ms;
    uint32_t connection_generation;
} md_info_t;

typedef struct gmk70_radio_parser md_receive_t;

static md_receive_t md_receive;

static uint8_t md_pkt_payload[MD_SEND_PKT_PAYLOAD_MAX] = {0};
static uint8_t md_rev_payload[MD_SEND_PKT_PAYLOAD_MAX] = {0};
static uint8_t md_raw_payload[MD_RAW_SIZE]             = {0};

static md_info_t md_info = {
    .bat       = 100,
    .indicator = 0,
    .version   = 0,
    .state     = MD_STATE_NONE,
};

static uint8_t       selected_device;
static uint8_t       status_context;
static uint32_t      smsg_timer;
static uint8_t       smsg_retry;
static uint8_t       smsg_limit;
static bool          report_reset_pending;
static volatile bool battery_requested;

static void md_set_connection_state(uint8_t state, bool inhibit_reports) {
#ifdef GMK70_RADIO_THREAD
    chSysLock();
#endif
    // Latch transitions so a loss followed by reconnect between main-loop
    // passes cannot hide stale native input. Never call report APIs here.
    if (md_info.state != state) report_reset_pending = true;
    ++md_info.connection_generation;
    md_info.state           = state;
    md_info.inhibit_reports = inhibit_reports;
#ifdef GMK70_RADIO_THREAD
    chSysUnlock();
#endif
}

static void md_send_ack(void) {
    uint8_t sdata[0x03] = {0x61, 0x0D, 0x0A};
    md_send_pkt(sdata, sizeof(sdata));
}

static bool md_check_sum(const uint8_t *data, uint32_t length) {
    return length && gmk70_radio_checksum(data, length - 1) == data[length - 1];
}
static void md_calc_check_sum(uint8_t *data, uint32_t length) {
    data[length] = gmk70_radio_checksum(data, length);
}

bool md_receive_process_user(uint8_t *pdata, uint8_t len) __attribute__((weak));
bool md_receive_process_user(uint8_t *pdata, uint8_t len) {
    return true;
}

bool md_receive_process_kb(uint8_t *pdata, uint8_t len) __attribute__((weak));
bool md_receive_process_kb(uint8_t *pdata, uint8_t len) {
    return md_receive_process_user(pdata, len);
}

void md_receive_raw_cb(uint8_t *pdata, uint8_t len) __attribute__((weak));
void md_receive_raw_cb(uint8_t *pdata, uint8_t len) {}

void md_receive_host_cb(bool resume) __attribute__((weak));
void md_receive_host_cb(bool resume) {}

static void md_quarantine_or_reset(void) {
    gmk70_radio_fault(&md_receive);
}

static bool md_uart_fault(void) {
#ifdef PROTOCOL_CHIBIOS
    return (chEvtGetAndClearFlags(&md_uart_errors) & (SD_PARITY_ERROR | SD_FRAMING_ERROR | SD_OVERRUN_ERROR | SD_BREAK_DETECTED | SD_QUEUE_FULL_ERROR)) != 0;
#else
    return false;
#endif
}

static void md_receive_msg_task(void) {
    (void)gmk70_radio_expire(&md_receive, sync_timer_read32());
    for (;;) {
        if (md_uart_fault()) {
            md_quarantine_or_reset();
            // Lost bytes have unknown length; retain the finite raw-tail budget.
            while (uart_available()) {
                (void)uart_read();
                gmk_radio_diag_count(GMK_RADIO_RX_BYTES, selected_device);
            }
            return;
        }
        if (!uart_available()) return;
        uint8_t data = uart_read();
        gmk_radio_diag_count(GMK_RADIO_RX_BYTES, selected_device);
        unsigned events = gmk70_radio_receive(&md_receive, data, sync_timer_read32(), md_rev_payload, sizeof(md_rev_payload), 0);
        if (events & GMK70_RX_UNKNOWN) gmk_radio_diag_count(GMK_RADIO_UNKNOWN_BYTES, selected_device);
        if (events & GMK70_RX_RAW_HEADER) gmk_radio_diag_header(data, selected_device);
        if (events & GMK70_RX_OVERSIZE) gmk_radio_diag_count(GMK_RADIO_RAW_OVERSIZE, selected_device);
        if (events & GMK70_RX_CHECKSUM) gmk_radio_diag_count(GMK_RADIO_CHECKSUM_ERRORS, selected_device);
        if (events & GMK70_RX_ACK) {
            gmk_radio_diag_count(GMK_RADIO_ACKS, selected_device);
            if (smsg_get_state() == smsg_state_busy) smsg_set_state(smsg_state_replied);
            return;
        }
        if (!(events & GMK70_RX_FRAME)) continue;
        uint8_t length = (uint8_t)md_receive.length;
        md_receive     = (md_receive_t){0};
        if (md_check_sum(md_rev_payload, length)) {
            gmk_radio_diag_count(GMK_RADIO_VALID_FRAMES, selected_device);
            if (md_rev_payload[0] == MD_REV_CMD_RAW && md_rev_payload[1] == MD_REV_CMD_RAW_OUT) {
                gmk_radio_diag_count(md_rev_payload[2] == 31 ? GMK_RADIO_RAW_31 : md_rev_payload[2] == 32 ? GMK_RADIO_RAW_32 : GMK_RADIO_RAW_OTHER, selected_device);
            }
            md_send_ack();

            if (md_receive_process_kb(md_rev_payload, length) != true) {
                return;
            }

            switch (md_rev_payload[0]) {
                case MD_REV_CMD_RAW: {
                    uint8_t *pdata;
                    uint8_t  len;

                    len   = md_rev_payload[2];
                    pdata = &md_rev_payload[3];

                    if (len == sizeof(md_raw_payload)) {
                        memcpy(md_raw_payload, pdata, len);
                        gmk_radio_diag_count(GMK_RADIO_RAW_DISPATCHED, selected_device);
                        md_receive_raw_cb(md_raw_payload, len);
                    }
                } break;
                case MD_REV_CMD_INDICATOR: {
                    md_info.indicator = md_rev_payload[1];
                } break;
                case MD_REV_CMD_DEVCTRL: {
                    // V15 accepts status only for a dispatched channel command.
                    if (!status_context || status_context != selected_device) break;
                    uint8_t connection = gmk70_radio_connection(md_rev_payload[1]);
                    if (connection != UINT8_MAX) md_set_connection_state(connection, connection == MD_STATE_PAIRING);
                } break;
                case MD_REV_CMD_BATVOL: {
                    if (md_rev_payload[1] <= 100) {
                        md_info.bat               = md_rev_payload[1];
                        md_info.battery_sample_ms = sync_timer_read32();
                        md_info.battery_valid     = true;
                    }
                } break;
                case MD_REV_CMD_MD_FW_VERSION: {
                    md_info.version = md_rev_payload[1];
                    gmk_radio_diag_version(md_rev_payload[1]);
                } break;
                case MD_REV_CMD_HOST_STATE: {
                    md_receive_host_cb(md_rev_payload[1] == MD_REV_CMD_HOST_STATE_RESUME);
                } break;
                default:
                    break;
            }
        } else {
            gmk_radio_diag_count(GMK_RADIO_CHECKSUM_ERRORS, selected_device);
        }
    }
}

static void md_send_pkt_task(void) {
    struct gmk70_command_step step = gmk70_command_poll((enum gmk70_command_phase)smsg_get_state(), smsg_retry, smsg_limit, sync_timer_read32(), smsg_timer, MD_SNED_PKT_TIMEOUT, true, true);
    smsg_retry                     = step.retries;
    smsg_set_state((smsg_states_t)step.phase);
    if (step.retire) smsg_pop();
    if (step.send) {
        uint32_t size = smsg_peek(md_pkt_payload);
        if (size) {
            if (!smsg_retry) {
                smsg_limit = MD_SEND_PKT_RETRY;
                if (md_pkt_payload[0] == MD_SND_CMD_DEVCTRL) {
                    const uint8_t command = md_pkt_payload[1];
                    if ((command >= MD_SND_CMD_DEVCTRL_2G4 && command <= MD_SND_CMD_DEVCTRL_BT5) || command == MD_SND_CMD_DEVCTRL_PAIR || command == MD_SND_CMD_DEVCTRL_CLEAN) status_context = selected_device;
                    if (command == MD_SND_CMD_DEVCTRL_PAIR) md_info.inhibit_reports = true;
                    if (command == MD_SND_CMD_DEVCTRL_INQVOL) smsg_limit = 1;
                }
            }
            md_send_pkt(md_pkt_payload, size);
            smsg_timer = sync_timer_read32();
            smsg_set_state(smsg_state_busy);
        }
    }
}

#ifdef GMK70_RADIO_THREAD
static THD_WORKING_AREA(md_worker_stack, 1024);
static bool md_worker_started;
static THD_FUNCTION(md_worker, arg) {
    (void)arg;
    for (;;) {
        md_main_task();
        // V15 sleeps five ticks at 100 kHz; round up on a coarser QMK kernel.
        chThdSleep(TIME_US2I(50));
    }
}
#endif

void md_init(void) {
    gmk_radio_diag_init();
    uart_init(MD_BAUD_RATE);
#ifdef PROTOCOL_CHIBIOS
    if (!md_uart_errors_registered) {
        // WB32's pinned driver also sets NOISE_ERROR for ordinary DR bytes.
        // Parity, framing, hardware/software overflow and break lose framing.
        chEvtRegisterMaskWithFlags(chnGetEventSource(&UART_DRIVER), &md_uart_errors, 0, SD_PARITY_ERROR | SD_FRAMING_ERROR | SD_OVERRUN_ERROR | SD_BREAK_DETECTED | SD_QUEUE_FULL_ERROR);
        md_uart_errors_registered = true;
    }
    (void)chEvtGetAndClearFlags(&md_uart_errors);
#endif
    smsg_init();
    md_receive           = (md_receive_t){0};
    md_info              = (md_info_t){.bat = 100, .state = MD_STATE_NONE};
    smsg_timer           = 0;
    smsg_retry           = 0;
    report_reset_pending = false;
    battery_requested    = false;
    selected_device = status_context = 0;

    memset(md_pkt_payload, 0, sizeof(md_pkt_payload));
#ifdef GMK70_RADIO_THREAD
    if (!md_worker_started) {
        md_worker_started = true;
        chThdCreateStatic(md_worker_stack, sizeof(md_worker_stack), HIGHPRIO, md_worker, NULL);
    }
#endif
}

void md_main_task(void) {
    if (md_info.battery_valid && sync_timer_elapsed32(md_info.battery_sample_ms) > MD_BATTERY_FRESH_MS) md_info.battery_valid = false;
    md_send_pkt_task();
    md_receive_msg_task();
    if (battery_requested && md_inquire_bat()) battery_requested = false;
}

void md_request_battery(void) {
    battery_requested = true;
}

void md_wait_for_commands(void) {
    // V15 0x08002EDE/0x08002F2E waits for the existing ACK queue to drain.
    while (smsg_is_busy()) {
#ifdef GMK70_RADIO_THREAD
        chThdSleep(TIME_US2I(50));
#else
        md_main_task();
#endif
    }
}

uint8_t *md_getp_state(void) {
    return &md_info.state;
}

uint32_t md_connection_generation(uint8_t *state) {
#ifdef GMK70_RADIO_THREAD
    chSysLock();
#endif
    uint32_t generation = md_info.connection_generation;
    if (state) *state = md_info.state;
#ifdef GMK70_RADIO_THREAD
    chSysUnlock();
#endif
    return generation;
}

uint8_t *md_getp_bat(void) {
    return &md_info.bat;
}

bool md_battery_is_fresh(void) {
    return md_info.battery_valid && sync_timer_elapsed32(md_info.battery_sample_ms) <= MD_BATTERY_FRESH_MS;
}

uint8_t *md_getp_indicator(void) {
    return &md_info.indicator;
}

uint8_t md_get_version(void) {
    return md_info.version;
}

bool md_reports_inhibited(void) {
#ifdef GMK70_RADIO_THREAD
    chSysLock();
#endif
    bool inhibited = md_info.inhibit_reports || report_reset_pending;
#ifdef GMK70_RADIO_THREAD
    chSysUnlock();
#endif
    return inhibited;
}

bool md_take_report_reset(void) {
#ifdef GMK70_RADIO_THREAD
    chSysLock();
#endif
    bool pending         = report_reset_pending;
    report_reset_pending = false;
#ifdef GMK70_RADIO_THREAD
    chSysUnlock();
#endif
    return pending;
}

void md_send_pkt(uint8_t *data, uint32_t len) {
    if (!data || !len) {
        return;
    }

    // send
#ifdef GMK70_RADIO_THREAD
    chMtxLock(&md_uart_mutex);
#endif
    uart_transmit(data, len);
#ifdef GMK70_RADIO_THREAD
    chMtxUnlock(&md_uart_mutex);
#endif
}

void md_send_kb(uint8_t *data) {
    uint8_t sdata[MD_SND_CMD_KB_LEN + 2] = {0x00};

    gmk70_radio_report(sdata, MD_SND_CMD_SEND_KB, data, sizeof(sdata) - 2);
    if (!md_reports_inhibited()) md_send_pkt(sdata, sizeof(sdata));
}

void md_send_nkro(uint8_t *data) {
    uint8_t sdata[MD_SND_CMD_NKRO_LEN + 2] = {0x00};

    gmk70_radio_report(sdata, MD_SND_CMD_SEND_NKRO, data, sizeof(sdata) - 2);
    if (!md_reports_inhibited()) md_send_pkt(sdata, sizeof(sdata));
}

void md_send_consumer(uint8_t *data) {
    uint8_t sdata[MD_SND_CMD_CONSUMER_LEN + 2] = {0x00};

    gmk70_radio_report(sdata, MD_SND_CMD_SEND_CONSUMER, data, sizeof(sdata) - 2);
    if (!md_reports_inhibited()) md_send_pkt(sdata, sizeof(sdata));
}

void md_send_system(uint8_t *data) {
    uint8_t sdata[MD_SND_CMD_SYSTEM_LEN + 2] = {0x00};

    gmk70_radio_report(sdata, MD_SND_CMD_SEND_SYSTEM, data, sizeof(sdata) - 2);
    if (!md_reports_inhibited()) md_send_pkt(sdata, sizeof(sdata));
}

void md_send_fn(uint8_t *data) {
    uint8_t sdata[MD_SND_CMD_FN_LEN + 2] = {0x00};

    gmk70_radio_report(sdata, MD_SND_CMD_SEND_FN, data, sizeof(sdata) - 2);
    if (!md_reports_inhibited()) md_send_pkt(sdata, sizeof(sdata));
}

void md_send_mouse(uint8_t *data) {
    uint8_t sdata[MD_SND_CMD_MOUSE_LEN + 2] = {0x00};

    gmk70_radio_report(sdata, MD_SND_CMD_SEND_MOUSE, data, sizeof(sdata) - 2);
    if (!md_reports_inhibited()) md_send_pkt(sdata, sizeof(sdata));
}

void md_send_devinfo(const char *name) {
    uint8_t sdata[MD_SND_CMD_DEVINFO_LEN + 3] = {0x00};
    uint8_t infolen                           = strlen((const char *)name);

    if (infolen > MD_SND_CMD_DEVINFO_LEN) {
        return;
    }

    sdata[0] = MD_SND_CMD_SEND_DEVINFO;
    sdata[1] = infolen;

    memcpy(&sdata[2], name, infolen);
    md_calc_check_sum(sdata, infolen + 2);
    smsg_push(sdata, infolen + 3);
}

void md_send_devctrl(uint8_t cmd) {
    uint8_t sdata[3] = {0x00};

    gmk70_radio_control(sdata, cmd);

    smsg_push(sdata, sizeof(sdata));
}

void md_send_manufacturer(char *str, uint8_t len) {
    uint8_t sdata[MD_SND_CMD_MANUFACTURER_LEN + 3] = {0x00};

    if (len > MD_SND_CMD_MANUFACTURER_LEN) {
        return;
    }

    sdata[0] = MD_SND_CMD_MANUFACTURER;
    sdata[1] = len;
    memcpy(&sdata[2], str, len);
    md_calc_check_sum(sdata, len + 2);
    smsg_push(sdata, len + 3);
}

void md_send_product(char *str, uint8_t len) {
    uint8_t sdata[MD_SND_CMD_PRODUCT_LEN + 3] = {0x00};

    if (len > MD_SND_CMD_PRODUCT_LEN) {
        return;
    }

    sdata[0] = MD_SND_CMD_PRODUCT;
    sdata[1] = len;
    memcpy(&sdata[2], str, len);
    md_calc_check_sum(sdata, len + 2);
    smsg_push(sdata, len + 3);
}

void md_send_vpid(uint16_t vid, uint16_t pid) {
    uint8_t  sdata[4 + 2] = {0x00};
    uint32_t vpid;

    vpid = (pid << 16) | vid;

    sdata[0] = MD_SND_CMD_VPID;
    memcpy(&sdata[1], &vpid, sizeof(vpid));
    md_calc_check_sum(sdata, sizeof(sdata) - 1);
    smsg_push(sdata, sizeof(sdata));
}

void md_send_raw(uint8_t *data, uint8_t length) {
    uint8_t sdata[MD_RAW_SIZE + 4] = {0x00};

    if (length != MD_RAW_SIZE) {
        return;
    }
    gmk_radio_diag_count(GMK_RADIO_RAW_TX_REQUESTS, selected_device);

    sdata[0] = MD_SND_CMD_RAW;
    sdata[1] = MD_SND_CMD_RAW_IN;
    sdata[2] = length;
    memcpy(&sdata[3], data, length);
    md_calc_check_sum(sdata, sizeof(sdata) - 1);
    smsg_push(sdata, sizeof(sdata));
}

void md_devs_change(uint8_t devs, bool reset) __attribute__((weak));
void md_devs_change(uint8_t devs, bool reset) {
    selected_device = devs;
    if (devs > DEVS_2G4) return;
    md_send_devctrl(gmk70_host_command(devs));
    if (!reset || devs == DEVS_USB) return;
    if (devs == DEVS_2G4) {
        // Native identity encoding differs before/after stock radio v48.
        if (md_get_version() < 48) {
            md_send_manufacturer(MD_DONGLE_MANUFACTURER, strlen(MD_DONGLE_MANUFACTURER));
            md_send_product(MD_DONGLE_PRODUCT, strlen(MD_DONGLE_PRODUCT));
        } else {
            md_send_manufacturer((char *)USBSTR(MD_DONGLE_MANUFACTURER), sizeof(USBSTR(MD_DONGLE_MANUFACTURER)));
            md_send_product((char *)USBSTR(MD_DONGLE_PRODUCT), sizeof(USBSTR(MD_DONGLE_PRODUCT)));
        }
        md_send_vpid(VENDOR_ID, PRODUCT_ID);
        md_send_devctrl(MD_SND_CMD_DEVCTRL_CLEAN);
    } else {
        static const char *const names[] = {MD_BT1_NAME, MD_BT2_NAME, MD_BT3_NAME, MD_BT4_NAME, MD_BT5_NAME};
        md_send_devctrl(MD_SND_CMD_DEVCTRL_CLEAN);
        md_send_devinfo(names[devs - DEVS_BT1]);
    }
    md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
}

bool md_inquire_bat(void) {
    if (smsg_is_busy()) {
        return false;
    }

    md_send_devctrl(MD_SND_CMD_DEVCTRL_INQVOL);

    return true;
}
