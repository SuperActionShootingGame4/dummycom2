# Dummy Intercom — 受信専用 HFP-AG → アナログ音声ブリッジ

SENA SRL-3(または同等の Sena インターコム)に **HFP Audio Gateway**(「電話」役)
として接続する ESP32-WROOM-32 用ファームウェアです。インターコムの音声を
**SCO** リンク経由・**HCI データパス**で受信し(**CVSD 8 kHz**、または相手側が
選択すれば **mSBC/WBS 16 kHz**)、48 kHz にアップサンプリングして(DC ブロック
ハイパス + ノイズゲート + ポリフェーズ FIR、`audio_dsp.c`)、
**I2S0 → PCM5102A → 3.5 mm アナログ(モノラル)** から出力します。

SRL-3 へのアップリンクは無音のみです。電話機能・A2DP・AVRCP・Wi-Fi・USB は
一切ありません。

- ターゲット: `esp32`(オリジナル ESP32、BR/EDR Classic 必須 — S2/S3/C3/C6/H2 では動作しません)
- SDK: ESP-IDF v5.4、Bluedroid ホスト
- 仕様書: `dummy_intercom_spec.md` rev 0.10 ・ 設計書: `docs/agent_team/software_design.md`

## ビルド

```sh
export IDF_TOOLS_PATH=/home/subcon/.espressif
. /home/subcon/esp/esp-idf/export.sh
cd /home/subcon/SOURCE_CODE/android_BT_TEST
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

> **mSBC を使うには ESP-IDF のパッチが必要です。** SENA SRL-3 は HFP 1.9 の
> LC3-SWB コーデックを広告してきますが、これが Bluedroid の `AT+BAC` パーサの
> バグを誘発し、CVSD に固定されてしまいます。ビルド前に
> `patches/esp-idf-v5.4-hfp-bac-lc3.patch` を `~/esp/esp-idf` に適用してください
> (`patches/README.md` 参照)。適用しなくてもリンク自体は成立しますが、狭帯域の
> CVSD のままになります。

`sdkconfig.defaults` では既に Voice-over-HCI 構成が選択されています
(`BT_HFP_AUDIO_DATA_PATH_HCI=y`、`BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y`、
`BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1`、`BT_HFP_WBS_ENABLE=y` — AG は CVSD と
mSBC の両方を提供します。CVSD のみに固定したい場合は `=n` にしてください)。
また、パーティションテーブルもカスタム設定済みです
(`partitions.csv`、`factory` = 0x1E0000、4 MB フラッシュ)。

## 配線

| 信号          | ESP32 GPIO | 接続先                     |
|---------------|-----------:|----------------------------|
| I2S BCLK      | 26         | PCM5102A BCK               |
| I2S LRCLK/WS  | 25         | PCM5102A LRCK              |
| I2S DOUT      | 27         | PCM5102A DIN               |
| 3V3 / GND     | 3V3 / GND  | PCM5102A VIN / GND         |
| LED PWR       | 32         | LED + 1 kΩ → GND(起動 OK) |
| LED LINK      | 33         | LED + 1 kΩ → GND(ACL/SLC) |
| LED SCO       | 13         | LED + 1 kΩ → GND(音声)    |
| BOND-CLR ボタン | 14       | タクトスイッチ → GND(内部プルアップ) |

PCM5102A: SCK → GND(内蔵 PLL 使用)、FLT/DEMP/XSMT はモジュールのデフォルトの
まま。出力段(L/R 合成、レベルトリム、LINE/MIC パッド、絶縁トランス)は
仕様書 §4.4 を参照してください。基板はスマートフォンやカメラの Wi-Fi から
10 cm 以上離してください。

## 2×ESP32 I2S デジタル直結(CR-012)

`main/app_config.h` の `AUDIO_OUT_MODE` で音声出力を切り替えます(コンパイル時)。

- `AUDIO_OUT_I2S16K`(既定)= 16 kHz / 16 bit / モノ / Philips / 左スロットの I2S **マスタ**出力。2 台目 ESP32(`hf_unit/`)へ直結します。CVSD(8 kHz)は 16 kHz へ 2 倍アップサンプルし、mSBC(16 kHz)はそのまま出力します。
- `AUDIO_OUT_DAC48K` = 従来の 48 kHz ステレオ出力(PCM5102A 用)。**PCM5102A 構成に戻すには、この値に変更して再ビルドします。**

配線(1 台目 → 2 台目): BCLK=GPIO26、WS=GPIO25、DOUT=GPIO27(2 台目の DIN=GPIO27)の 3 本、GND 共通、電源は 1 台目の 5 V を 2 台目へ供給。詳細は `docs/wiring_dual_esp32.md`。

## LED の状態

| LED  | 消灯            | 点滅                       | 点灯             |
|------|-----------------|----------------------------|------------------|
| PWR  | —               | 5 Hz = BT 起動失敗          | 起動完了         |
| LINK | 未接続          | 1 Hz = ページング/再接続中  | ACL/SLC 確立済み |
| SCO  | 音声リンクなし  | 約 10 Hz = バッファ異常     | SCO 確立中(受信) |

## 役割(AG / HF)の切り替え(CR-011)

`main/app_config.h` の `BT_ROLE` でビルド時に選びます(1 起動 1 役)。

| BT_ROLE | 用途 |
|---|---|
| `BT_ROLE_AG`(従来の既定) | SRL-3 の phone pairing(電話ペアリング)。SRL-3 が HF、dummycom が AG |
| `BT_ROLE_HF` | SRL-3 のユニバーサルインターコム・ペアリング(SRL-3 が AG、dummycom がヘッドセット役)。技術検証中 |

**現在のツリーは実機検証のため `BT_ROLE_HF` です**(検証後に既定を確定します)。HF 役で期待するログ: `HFP-HF ready`、`paired:`、`connection state: SLC_CONNECTED`、通話時の `audio state: connected`。

`BT_ROLE_HF` のビルドには、ESP-IDF パッチ `patches/esp-idf-v5.4-hfp-client-no-post-slc-at.patch` の適用が必要です(適用方法は `patches/README.md`)。

役を切り替えたら、旧ボンドを消すため BOND-CLR を 3 秒長押ししてください。

## 初回ペアリング

1. 基板を書き込んで電源を入れます。`DummyIntercom` は、SLC(HFP 接続)が確立していない間は常に検出可能な状態です。
2. SRL-3 を **phone pairing** モードにし、`DummyIntercom` を選択します(レガシー PIN `0000`、ペア番号の確認なし。CR-010・技術検証用)。旧 SSP ボンドが残っている場合は先に BOND-CLR を 3 秒長押しして消去してください。
3. シリアルログに `SLC connected` が表示され、相手の BDA が NVS に書き込まれます。
4. 接続中は非検出、切断中は再び検出可能となり(CR-010)、保存した相手へ自動再接続します。

**BOND-CLR(GPIO14)を 3 秒間長押し**すると、ボンドと保存済みの相手情報が消去され再起動します。

### ホスト PC からコマンドでペアリング解除する方法

このファームウェアにはシリアルコンソールのコマンド受付機能はありません。
`idf.py monitor` に何か入力しても意味はありません。GPIO14 のボタンに手が届か
ない場合(筐体に収めた後・ベンチ環境など)は、代わりに PC 側から USB/UART 経由
で同じ NVS データを消去できます。使用するのは `partitions.csv` で定義されている
`nvs` パーティションです(オフセット `0x9000`、サイズ `0x6000`)。

```sh
# 推奨: parttool.py で nvs パーティションだけを名前指定で消去する
python $IDF_PATH/components/partition_table/parttool.py -p /dev/ttyUSB0 \
  erase_partition --partition-name=nvs

