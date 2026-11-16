# SPDX-License-Identifier: Apache-2.0
"""Use pinned Zephyr disconnect handling in the USB cable lifecycle fixture."""

import sys
from pathlib import Path

from test_nkro_boot import NkroBootTests


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    source = (root / "dependencies/zephyr/subsys/usb/device/usb_device.c").read_text()
    functions: list[str] = []
    for prefix in (
        "static void usb_reset_alt_setting(",
        "static uint32_t get_ep_bm_from_addr(",
        "static int foreach_ep(",
        "static int disable_interface_ep(",
        "static void forward_status_cb(",
    ):
        begin = source.index(prefix)
        signature = source[begin : source.index("{", begin)].strip()
        functions.append(NkroBootTests.function(source, signature))
    Path(sys.argv[1]).write_text("\n".join(functions))


if __name__ == "__main__":
    main()
