/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <zephyr/drivers/usb/usb_dc.h>
int usb_enable(usb_dc_status_callback callback);
int usb_disable(void);
#ifdef __cplusplus
}
#endif