# 代替: esptool.py で同じ領域を直接消去する(ツールチェーンのラッパーを経由しない方法)
esptool.py --port /dev/ttyUSB0 erase_region 0x9000 0x6000
```

どちらのコマンドも、保存済みの相手 BDA と Bluedroid のボンドテーブルを
BOND-CLR と同様に消去します。実行後は基板を電源再投入(またはリセット)して
ください。フラッシュ直後と同様に、検出可能な状態に戻り再ペアリングできる
ようになります。

## コマンドラインからできるその他の操作

日常の運用・トラブルシューティング用コマンドです。プロジェクトディレクトリで
`. /home/subcon/esp/esp-idf/export.sh` を実行した後に使用します。

| コマンド | 内容 |
|---|---|
| `idf.py set-target esp32` | チップターゲットを選択する(`esp32` である必要があります。S2/S3/C3/C6/H2 は不可) |
| `idf.py build` | ファームウェアをビルドする |
| `idf.py -p /dev/ttyUSB0 flash` | ビルド済みファームウェアを書き込む |
| `idf.py -p /dev/ttyUSB0 monitor` | シリアルログモニタを開く(終了は Ctrl+]) |
| `idf.py -p /dev/ttyUSB0 flash monitor` | 書き込み後、続けてモニタを開く |
| `idf.py menuconfig` | `sdkconfig` を対話的に編集する(例: `BT_HFP_WBS_ENABLE` の切り替え) |
| `idf.py size` / `idf.py size-components` | フラッシュ/RAM 使用量を全体または各コンポーネント単位で表示する |
| `idf.py partition-table` | パース済みのパーティションテーブル(オフセット/サイズ)を表示する |
| `idf.py erase-flash` | フラッシュチップ**全体**(app + NVS + phy_init)を消去する — ペアリング情報も消えるが、消去後は再度書き込むまで基板は起動しない |
| `python $IDF_PATH/components/partition_table/parttool.py -p PORT get_partition_info --partition-name=nvs --info offset size` | `nvs` パーティションの実際のオフセット/サイズを表示する(`erase_region` を実行する前の確認用) |
| `python $IDF_PATH/components/partition_table/parttool.py -p PORT read_partition --partition-name=nvs --output nvs.bin` | NVS パーティションの生データをファイルに書き出す(消去前に保存済みボンド情報をバックアップ/確認する用途) |
| `esptool.py --port PORT chip_id` / `flash_id` | チップ/フラッシュの識別情報を読み出す。基板が正しく認識されない場合の切り分けに使用 |

## 動作に関する補足

- SLC キープアライブ: SLC が確立している間、AG は `audio_connect` を 3 秒ごとに
  再試行し、SCO が維持されるようにします(受信専用・常時 SCO でも問題ありません)。
- 再接続: ACL が切断されると、AG は保存済みの相手に対して 5 秒ごとに
  `slc_connect` を呼び出します。
- 発信のふり: SRL-3 からの `ATD` / `AT+BLDN` / `AT+CKPD` には応答を返し、ヘッドセット
  側が音声を開くところまで進めます。AG は疑似的な通話を `call=1` まで進行させます
  (`bt_hf_ag.c` の `handle_dial_finish()` に、この簡略化されたフォールバックの説明が
  あります)。
- ジッタバッファ: 目標 60 ms、プリフィル 30 ms(`main/app_config.h` の
  `JITTER_TARGET_BYTES` / `JITTER_PREFILL_BYTES`。ドロップ耐性よりも遅延を優先する
  場合は 640 に下げる、または DMA を低くすると約 40 ms になります)。
- 音声 DSP(`main/audio_dsp.c`、I2S TX タスク内で動作): 120 Hz の DC ブロック
  ハイパス → 任意のダウンワードエキスパンダ型ノイズゲート(`app_config.h` の
  `AUDIO_GATE_*`、**デフォルトは無効** — 有効化するには `AUDIO_GATE_ENABLE 1`)→
  固定 48 kHz の I2S レートへのポリフェーズ FIR アップサンプル(CVSD からは ×6、
  mSBC からは ×3。カイザー窓 sinc、CVSD 帯域は 3.4 kHz までフラット)。FIR の
  再生成は `tools/gen_fir.py` で行います。
- コーデック: WBS が有効なため、AG は CVSD と mSBC の両方を提供します。mSBC では
  受信 PCM が 16 kHz となり、音声パスは自動的に切り替わります
  (`bt_hf_ag.c` の `ESP_HF_AUDIO_STATE_CONNECTED_MSBC`)。
- 受信 PCM の RMS を 1 秒ごとにログ出力します(発話/グループ音声の確認用、R1)。

## ハードウェアスモークテスト(ESP32 + SRL-3 実機が必要 — 本リポジトリ内では未実施)

| # | 手順 | 合格基準 |
|---|------|----------|
| T1 | 電源投入 | 10 秒以内に検出可能な状態になる / 保存済みの相手がいれば再接続が始まる |
| T2 | SRL-3 の phone-pairing からペアリング | PIN 0000 で `SLC connected` がログに出る、NVS に保存される |
| T3 | SRL-3 で universal intercom を開始 | **3 秒以内に SCO 確立、SCO LED 点灯、J1 で音声出力** ← 最もリスクの高い項目(ISS-008) |
| T4 | 2 台目のインターコムが発話 + 装着者が発話 | 音声が聞き取れる。グループ音声については RMS ログを確認 |
| T5 | SRL-3 のマイクに拍手音を入力し、スコープで J1 と比較 | 100 ms 未満、一定 |
| T6 | 30 分間連続稼働 | 3 秒以上のドロップアウトがない |
| T8 | SRL-3 の電源再投入 / 距離を離す | 自動再接続、LINK/SCO が復帰する |
| T12 | BOND-CLR 3 秒長押し | 消去され、再起動し、再ペアリング可能になる |
| T13 | ログ / btmon: BRSF / AT+BAC / BCS / SCO パラメータ | コーデックがログに出る(CVSD または mSBC)。パスが `SCO PCM … Hz` のログ行と一致する |

**T3(SCO 確立)は本プロジェクトにおいて最も大きな未検証項目です** —
ビルドと静的レビューは完了していますが、実機(ハードウェアなし)での SRL-3 との
実際の SCO 音声のやり取りは検証されていません。SCO がハングする場合は、まず
recv/send コールバックの登録を audio-connect のタイミングへ移動してください
(`main/bt_data_path.c` の U-02 コメント参照)。
