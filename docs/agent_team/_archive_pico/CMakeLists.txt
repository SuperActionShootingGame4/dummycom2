# build_probe / DEBUGGER T04 準備フェーズ
# 目的: dummy_intercom (rev 0.5) のビルド環境健全性を検証する隔離プロジェクト。
#   - pico2_w ボードで cmake 構成が通るか
#   - pico_btstack_classic / pico_btstack_cyw43 がリンクできるか
#   - tinyusb_device (UAC + CDC 複合) がリンクできるか
#   - RP2350 (PICO_PLATFORM=rp2350) のコンパイルが通るか
# ※ src/ の本実装とは無関係。CODER 実装前の環境検証専用。

cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(build_probe C CXX ASM)

pico_sdk_init()

add_executable(probe
    probe_src/main.c
    probe_src/usb_descriptors.c
)

target_include_directories(probe PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/probe_src
)

target_link_libraries(probe
    pico_stdlib
    pico_multicore
    # --- Bluetooth: CYW43 + BTstack Classic (HFP-AG / SCO / CVSD-PLC) ---
    # BT のみ利用のため lwip 非依存の cyw43_arch_none を使用(pico-examples の
    # pico_w/bt/* と同じ構成)。CYW43_ENABLE_BLUETOOTH は pico_btstack_cyw43 が定義。
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_cyw43
    # --- USB: TinyUSB device, UAC + CDC composite ---
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_usb(probe 0)
pico_enable_stdio_uart(probe 1)

pico_add_extra_outputs(probe)
