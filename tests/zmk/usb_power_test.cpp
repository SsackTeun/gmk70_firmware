/* SPDX-License-Identifier: Apache-2.0 */
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <zephyr/init.h>
#include <zephyr/usb/usb_device.h>
#include "../../firmware/zmk/drivers/gpio/gpio_wb32.c"

/* Actual GPIO/controller implementations, bounded MMIO/time/lifecycle model. */
static uint32_t                   registers[2][13], initial[2][13], now;
static unsigned                   writes, preload, output, starts, stops, disconnected, initializations, cancellations, class_disconnects, control_completions;
static bool                       clock_on, transfer_pending;
unsigned                          power_irq_depth;
static wb32_gpio                  port_b   = {{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}, {10}, {11}, {12}};
static wb32_gpio                  port_a   = {{13}, {14}, {15}, {16}, {17}, {18}, {19}, {20}, {21}, {22}, {23}, {24}, {25}};
static const wb32_gpio_config     config_b = {{0xffff}, &port_b, 6}, config_a = {{0xffff}, &port_a, 5};
static wb32_gpio_data             data_b, data_a;
extern "C" const device           power_gpio_b = {&config_b, &wb32_gpio_api, true, &data_b};
extern "C" const device           power_gpio_a = {&config_a, &wb32_gpio_api, true, &data_a};
extern "C" const power_init_entry power_registration __attribute__((weak));

