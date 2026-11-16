#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/qmk
for framework in qmk vial-qmk; do
    cc -std=c11 -Wall -Wextra -Werror -g -O1 -fno-pie -no-pie \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -Itests/qmk/usb_power_stubs -Itests/qmk/stubs \
        -Ifirmware/qmk-common/keyboards/zuoya/gmk70 -Ifirmware/qmk-common/drivers/wireless \
        -I"dependencies/$framework/quantum" -I"dependencies/$framework/quantum/keymap_extras" \
        -I"dependencies/$framework/tmk_core/protocol" \
        tests/qmk/usb_power_test.c firmware/qmk-common/keyboards/zuoya/gmk70/usb_power.c \
        -o "artifacts/tests/qmk/usb-power-$framework"
    "artifacts/tests/qmk/usb-power-$framework"
    "artifacts/tests/qmk/usb-power-$framework" connected-boot
done
