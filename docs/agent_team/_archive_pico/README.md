# _archive_pico — 旧 Pico 2 W 資産(rev 0.4/0.5、破棄済み)

仕様 rev 0.6 で Raspberry Pi Pico 2 W プラットフォームは破棄された
(CYW43439 の SCO-over-HCI 不具合 pico-sdk #1461 により HFP 音声受信不可)。

ここにある DEBUGGER の Pico 用ビルド検証資産(2026-09-02 T04 準備フェーズ 第1版)は
参考として退避。**現行のビルド検証は ESP-IDF 版(`build_probe_espidf/`)を参照。**

| ファイル | 旧内容 |
|---|---|
| `CMakeLists.txt` | pico2_w / pico-sdk 2.1.1 のビルド検証プロジェクト |
| `probe_src/` | BTstack Classic(HFP-AG)+ TinyUSB(UAC+CDC)+ multicore のリンク検証スケルトン |
| `build_probe.sh` | 旧検証スクリプト |

Pico 版検証結果(参考): 全ライブラリがリンク可能で UF2 生成まで到達したが、
SCO-over-HCI は「ビルドは通るが実機で動作しない」ため rev 0.6 で不採用。
