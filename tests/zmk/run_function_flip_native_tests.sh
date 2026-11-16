#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
for radio in 0 1; do
    for separate_mod_release in 0 1; do
        flags=(-std=c11 -D_XOPEN_SOURCE=700 -pthread -Wall -Wextra -Werror -O1 -g -fno-pie -fsanitize=address,undefined
            -DCONFIG_ZMK_HID_REPORT_TYPE_NKRO=1 -DCONFIG_ZMK_HID_REPORT_TYPE_HKRO=0
            -DCONFIG_GMK70_FUNCTION_FLIP=1 -DCONFIG_GMK70_NKRO=1
            -DCONFIG_ZMK_USB_BOOT=1 -DCONFIG_ZMK_POINTING=0
            -DCONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6
            -DCONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT="$separate_mod_release"
            -include tests/zmk/function_flip_native_prelude.h -Itests/zmk/function_flip_native_stubs
            -Itests/zmk/function_flip_stubs -Itests/zmk/radio_kernel_stubs
            -Itests/zmk/radio_endpoint_stubs -Idependencies/zmk/app/include -Ifirmware/zmk/update
            -Ifirmware/common/update
            -Ifirmware/zmk/nkro -Ifirmware/common/radio)
        sources=()
        if [[ "$radio" == 1 ]]; then
            flags+=(-DCONFIG_GMK70_RADIO=1)
            sources+=(firmware/zmk/radio/endpoint.c)
        fi
        for native_abi in original optimized; do
            native_flags=()
            native_sources=()
            if [[ "$native_abi" == optimized ]]; then
                native_flags=(-flto
                    -Dzmk_hid_keyboard_clear=gmk_native_hid_keyboard_clear
                    -Dzmk_hid_consumer_clear=gmk_native_hid_consumer_clear
                    -Dzmk_hid_mouse_clear=gmk_native_hid_mouse_clear
                    -Dzmk_hid_implicit_modifiers_press=gmk_native_hid_implicit_modifiers_press
                    -Dzmk_hid_implicit_modifiers_release=gmk_native_hid_implicit_modifiers_release
                    -Dzmk_hid_press=gmk_native_hid_press -Dzmk_hid_release=gmk_native_hid_release
                    -Dzmk_hid_get_keyboard_report=gmk_native_hid_get_keyboard_report
                    -Dzmk_hid_get_boot_report=gmk_native_hid_get_boot_report)
                native_sources=(firmware/zmk/runtime/native_hid.c)
            fi
            # Pinned upstream boot packing uses int versus sizeof; do not edit upstream.
            cc "${flags[@]}" "${native_flags[@]}" -Wno-sign-compare -c dependencies/zmk/app/src/hid.c \
                -o ".work/zmk/tests/function-flip-native-hid-$separate_mod_release.o"
            cc "${flags[@]}" -no-pie \
                tests/zmk/function_flip_native_test.c ".work/zmk/tests/function-flip-native-hid-$separate_mod_release.o" \
                firmware/zmk/function_flip/core.c firmware/zmk/studio/bool_setting.c \
                tests/zmk/function_flip_native_nkro.c "${sources[@]}" "${native_sources[@]}" \
                firmware/common/radio/radio_keys.c \
                -Wl,--wrap=zmk_hid_get_keyboard_report -Wl,--wrap=zmk_hid_get_boot_report \
                -Wl,--wrap=zmk_hid_implicit_modifiers_press -Wl,--wrap=zmk_hid_implicit_modifiers_release \
                -Wl,--wrap=zmk_hid_keyboard_clear -Wl,--wrap=zmk_hid_consumer_clear \
                -Wl,--wrap=zmk_hid_press -Wl,--wrap=zmk_hid_release \
                -Wl,--wrap=zmk_hid_is_pressed -Wl,--wrap=zmk_hid_register_mods \
                -o ".work/zmk/tests/function-flip-native-$radio-$separate_mod_release-$native_abi"
            ".work/zmk/tests/function-flip-native-$radio-$separate_mod_release-$native_abi"
        done
    done
done
