/* Copyright 2024 yangzheng20003 (@yangzheng20003)
 * Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
/* Preprocessor-only board data is also safe in native generated config. */
#define GMK70_LED_LEFT 36
#define GMK70_LED_RIGHT 41
#define GMK70_LED_COUNT (GMK70_LED_LEFT + GMK70_LED_RIGHT)
#define GMK70_BATTERY_LED_INDICES {5, 6, 7, 8, 9, 39, 40, 41, 42, 43}
#define GMK70_BATTERY_LED 33
#define GMK70_CAPS_LED 18
#define GMK70_GUI_LED 73
#define GMK70_WINDOWS_LED 19
#define GMK70_MAC_LED 20
#define GMK70_BT1_LED 14
#define GMK70_BT2_LED 13
#define GMK70_BT3_LED 12
#define GMK70_DONGLE_LED 11

#define GMK70_BATTERY_WARNING_LEVEL 15
#define GMK70_BATTERY_STOP_LEVEL 0
