# ダミーインカム(受信専用 / アナログ出力型)仕様書 — ESP32 + ESP-IDF 版

`dummy_intercom_spec.md` / rev. 0.11 / 2026-09-30

---

## 0. 改訂の経緯

| rev | プラットフォーム / 出力 | 変更理由 |
|---|---|---|
| 0.1 | Raspberry Pi Zero 2 W / アナログ | 初版 |
| 0.2 | オリジナル ESP32 / アナログ | Pi Zero 2 W 高騰(¥10,000 超) |
| 0.3 | Wemos ESP32 mini / アナログ | ボード確定 |
| 0.4 | Raspberry Pi Pico 2 W + BTstack / アナログ I2S DAC | ¥2,280 供給安定 |
| 0.5 | Raspberry Pi Pico 2 W + BTstack / USB マイク(UAC) | RP2350 ネイティブ USB 活用、USB マイク一本化 |
| **0.6** | **オリジナル ESP32(WROOM)+ ESP-IDF/Bluedroid / アナログ I2S DAC 出力** | **rev 0.4/0.5 は破棄。CYW43439(Pico W/2W)は SCO-over-HCI が動作しない(pico-sdk #1461、2026-09 も未修正、Infineon 対応待ち)ため、Pico 2 W では SRL-3 のインカム音声(SCO)を受信不可能。ESP-IDF の ESP32 は HFP-AG + SCO(CVSD, HCI データパス)が公式サポートされ、デコード済み PCM をコールバックで取得できる。USB マイク化は ESP32 に USB デバイス機能が無いため断念し、I2S DAC → 3.5mm アナログ出力で確定** |

### 不採用の選択肢

- Raspberry Pi Pico W / Pico 2 W — CYW43439 の SCO-over-HCI 不具合(#1461)で HFP 音声受信不可
- XIAO ESP32-C6 / C3 / S3、XIAO 全機種 — BLE 専用で HFP(Bluetooth Classic)不可
- USB マイク(UAC)化 — オリジナル ESP32 に USB デバイス機能なし(USB-UART シリアルのみ)
- Linux SBC + CSR8510 USB ドングル — SCO は確実だが、host + gadget の USB を別々に持つ SBC が必要で ¥1万超

---

## 1. 概要・目的

SENA SRL-3(および同等の Sena インカム)のユニバーサルインターコム音声を、
ESP32 が **HFP Audio Gateway(スマホ役)** として Bluetooth Classic 受信し、
**アナログ音声(3.5mm mono)として外部機器へ出力する**受信専用ブリッジを製作する。

出力先は用途に応じて差し替える:

- 録音機材(フィールドレコーダー等)のライン入力 ← 主用途
- Insta360 X4 の純正マイクアダプター(USB-C → 3.5mm TRS)経由でカメラの外部マイク入力

---

## 2. スコープ

### やること

- SRL-3 に対して **HFP Audio Gateway(AG＝スマホ役)** として接続
- インターコム音声(SCO / CVSD 8kHz)を **HCI データパス**で受信し PCM 化
- I2S0 → PCM5102A でアナログ化し 3.5mm 出力(LINE/MIC レベル切替)
- 単体アプライアンスとして自動起動・自動再接続(ボンディング保持)

### やらないこと

- SRL-3 へ音声を送る(上り方向は無音送出のみ)
- 電話発着信・A2DP 音楽受信・AVRCP
- USB マイク化、Wi-Fi 機能
- カメラへの Bluetooth マイク送信(将来拡張)

> rev 0.7: **WBS / mSBC(16kHz)は実装済み**(旧 Phase 2 項目)。AG は CVSD と mSBC の
> 両方を提示し、SRL-3 が mSBC を選べば 16kHz 経路に自動切替。`CONFIG_BT_HFP_WBS_ENABLE=n`
> で CVSD 固定に戻せる。あわせて 8k/16k→48k アップサンプリング + HPF + ノイズゲート
> (`main/audio_dsp.c`)を追加(旧 §13-1 / ISS-017)。

---

## 3. システム構成

```
┌──────────┐   Bluetooth Classic (BR/EDR)      ┌────────────────────────────────┐
│ SENA     │   HFP: RFCOMM(AT) + SCO(音声)      │  ダミーインカム機 (ESP32)        │
│ SRL-3    │◀────────────────────────────────▶│  ESP-IDF v5.4 / Bluedroid       │
│ (HFP-HF) │   相手=HF / 本機=AG                │  esp_hf_ag (HFP-AG)             │
└──────────┘                                   │  SCO CVSD → HCI data path       │
                                               │  incoming_cb: PCM S16 8k mono   │
                                               │        │                       │
                                               │  FreeRTOS ring buffer           │
                                               │        │                       │
                                               │  I2S0 master TX 8kHz/16bit      │
                                               └────────┬───────────────────────┘
                                                        │ I2S (3線)
                                               ┌────────┴───────────────────────┐
                                               │  PCM5102A I2S DAC              │
                                               └────────┬───────────────────────┘
                                                        │ アナログ (L=R, mono)
                                               ┌────────┴───────────────────────┐
                                               │ 出力段: L/R 加算 → mono         │
                                               │  半固定 VR(レベル)             │
                                               │  LINE / MIC パッド ジャンパ     │
                                               │  1:1 絶縁トランス(推奨)         │
                                               └────────┬───────────────────────┘
                                                        │ 3.5mm TRS (J1)
                       ┌────────────────────────────────┼────────────────────────────────┐
                       ▼                                                                 ▼
           フィールドレコーダー LINE IN                             Insta360 マイクアダプター → X4
```

---

## 4. ハードウェア仕様

### 4.1 リファレンス構成(確定)

| 区分 | 型番／仕様 | 備考 |
|---|---|---|
| MCU ボード | **Wemos/LOLIN ESP32 mini(D1 mini 形状、約 34 × 26 mm、ESP32-WROOM-32、micro-USB)**。互換名: "MH-ET Live MiniKit for ESP32" | **必須: BR/EDR(Classic)対応の オリジナル ESP32。S2/S3/C3/C6/H2、XIAO は不可**。電池充電回路なし → USB / モバイルバッテリー給電 |
| フラッシュ | 4MB 以上 | Bluedroid Classic + HFP は 4MB で収まる。PSRAM 不要(WROVER でも可) |
| オーディオ DAC | **PCM5102A I2S DAC モジュール** | フルスケール約 2.1 Vrms、内蔵デジタルフィルタ、外部 MCLK 不要(PLL) |
| 出力コネクタ J1 | 3.5mm TRS ステレオジャック(パネル) | tip/ring に同一 mono 信号 |
| レベル調整 | 半固定 VR 10kΩ + 抵抗パッド、LINE/MIC ジャンパ | §4.4 |
| 絶縁(推奨) | 1:1 オーディオトランス(600:600Ω) | グラウンドループ・ノイズ対策 |
| 電源 | 5V(USB / モバイルバッテリー) | §9 |
| 状態表示 | LED ×3(GPIO): PWR / LINK / SCO + 直列 1kΩ | 任意だが推奨 |
| ボタン | タクトスイッチ ×1(GPIO、長押しでボンディング消去) | 任意 |
| 筐体 | 小型 ABS ケース、3.5mm と USB を露出 | |

### 4.2 代替オーディオ出力

- **内蔵 8bit DAC(GPIO25/26)** + RC ローパス: 追加部品ゼロだが SNR 約 -48dB。緊急代替のみ
- **MAX98357A(I2S クラス D アンプ)**: スピーカー直接駆動用途のみ。ライン/マイク出力には PCM5102A

### 4.3 ピンアサイン(ESP32 → 周辺)

D1 mini 形状ボードで確実にヘッダに出ている GPIO(25/26/27/32/33/13/14)を使用。

| 信号 | ESP32 GPIO | 接続先 | 備考 |
|---|---|---|---|
| I2S BCLK | GPIO26 | PCM5102A BCK | |
| I2S LRCLK (WS) | GPIO25 | PCM5102A LRCK | |
| I2S DOUT | GPIO27 | PCM5102A DIN | |
| 3V3 / GND | 3V3 / GND | PCM5102A VIN / GND | 3.3V 入力版を選定(5V 版は VBUS/5V ピンから) |
| LED: PWR | GPIO32 | LED + 1kΩ → GND | 起動完了で点灯 |
| LED: LINK | GPIO33 | LED + 1kΩ → GND | ACL 接続状態 |
| LED: SCO | GPIO13 | LED + 1kΩ → GND | 音声リンク(受信中) |
| ボタン: BOND-CLR | GPIO14 | タクト → GND(内部プルアップ) | 3 秒長押しで NVS のボンディング消去→再起動 |

- ストラッピング(GPIO0/2/5/12/15)、フラッシュ用(GPIO6–11)、入力専用(GPIO34–39)は使用しない
- GPIO21/22 はボードによりヘッダ非露出のため I2S に使わない(27 を使用)
- PCM5102A の SCK は GND(内部 PLL)、FLT/DEMP/XSMT はモジュール既定でよい

### 4.4 出力段(J1)

```
DAC L ─[ 470Ω ]─┬─────────────┐
                │             │   (LINEジャンパ: 直結)
DAC R ─[ 470Ω ]─┘             ├──[C 1–10µF]──[ 半固定VR 10kΩ ]──┬── (絶縁トランス 1次)
                              │                                 │
        (MICジャンパ: 直列3.3kΩ + 対GND 33Ω ≒ -40dB)
                                                    絶縁トランス 2次 ─┬── J1 tip
                                                                     ├── J1 ring
                                                       GND ──────────┴── J1 sleeve(トランスで絶縁)
```

- **LINE モード**: 公称 0.3–1.0 Vrms、レコーダー LINE IN 向け
- **MIC モード**: 公称 3–30 mVrms、カメラ／マイクアダプターの MIC 入力向け(カメラ側ゲインで微調整)
- 直列 470Ω は L/R 加算 + 短絡保護、C は DC カット
- 絶縁トランスは J1 直前。電池運用かつノイズ問題が無ければ省略可

---

## 5. Bluetooth / プロトコル仕様

| 項目 | 値 |
|---|---|
| 無線 | Bluetooth Classic(BR/EDR)。BLE は無効化(`BTDM_CTRL_MODE_BR_EDR_ONLY`) |
| ホストスタック | Bluedroid(ESP-IDF 標準) |
| プロファイル | HFP、**本機ロール = Audio Gateway (AG)**、コンポーネント `esp_hf_ag` |
| 相手ロール | Hands-Free (HF) = SRL-3 |
| 制御 | RFCOMM 上 AT コマンド(SLC 確立) |
| 音声 | SCO(HCI 経由)。**CVSD(8kHz)** または **mSBC/WBS(16kHz)** / 16bit / mono。`CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y`、`CONFIG_BT_HFP_WBS_ENABLE=y`(CVSD 固定に戻すなら `=n`) |
| データ受信 | `esp_hf_ag_register_data_callback(recv, send)` の recv コールバックに デコード済み PCM(CVSD=8kHz / mSBC=16kHz、Bluedroid が mSBC ソフトデコード)が届く。バッファはコールバック内で消費/コピー(非ブロッキング) |
| コーデック交渉 | AG は CVSD + mSBC を提示 → SRL-3 が選択。`ESP_HF_AUDIO_STATE_EVT` の `CONNECTED` / `CONNECTED_MSBC` で判別し、`audio_i2s_set_input_rate(8000/16000)` で音声経路のサンプルレートを切替 |
| デバイスクラス(CoD) | service=Telephony\|Audio + major=Phone + minor=Smartphone = **`0x60020C`**(`esp_bt_cod_t` のフィールドで設定。実機で SLC 不成立なら `0x20040C` へ) |
| デバイス名 | `DummyIntercom` |
| 役割(CR-011) | `BT_ROLE`(`app_config.h`)でビルド時に選択、1 起動 1 役。`BT_ROLE_AG`=従来(SRL-3 電話ペアリング、SRL-3=HF/本機=AG)、`BT_ROLE_HF`=SRL-3 ユニバーサルインターコム用(SRL-3=AG/本機=HFP-HF、`bt_hf_client.c`)。**現在のツリーは実機検証のため `BT_ROLE_HF`**(検証後に既定へ戻す/決定する) |
| CoD | AG 役: Phone/Smartphone/Telephony\|Audio(`BT_COD_PROFILE=1` で AV Hands-free)。HF 役: AV/Hands-free(minor 0x02)、service Audio\|Rendering(実効 ≈ 0x240408) |
| ペアリング | **レガシーペアリング(固定 PIN `0000`)、SSP 無効**(CR-010、技術検証用・後日セキュリティ強化予定)。`app_config.h` の `BT_LEGACY_PAIRING 0` で SSP Just Works に戻せる |
| ボンディング保存 | Bluedroid が NVS に自動保存 + ピア BDA を明示的に NVS 保存(再接続用)。対象 SRL-3 1 台 |

### 5.1 sdkconfig 主要設定

```
CONFIG_BT_ENABLED=y
CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_A2DP_ENABLE=n
CONFIG_BT_HFP_ENABLE=y
CONFIG_BT_HFP_AG_ENABLE=y
# --- Bluedroid: HFP-AG over HCI, CVSD 固定 ---
CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y
CONFIG_BT_HFP_WBS_ENABLE=y   # rev 0.7: mSBC 16kHz 対応(=n で CVSD 固定に戻る)
CONFIG_BT_ACL_CONNECTIONS=2
# --- コントローラ: SCO を HCI 経路へ / 同期接続 1 本 ---
CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y
CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1
# --- パーティション(カスタム / 4MB flash。factory 0x1E0000)---
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
```
`partitions.csv`: `nvs 0x9000/0x6000`, `phy_init 0xf000/0x1000`, `factory 0x10000/0x1E0000`
(app image 約 720KB、factory に 63% 空き)

- ベース: `esp-idf/examples/bluetooth/bluedroid/classic_bt/hfp_ag`
  (特に `sdkconfig.ci.vohci` が Voice-over-HCI のリファレンス構成)
- **HCI データパスは 2 か所必須**: コントローラ側 `BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI` と
  Bluedroid 側 `BT_HFP_AUDIO_DATA_PATH_HCI` の両方(ISS-008 対策の中核)
- `CONFIG_BT_SSP_ENABLED` は ESP-IDF v5.4 に無い。SSP は実行時に `esp_bluedroid_config_t.ssp_en`(`app_main.c`)で切替える(CR-010 で `ssp_en=false`)。
  `BT_LEGACY_PAIRING 0` のときのみ、IO capability を `esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof)` で
  `ESP_BT_IO_CAP_NONE` に設定(`ssp_en=false` ではこの API は呼ばない)
- 例の既定は **PCM データパス**。`gpio_pcm_config.c` は PCM 経路用のため本実装 `main/` には持ち込まない

### 5.2 SLC 確立時の AG のふるまい

Bluedroid の HFP-AG が AT ハンドシェイクを処理する。アプリは各クエリイベントで
「圏内・良好」を固定で返す:

| イベント | 返す内容 |
|---|---|
| `ESP_HF_CIND_RESPONSE_EVT` | service=1, call=0, callsetup=0, signal=5, roam=0, battchg=5 |
| `ESP_HF_COPS_RESPONSE_EVT` | 任意事業者名(例 `"DummyNet"`) |
| `ESP_HF_CLCC_RESPONSE_EVT` | 通話中偽装時のみ 1 件(dir=1 outgoing, status=0 active)、それ以外 0 件 |
| `ESP_HF_CNUM_RESPONSE_EVT` | 空 or ダミー番号 |
| `ESP_HF_UNAT_RESPONSE_EVT` | 未知 AT はログ出力し `OK` |

`service=0` / `signal=0` は SRL-3 に SLC 破棄されうるため必須。

### 5.3 SCO 確立ロジック(受信専用の要)

1. SRL-3 側から音声リンク要求(`ESP_HF_AUDIO_STATE_EVT` connecting→connected)を受理。
   (`AUDIO_STATE_EVT` param は v5.4 では `remote_addr` / `state` / `sync_conn_handle` のみ。
   `preferred_frame_size` は存在しない — 送信は send コールバックの `sz` を厳守)
2. SRL-3 が発呼系 AT(`AT+BLDN`=`ESP_HF_DIAL_EVT` / `ATD` / `AT+CKPD`)を送る場合は
   通話中を偽装:callsetup=2 → callsetup=0,call=1 を CIEV 通知 →
   `esp_hf_ag_audio_connect(bda)`。
3. **堅牢化**: SLC 確立後 3 秒間隔で音声リンクが無ければ AG から
   `esp_hf_ag_audio_connect()` を試行し常時 SCO を維持(受信専用のため常時 SCO で問題なし)。
4. 上り SCO(本機 → SRL-3)には無音 PCM(ゼロ)を送出。send コールバックでゼロ埋め、
   必要に応じ `esp_hf_ag_outgoing_data_ready()` を周期呼び出し。
5. ACL 切断(`ESP_HF_CONNECTION_STATE_EVT` = DISCONNECTED)時は再接続タイマ(5 秒周期)で
   NVS のピア BDA へ `esp_hf_ag_slc_connect()` を試行。

### 5.4 プロトコル挙動の観測

- 受信した全 AT コマンド(`ESP_HF_*_EVT` と UNAT)をシリアルログにダンプ
- recv コールバック毎の PCM バイト数と RMS レベル(発話有無の判定)
- `ESP_HF_BCS_RESPONSE_EVT` / `btm_sco_connected` / `AUDIO_STATE_EVT` でコーデックと SCO 状態
- 補助: 任意の Linux PC + USB BT ドングル(CSR8510 系)+ `btmon` で HCI レベル確認(開発時のみ)

---

## 6. オーディオ仕様

| 項目 | 値 |
|---|---|
| 受信フォーマット | CVSD: 8kHz / 16bit / mono、または mSBC/WBS: 16kHz / 16bit / mono(Bluedroid がデコード済み PCM をコールバック) |
| I2S 出力 | I2S0 master TX、**48000 Hz 固定**、16bit、ステレオフレーム(L=R に複製) |
| 内部処理(`main/audio_dsp.c`、I2S TX タスク上) | ① 120Hz 1 次ハイパス(DC/ゴロ/ハム裾カット、`AUDIO_HPF_CUTOFF_HZ 0` で無効) → ② ノイズゲート(下方エキスパンダ、`AUDIO_GATE_*`。**既定は無効 `AUDIO_GATE_ENABLE 0`**、必要なら 1) → ③ ポリフェーズ FIR で 48kHz へアップサンプル(CVSD ×6 / mSBC ×3。Kaiser 窓 sinc 32 taps/phase、CVSD 帯域 3.4kHz まで平坦 −0.5dB 以内・イメージ 70dB 以上減衰)。FIR 係数は `tools/gen_fir.py` で再生成 |
| ジッタバッファ | 40–100 ms 可変(初期 60 ms @8kHz)。ドロップアウトと遅延のトレードオフで調整 |
| 出力チャネル | mono(J1 tip/ring 同相) |
| 出力レベル | LINE: 0.3–1.0 Vrms / MIC: 3–30 mVrms(パッド切替) |
| 出力インピーダンス | < 300Ω / 想定負荷 ≧ 10kΩ(LINE)、1–5kΩ(MIC) |
| 帯域 | CVSD: 約 300–3400 Hz / mSBC: 約 120–7000 Hz(下限は HPF、上限は mSBC) |
| エンドツーエンド遅延 | 目標 < 100 ms、可能なら < 60 ms(SRL-3 発話 → J1) |
| 遅延安定性 | 一定であること(録音同期は固定オフセット補正前提) |
| 無音時ノイズ | 可聴レベル以下(絶縁トランス使用時 -60 dBFS 相当以下) |

### 6.1 信号経路(ファーム内)

```
SCO(HCI) → esp_hf_ag recv コールバック (PCM S16 mono 8k)
         → FreeRTOS ring buffer (ジッタバッファ)
         → I2S TX タスク (DMA)
         → I2S0 → PCM5102A → 出力段 → J1
```

- recv コールバックは軽量処理のみ(memcpy + ringbuffer 書き込み)。DMA バッファは多め
- SCO 未確立時の I2S 出力は無音(ゼロ)

---

## 7. ソフトウェア仕様

### 7.1 ベース

| 項目 | 値 |
|---|---|
| SDK | ESP-IDF v5.4(v5.3 以降。API は使用バージョンの `esp_hf_ag_api.h` に合わせる) |
| ホスト | Bluedroid |
| ベース例 | `examples/bluetooth/bluedroid/classic_bt/hfp_ag` |
| ビルド | `idf.py set-target esp32 && idf.py build flash monitor` |
| ログ | UART0 / USB シリアル(`idf.py monitor`) |

### 7.2 ファーム構成(`main/`)

| ファイル | 役割 |
|---|---|
| `app_main.c` | NVS 初期化、BT コントローラ(BR/EDR only)+ Bluedroid 起動、GAP(名前/CoD/固定 PIN 0000、`ssp_en=false`)、`BT_ROLE` に応じ `esp_hf_ag_init`(AG)または `esp_hf_client_init`(HF)+ コールバック登録、I2S タスク起動 |
| `bt_hf_ag.c` | HF イベント処理: SLC、CIND/COPS/CLCC/CNUM 応答、DIAL/CKPD → 通話偽装 + `audio_connect`、AUDIO 状態、再接続タイマ、UNAT ログ |
| `bt_data_path.c` | recv コールバック → ringbuffer / send コールバック → ゼロ埋め、診断用 RMS ログ、`preferred_frame_size` 対応 |
| `audio_i2s.c` | I2S0 master TX @8kHz/16bit、ringbuffer → DMA、ジッタバッファ、L=R 複製、SCO 断時ゼロ出力 |
| `storage.c` | NVS にピア BDA 保存/読出、BOND-CLR 処理(消去 → `esp_restart`) |
| `leds.c` | PWR/LINK/SCO GPIO 駆動、BOND-CLR ボタン監視 |
| `app_config.h` | GPIO 定義、バッファサイズ、タイマ周期、デバイス名等の定数 |

### 7.3 LED 状態

| LED | 状態 | 意味 |
|---|---|---|
| PWR | 点灯 | 起動完了 |
| LINK | 消灯 / 点滅(1Hz) / 点灯 | 未接続 / ページング・再接続試行中 / ACL 接続 |
| SCO | 消灯 / 点灯 / 速い点滅 | 音声リンク無し / SCO 確立(受信中)/ バッファ異常 |

---

## 8. 動作シーケンス

### 8.1 初回セットアップ

1. ESP-IDF でビルド・書き込み(`idf.py flash`)
2. 起動後、`DummyIntercom` が discoverable(SLC 未接続の間は常時。ボンド済みでも SRL3 等の新規機器から検出可能)
3. SRL-3 を取扱説明書に従い **「電話(Phone)ペアリング」モード**にする
4. SRL-3 から `DummyIntercom` を選択(PIN `0000`。ペア番号の確認なし。CR-010)
5. シリアルログで SLC 確立(`ESP_HF_CONNECTION_STATE_EVT: SLC_CONNECTED`)を確認、
   ピア BDA が NVS 保存される
6. SLC 接続中は discoverable を停止、切断後に再び discoverable(CR-010)。自動再接続は従来どおり

### 8.2 通常運用

1. 電源投入 → 数秒で BT 起動、NVS のピアへ自動接続(LINK 点灯)
2. ライダーが SRL-3 でユニバーサルインターコム(またはインカム音声)を開始
3. SCO 確立(SCO LED 点灯)、J1 に音声出力
4. J1 → レコーダー LINE IN、または J1 → Insta360 マイクアダプター → X4
5. 電波断・電源断からの復帰で自動再接続

### 8.3 ボンディング消去

BOND-CLR ボタン 3 秒長押し → BtAppT へ dispatch → Bluedroid ボンド(`esp_bt_gap_remove_bond_device`, 非同期)+ 自前 NVS ピア BDA を消去 → 非同期処理完了を待つため 500ms 待機 → `esp_restart` → 初回ペアリング可能

---

## 9. 電源

| 項目 | 値 |
|---|---|
| 入力 | 5V(USB micro-B / モバイルバッテリー)。電池直結時は 3.3V を安定供給できるレギュレータ |
| 消費電流(目安) | BT 接続 + SCO 受信時 平均 約 100–160 mA @5V(約 0.5–0.8W) |
| 連続運用(10,000mAh モバイルバッテリー) | 目安 30–40 時間 |
| 省電力 | BT modem-sleep 自動。deep sleep 不可(リンク維持) |
| モバイルバッテリー | 低負荷オートカットオフする個体に注意。「低電流モード」対応品を推奨 |
| 電源断 | そのまま遮断可(NVS はトランザクション保護) |
| ボードの注意 | D1 mini 形状の AMS1117 系 LDO は効率が低い。電池運用では発熱・ロスに留意 |

---

## 10. 物理・環境

- 動作温度: 0–45 ℃。直射日光下の車内放置は避ける
- 3.5mm は L 字プラグの逃げを確保
- 2.4GHz 干渉低減のため、カメラ本体・スマホから 10cm 以上離す。金属ケースなら外部アンテナ版(WROOM-32U)を検討

---

## 11. テスト計画・受け入れ基準

| ID | 項目 | 手順 | 合格基準 |
|---|---|---|---|
| T1 | 起動 | 電源投入 | 10 秒以内に discoverable / 既ボンド時は自動接続開始 |
| T2b | HF 役ペアリング(CR-011、`BT_ROLE_HF`) | SRL-3 ユニバーサルインターコムペアリング | ACL が約 0.4 秒で切れない、PIN 0000 で `paired:`、`connection state: SLC_CONNECTED`、通話時 `audio state: connected*`(実機未検証) |
| T2 | ペアリング | SRL-3 電話ペアリング/ユニバーサルインターコムペアリングから接続 | 固定 PIN 0000(ペア番号確認なし)で成功、ログに SLC_CONNECTED、NVS 保存 |
| T3 | SCO 確立 | SRL-3 でユニバーサルインターコム開始 | 3 秒以内に SCO、SCO LED 点灯、J1 に音声。CVSD で確立 |
| T4 | 音声内容 | 2 台目インカムから通話、装着者も発話 | 音声識別可能。**グループ音声が HFP に乗るか RMS ログで記録** |
| T5 | 遅延 | SRL-3 マイクに拍手、SRL-3 側音声と J1 をオシロ比較 | < 100 ms、変動なし |
| T6 | 連続性 | 30 分連続受信 | 3 秒以上の無音・断が発生しない |
| T7 | レベル | J1 出力を測定 | LINE: 0.3–1.0 Vrms / MIC: 3–30 mVrms |
| T8 | 再接続 | SRL-3 電源断→復帰、距離を離す→戻す | 手動操作なしで再接続、LINK/SCO 復帰 |
| T9 | ノイズ | 無音時に J1 をレコーダー録音 | ハム・ノイズが -60 dBFS 相当以下(絶縁トランス使用時) |
| T10 | 電源断耐性 | 動作中に電源遮断 ×10 | 毎回正常起動、NVS 破損なし、再ペアリング不要 |
| T11 | カメラ連携 | J1(MIC)→ マイクアダプター → X4 録画 | X4 録画に音声記録、レベル適正 |
| T12 | ボンディング消去 | BOND-CLR 3 秒長押し | 消去 → 再起動 → 初回ペアリング可能 |
| T13 | コーデック互換 | ログ / btmon で BRSF / `AT+BAC` / BCS / SCO パラメータ確認 | SRL-3 が CVSD または mSBC を確立。`codec negotiation result` ログと `SCO PCM … Hz` ログが一致 |

---

## 12. 既知の制約・リスク

| # | 内容 | 対策 / 方針 |
|---|---|---|
| R1 | SRL-3 がユニバーサルインターコム音声を HFP に流すか未確認(電話通話経路のみの可能性) | §5.4 の RMS ログ + 任意 PC/btmon で確認、結果を本書に追記。最悪でも装着者マイク音声は取得可 |
| R2 | CVSD 確立時は音質がテレフォニー帯域(300–3400Hz)。mSBC 確立時は ~7kHz | rev 0.7 で WBS 有効化。SRL-3 が mSBC を選べば広帯域化。CVSD のままなら用途をナレーション/交信記録に限定 |
| R3 | ~~SRL-3 が CVSD を拒否し WBS 必須の可能性~~ → **対応済み(rev 0.7)** | `CONFIG_BT_HFP_WBS_ENABLE=y`。AG が CVSD/mSBC 両提示、16kHz 経路自動切替。mSBC 実機動作は T13 で確認 |
| R4 | **ESP-IDF HFP-AG + HCI データパスの歴史的不具合**(codec ネゴでハング: BT-776 / espressif/esp-idf#4967、`BTA_AG_SCO_CODEC_ST: Ignoring event` 等) | 最新の安定 IDF(v5.4 系)を使用。T3 を最優先で実機検証。詰まったら IDF 側 issue と例のパッチを確認。btmon で切り分け |
| R5 | Bluedroid HFP-AG が SRL-3 の想定外 AT で詰まる恐れ | UNAT を全ログ化。`esp_hf_ag_unknown_at_send` で個別対応 |
| R6 | グラウンドループによるハム(USB 電源と録音機材/カメラの電位差) | 1:1 絶縁トランスを標準実装。電源はバッテリー推奨 |
| R7 | Insta360 マイクアダプターの入力レベル/インピーダンス仕様が非公開 | MIC パッド + 半固定 VR で広く調整可能に |
| R8 | SRL-3 のユニバーサルインターコム非公式利用。Sena ファーム更新で挙動変化の可能性 | ファーム版数を記録。更新前に動作確認 |
| R9 | 2.4GHz 混雑(カメラ Wi-Fi、Sena メッシュ、スマホ) | アンテナ離隔、必要なら外部アンテナ版モジュール |
| R10 | D1 mini ボードの LDO 効率が低く電池運用で発熱・ロス | 高効率レギュレータ搭載ボードか外部 DC/DC |
| R11 | 実機(ESP32 / SRL-3)が開発環境に無く HW 実測不可 | コンパイル/静的検証まで実施。HW テスト(T1〜T13)は手順書化して未実施明示。ユーザー実機で実施 |

---

## 13. 将来拡張(本仕様外)

1. ~~WBS(mSBC 16kHz)対応~~ — **rev 0.7 で実装済み**
2. **カメラへ Bluetooth マイク送信する版** — 2 台目 MCU に HFP-HF、X4 へ接続
3. バッテリー内蔵・専用筐体・車体/ヘルメットマウント
4. Wi-Fi(SoftAP)経由の設定 UI(ゲイン、ジッタバッファ長、ピア管理、ログ取得)

---

## 14. 付録

### 14.1 概算 BOM(日本, 目安)

| 品目 | 単価 |
|---|---|
| Wemos/LOLIN ESP32 mini(ESP32-WROOM-32) | 600–1,200円 |
| PCM5102A DAC モジュール | 400–900円 |
| 3.5mm ジャック・抵抗・半固定 VR・コンデンサ・ジャンパ・基板 | 400–600円 |
| 1:1 絶縁トランス(推奨・任意) | 500–1,500円 |
| LED ×3・タクトスイッチ | 100円 |
| 筐体 | 300–800円 |
| **合計** | **約 2,300–5,100円** |

### 14.2 開発環境

- ESP-IDF v5.4、`idf.py`、ローカル導入(no-sudo): `~/esp/esp-idf` + `~/.espressif`
- デバッグ: `idf.py monitor`(シリアルログ)
- 補助: 任意の Linux PC + USB BT ドングル(CSR8510 系)+ `btmon`(HCI トレース、開発時のみ)

### 14.3 参考

- ESP-IDF: `examples/bluetooth/bluedroid/classic_bt/hfp_ag`、
  `components/bt/host/bluedroid/api/include/api/esp_hf_ag_api.h`
- ESP-IDF HFP AG API ドキュメント(データパスとコーデックの関係、recv/send コールバック)
- espressif/esp-idf#4967(HFP-AG HCI データパスの codec ネゴ問題、履歴)
- HFP 1.7 仕様(SLC 手順、CIND、BAC/BCS/BCC、SCO パラメータ)

---

## 15. 変更履歴

| rev | 日付 | 内容 |
|---|---|---|
| 0.1 | 2026-09-02 | 初版。Pi Zero 2 W + bluez-alsa |
| 0.2 | 2026-09-02 | オリジナル ESP32 + ESP-IDF/Bluedroid へ(Pi Zero 2 W 高騰) |
| 0.3 | 2026-09-02 | Wemos ESP32 mini 確定、アナログ出力 |
| 0.4 | 2026-09-02 | Raspberry Pi Pico 2 W + BTstack、アナログ I2S DAC |
| 0.5 | 2026-09-02 | Pico 2 W で USB マイク(UAC)一本化 |
| 0.6 | 2026-09-02 | **Pico 2 W 破棄(CYW43439 の SCO-over-HCI 不具合 #1461)。オリジナル ESP32(WROOM)+ ESP-IDF/Bluedroid に確定。** HFP-AG + SCO(CVSD, HCI データパス、`esp_hf_ag_register_data_callback`)、I2S DAC(PCM5102A)→ 出力段 → 3.5mm アナログ。USB マイク化は ESP32 に USB デバイス機能が無いため断念。R4(ESP-IDF HFP-AG HCI データパスの codec ネゴ不具合履歴)を最重要検証項目とする |
| **0.7** | **2026-09-07** | **音質改善(旧 Phase 2)**。(A) I2S 出力を 48kHz 固定化し、`main/audio_dsp.c` に 120Hz ハイパス + 下方エキスパンダ型ノイズゲート(既定 OFF)+ ポリフェーズ FIR アップサンプラ(CVSD 8k×6 / mSBC 16k×3、Kaiser 32tap)を追加(§6 / ISS-017 / ISS-015)。(B) `CONFIG_BT_HFP_WBS_ENABLE=y` に変更し mSBC/WBS 16kHz を有効化。`ESP_HF_AUDIO_STATE_CONNECTED_MSBC` で音声経路を 16kHz へ自動切替(R3 解決 / §13-1)。 |
| 0.7a | 2026-09-07 | 実機で「こもった音」報告 → 録音+ログ解析。**原因 = CVSD 固定**(DSP は正常)。SRL-3 が `AT+BAC` に LC3-SWB(codec 3)を含み、ESP-IDF v5.4 Bluedroid `bta_ag_parse_bac()` がリスト全体を破棄して CVSD へフォールバック。IDF に 1 行パッチ(`patches/esp-idf-v5.4-hfp-bac-lc3.patch`、`git apply` で適用・IDF 更新で消える)。**実機確認済み: mSBC 16kHz 確立、帯域 3.4k→6.5kHz(T13 合格)**。録音側にクリップ・100/200Hz グラウンドループハムも検出(本体外、要 §R6 絶縁トランス・レベル調整) |
| 0.10 | 2026-09-30 | **CR-010 SENA SRL3 ペアリング対応(技術検証)**。SSP 無効化(`ssp_en=false`)+固定 PIN 0000 のレガシーペアリング、SLC 未接続中は常時 discoverable、`BT_COD_PROFILE` 追加(既定 Phone)。`BT_LEGACY_PAIRING 0` で SSP に戻せる。後日セキュリティ強化予定。実機(SRL3)未検証 |
| 0.11 | 2026-09-30 | **CR-011 HF 役追加(SRL3 ユニバーサルインターコム対応)**。実機で BT_COD_PROFILE=1 にすると SRL3 が ACL 接続するが約 0.4 秒後に自発切断(0x13)、認証前 → SRL3 は AG 役で相手に HFP-HF を期待する仮説(未検証)。`app_config.h` の `BT_ROLE`(`BT_ROLE_AG` 既定 / `BT_ROLE_HF`)で 1 起動 1 役を選択(AG と HF は IDF 上同時動作不可)。HF 役は `bt_hf_client.c`(CoD = AV/Hands-free, service Audio\|Rendering、レガシー PIN 0000、SLC 未接続中は常時 discoverable)、SCO 受信は AG 役と共通の I2S 経路。`BT_ROLE_HF` ビルドで SRL3 ユニバーサルインターコムを試験。**2026-09-30 実機確認: ペアリング(PIN 0000)、SLC、CVSD 8kHz SCO 受信(通話音声が rms に反映)まで成功。** HF 役は SCO を受動的に待つ(`connect_audio` と保存ピアへの能動再接続は行わない)、送信無音タイマは HF 役では起動しない(起動するとコントローラが panic)。IDF パッチ `patches/esp-idf-v5.4-hfp-client-no-post-slc-at.patch` が必要(SLC 後の AT が未応答で 30 秒後に切断されるため)。出力端子での可聴確認・mSBC 未確認、電話ペアリング(録音不可)は `BT_ROLE_AG`。`hf_unit/`(2 台目 ESP32 ファーム)をリポジトリに取り込み。実機(SRL3)未検証 |