power_register::operator uint32_t() const {
    return registers[index / 13][index % 13];
}
void power_register::operator=(uint32_t value) {
    unsigned  slot = index % 13;
    uint32_t *bank = registers[index / 13];
    ++writes;
    if (slot == 12) {
        bank[slot] = value;
        return;
    }
    if (slot == 6) {
        bank[5]    = (bank[5] | (value & 0xffff)) & ~(value >> 16);
        bank[slot] = value;
        if (index / 13 == 0 && !preload) preload = writes;
        return;
    }
    uint32_t mask  = 0;
    unsigned width = slot == 1 ? 1 : 2;
    for (unsigned pin = 0; pin < 16; ++pin)
        if (!(bank[12] & BIT(pin))) mask |= ((1U << width) - 1U) << (pin * width);
    if (slot == 0 && index / 13 == 0) {
        assert(preload && (bank[5] & BIT(1)));
        output = writes;
    }
    bank[slot] = (bank[slot] & ~mask) | (value & mask);
}
static uint32_t exti[6];
uint32_t        sys_read32(uintptr_t address) {
    assert(address >= EXTI_IMR && address <= EXTI_PR);
    return exti[(address - EXTI_IMR) / 4];
}
void sys_write32(uint32_t value, uintptr_t address) {
    assert(address >= EXTI_IMR && address <= EXTI_PR);
    uint32_t &reg = exti[(address - EXTI_IMR) / 4];
    if (address == EXTI_PR)
        reg &= ~value;
    else
        reg = value;
}
void sys_set_bits(uintptr_t address, uint32_t bits) {
    if (address == WB32_RCC_BASE + 0x48) {
        assert(bits == (BIT(15) | BIT(6) | BIT(9) | BIT(10)) || bits == (BIT(15) | BIT(5) | BIT(9) | BIT(10)));
    } else
        sys_write32(sys_read32(address) | bits, address);
}
void sys_clear_bits(uintptr_t address, uint32_t bits) {
    sys_write32(sys_read32(address) & ~bits, address);
}
extern "C" {
void (*fake_usb_isr)(const void *);
uint32_t k_uptime_get_32(void) {
    return now;
}
bool usb_get_remote_wakeup_status(void) {
    return false;
}
void k_busy_wait(uint32_t) {
    assert(false);
}
void            gmk_usb_power_poll(void) __attribute__((weak));
static uint8_t  usb_registers[0x30];
static uint8_t  setup_packet[8];
static unsigned setup_cursor = sizeof(setup_packet);
uint8_t         sys_read8(uintptr_t address) {
    assert(clock_on && address >= 0x40014000UL && address < 0x40014030UL);
    if (address == 0x40014020UL && setup_cursor < sizeof(setup_packet)) return setup_packet[setup_cursor++];
    return usb_registers[address - 0x40014000UL];
}
void sys_write8(uint8_t value, uintptr_t address) {
    assert(clock_on && address >= 0x40014000UL && address < 0x40014030UL);
    usb_registers[address - 0x40014000UL] = value;
}
void wb32_usb_clock_enable(void) {
    assert(!clock_on);
    if (EXPECT_WIRED) assert(((registers[0][0] >> 2) & 3) == 1 && (registers[0][5] & BIT(1)) && preload < output);
    clock_on = true;
    ++starts;
}
void wb32_usb_disconnect(void) {
    assert(clock_on && (registers[0][5] & BIT(1)));
}
void wb32_usb_clock_disable(void) {
    assert(clock_on);
    wb32_usb_disconnect();
    clock_on = false;
    ++stops;
}
static void status(enum usb_dc_status_code code, const uint8_t *) {
    if (code == USB_DC_DISCONNECTED) ++disconnected;
}
struct usb_ep_cfg_data {
    uint8_t ep_addr;
};
struct usb_cfg_data {
    usb_ep_cfg_data *endpoint;
    uint8_t          num_endpoints;
    void (*cb_usb_status)(usb_cfg_data *, enum usb_dc_status_code, const uint8_t *);
};
static struct {
    bool                   configured;
    uint32_t               ep_bm;
    uint8_t                alt_setting[2];
    usb_dc_status_callback user_status_callback;
} usb_dev;
static void class_status(usb_cfg_data *, enum usb_dc_status_code code, const uint8_t *) {
    if (code == USB_DC_DISCONNECTED) {
        assert(!usb_dev.configured && !transfer_pending && !usb_dev.ep_bm);
        ++class_disconnects;
    }
}
static usb_ep_cfg_data native_endpoints[] = {{0x81}, {0x01}};
static usb_cfg_data    native_classes[]   = {{native_endpoints, 2, class_status}};
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define USB_EP_DIR_IN 0x80
#define LOG_ERR(...)
#define STRUCT_SECTION_FOREACH(type, iterator) for (type *iterator = native_classes; iterator != native_classes + ARRAY_SIZE(native_classes); ++iterator)
void usb_cancel_transfers(void) {
    assert(!clock_on && usb_dev.configured);
    transfer_pending = false;
    ++cancellations;
}
#include "native_usb_power.h"
static void control_done(uint8_t ep, enum usb_dc_ep_cb_status_code code) {
    assert(ep == 0x80 && code == USB_DC_EP_DATA_IN);
    ++control_completions;
}
int usb_enable(usb_dc_status_callback callback) {
    assert(!initializations++);
    usb_dev.user_status_callback = callback;
    usb_dc_set_status_callback(forward_status_cb);
    int result = usb_dc_attach();
    assert(!usb_dc_ep_set_callback(0x80, control_done));
    return result;
}
}

static void configure_host(void) {
    usb_dev.configured     = true;
    usb_dev.alt_setting[0] = 1;
    usb_dev.ep_bm          = BIT(1) | BIT(17);
    for (const auto &ep : native_endpoints) {
        const usb_dc_ep_cfg_data config = {ep.ep_addr, 64, USB_DC_EP_BULK};
        assert(!usb_dc_ep_configure(&config) && !usb_dc_ep_enable(ep.ep_addr));
    }
    transfer_pending = true;
}

