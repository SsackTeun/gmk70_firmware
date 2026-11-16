#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/common
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -fno-pie -no-pie -fsanitize=address,undefined)
cc "${flags[@]}" tests/common/shared_core_test.c -o artifacts/tests/common/core
artifacts/tests/common/core
for framework in qmk vial-qmk; do
    cc "${flags[@]}" -Wconversion -Wsign-conversion \
        -Itests/qmk/function_flip_stubs -Ifirmware/qmk-common/keyboards/zuoya/gmk70 \
        -I"dependencies/$framework/quantum" -I"dependencies/$framework/quantum/keymap_extras" \
        -I"dependencies/$framework/quantum/sequencer" \
        -I"dependencies/$framework/tmk_core/protocol" \
        tests/common/flip_adapter_trace.c firmware/qmk-common/keyboards/zuoya/gmk70/function_flip.c \
        firmware/zmk/function_flip/core.c -o "artifacts/tests/common/flip-$framework"
    "artifacts/tests/common/flip-$framework"
done
