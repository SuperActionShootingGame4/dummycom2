#!/usr/bin/env bash
# build_probe_espidf.sh — DEBUGGER T04 ビルド環境検証(ESP-IDF / spec rev 0.6)
# 目的: examples/bluetooth/bluedroid/classic_bt/hfp_ag をベースに、
#       esp32 ターゲットで HFP-AG + HCI データパス + CVSD(WBS 無効)がビルドできることを確認。
#       src/ の本実装とは無関係。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROBE="$ROOT/build_probe_espidf"

export IDF_TOOLS_PATH=/home/subcon/.espressif
# cmake / ninja は idf_tools 導入分を使う(pico-env の cmake とは混ぜない)
. /home/subcon/esp/esp-idf/export.sh

cd "$PROBE"
rm -rf build sdkconfig
idf.py set-target esp32
idf.py build

echo
echo "=== 主要 sdkconfig ==="
grep -E 'CONFIG_BT_HFP_AG_ENABLE|CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI|CONFIG_BT_HFP_WBS_ENABLE|CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY|CONFIG_BT_A2DP_ENABLE|CONFIG_ESPTOOLPY_FLASHSIZE=' "$PROBE/sdkconfig"
echo
echo "=== サイズ ==="
idf.py size | grep -iE 'Total sizes|Flash Code|Flash Data|DRAM|IRAM' || true