int main(int argc, char **argv) {
    (void)argv;
    for (auto &bank : registers) {
        for (auto &reg : bank)
            reg = 0xa5a5a5a5U;
        bank[12] = 0xffff;
    }
    registers[0][0] &= ~(3U << 2);
    registers[0][5] |= BIT(1);
    registers[1][4] &= ~BIT(7);
    std::memcpy(initial, registers, sizeof initial);
    assert(!wb32_gpio_init(&power_gpio_b) && !wb32_gpio_init(&power_gpio_a));
    if (!EXPECT_WIRED) {
        assert(!&power_registration && !gmk_usb_power_poll);
        assert(!usb_dc_attach() && starts == 1 && writes == 0);
        assert(!std::memcmp(initial, registers, sizeof initial));
        return 0;
    }
    assert(power_registration.level == POST_KERNEL && power_registration.priority == 88);
    assert(!power_registration.init() && !usb_enable(status) && clock_on);
    if (argc > 1) {
        registers[1][4] |= BIT(7);
        for (now = 0; now < 100; ++now)
            gmk_usb_power_poll();
        assert(clock_on && starts == 1 && !stops && !(registers[0][5] & BIT(1)));
        registers[1][4] &= ~BIT(7);
        gmk_usb_power_poll();
        now += 50;
        gmk_usb_power_poll();
    }
    /* Native startup initializes once with the external pull-up still off.
     * The first stable unplugged observation parks the controller. */
    for (now = 0; now < 10000; ++now)
        gmk_usb_power_poll();
    assert(starts == 1 && stops == 1 && !clock_on && disconnected == 1);
    starts = stops = disconnected = class_disconnects = 0;
    registers[1][4] |= BIT(7);
    gmk_usb_power_poll();
    now += 49;
    gmk_usb_power_poll();
    assert(!starts);
    ++now;
    gmk_usb_power_poll();
    assert(clock_on && starts == 1 && !(registers[0][5] & BIT(1)));
    for (unsigned mode = 0; mode < 7; ++mode) {
        ++now;
        gmk_usb_power_poll();
    }
    assert(starts == 1 && !stops);
    configure_host();
    registers[1][4] &= ~BIT(7);
    gmk_usb_power_poll();
    now += 10;
    registers[1][4] |= BIT(7);
    gmk_usb_power_poll();
    now += 50;
    gmk_usb_power_poll();
    assert(!stops);
    registers[1][4] &= ~BIT(7);
    gmk_usb_power_poll();
    now += 50;
    gmk_usb_power_poll();
    assert(!clock_on && stops == 1 && disconnected == 1 && (registers[0][5] & BIT(1)));
    assert(cancellations == 1 && class_disconnects == 1 && !usb_dev.configured && !usb_dev.alt_setting[0]);
    now = UINT32_MAX - 20;
    registers[1][4] |= BIT(7);
    gmk_usb_power_poll();
    now += 50;
    gmk_usb_power_poll();
    assert(clock_on && starts == 2 && !(registers[0][5] & BIT(1)));
    assert(initializations == 1);
    uint8_t       control = 0;
    uint32_t      sent    = 0;
    const uint8_t setup[] = {0x80, 6, 0, 1, 0, 0, 1, 0};
    std::memcpy(setup_packet, setup, sizeof(setup_packet));
    setup_cursor        = 0;
    usb_registers[0x16] = 8;
    usb_registers[0x11] = 1;
    usb_registers[0x02] = 1;
    fake_usb_isr(nullptr);
    usb_registers[0x11] = 0;
    assert(!usb_dc_ep_write(0x80, &control, 1, &sent) && sent == 1);
    usb_registers[0x11] &= ~2U;
    usb_registers[0x02] = 1;
    fake_usb_isr(nullptr);
    assert(control_completions == 1); /* Native EP0 callback survives reattach. */
    usb_registers[0x02] = 0;
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        configure_host();
        registers[1][4] &= ~BIT(7);
        gmk_usb_power_poll();
        now += 50;
        gmk_usb_power_poll();
        assert(!clock_on && !transfer_pending && !usb_dev.configured);
        registers[1][4] |= BIT(7);
        gmk_usb_power_poll();
        now += 50;
        gmk_usb_power_poll();
        assert(clock_on && initializations == 1);
    }
    assert(cancellations == 21 && class_disconnects == 21);
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned slot : {0U, 1U, 2U, 3U, 5U, 11U}) {
            unsigned pin  = bank ? 7 : 1;
            uint32_t mask = slot == 1 || slot == 5 ? BIT(pin) : (3U << (2 * pin));
            assert((registers[bank][slot] & ~mask) == (initial[bank][slot] & ~mask));
        }
    std::puts("USB cable lifecycle: native disconnect cancellation, retained EP0, repeated cycles, startup, MMIO exclusion, debounce and wrap passed");
}
