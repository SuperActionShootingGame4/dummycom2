#!/usr/bin/env bash
# build_probe.sh — DEBUGGER T04 ビルド環境健全性検証(隔離プロジェクト build_probe/)
# 目的: pico2_w / RP2350 で BTstack Classic(HFP-AG/SCO/CVSD-PLC)+ TinyUSB(UAC+CDC)+
#       pico_multicore がリンクできることを確認する。src/ の本実装とは無関係。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROBE="$ROOT/build_probe"
BUILD="$PROBE/build"

source /home/subcon/opt/pico-env.sh

rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"
cmake -DPICO_BOARD=pico2_w -DPICO_PLATFORM=rp2350 "$PROBE"
make -j"$(nproc)" probe

echo
echo "=== 成果物 ==="
ls -la "$BUILD"/probe.uf2 "$BUILD"/probe.elf
arm-none-eabi-size "$BUILD"/probe.elf
