# テスト成績書 — ダミーインカム(受信専用 / アナログ出力型)ファームウェア

| 項目 | 内容 |
|---|---|
| 文書種別 | テスト成績書(チームプロンプト DEBUGGER 成果物) |
| 版 | 0.5(最終版 / ISS-019・ISS-022・ISS-024 修正確認 / 最終判定確定) |
| 作成 | DEBUGGER |
| 最終更新 | 2026-09-02 |
| 管理 | BOSS |
| 旧版メモ | v0.1 は Pico 2 W(rev 0.5)前提。rev 0.6 で Pico 破棄に伴い全面改訂。旧資産は `docs/agent_team/_archive_pico/`。v0.2 は準備フェーズ(実装前) |

---

## 1. テスト対象

- 対象ソフト: ダミーインカム(受信専用 / アナログ 3.5mm 出力型)ファームウェア(新規開発)
- 対象ハード: **オリジナル ESP32(ESP32-WROOM-32)** + PCM5102A I2S DAC + 出力段 → 3.5mm TRS
- 仕様書: `dummy_intercom_spec.md` **rev 0.6** / 設計書 `software_design.md` rev.4
- 対象 revision / commit: **未 commit**(`git add` 済・作業ツリー。`main/` 全 14 ファイル + `CMakeLists.txt` / `sdkconfig.defaults` / `partitions.csv`)
- ソースツリー: `main/`(`app_main.c` / `bt_hf_ag.c` / `bt_data_path.c` / `audio_i2s.c` / `bt_app_core.c` / `storage.c` / `leds.c` + ヘッダ + `app_config.h`)。※ 空の `src/` は未使用
- ロール: HFP Audio Gateway(AG)。相手 = SENA SRL-3(HFP-HF)

## 2. テスト環境

### 2.1 ビルド環境(本環境)

| 区分 | 内容 | 確認結果 |
|---|---|---|
| ホスト OS | Linux 6.12(Debian 13 系)x86_64 | OK |
| SDK | **ESP-IDF v5.4**(`~/esp/esp-idf`) | OK(`idf.py --version` = ESP-IDF v5.4) |
| toolchain | xtensa-esp-elf GCC(ESP-IDF v5.4 同梱)、esptool 4.12.0 | OK |
| ビルドツール | CMake 3.30.2 + Ninja 1.12.1(`idf_tools.py install cmake ninja` で追加導入) | OK |
| IDF ツールパス | `IDF_TOOLS_PATH=/home/subcon/.espressif` | OK |
| 環境セットアップ | `export IDF_TOOLS_PATH=/home/subcon/.espressif && . /home/subcon/esp/esp-idf/export.sh` | OK(`~/esp/.setup_complete` 出力済み) |
| ターゲット | `esp32`(Xtensa LX6 dual core) | OK |
| Bluetooth ホスト | Bluedroid(BR/EDR only) | OK |
| ビルド検証プロジェクト | `build_probe_espidf/`(`src/` 非依存。IDF 例のコピー) | OK |
| 検証スクリプト | `tools/build_probe_espidf.sh` | OK |
| 旧 Pico toolchain | `/home/subcon/opt`(pico-sdk 2.1.1)は残置。本 rev では未使用 | — |

### 2.2 実機テスト環境(本環境に無し)

| 機材 | 状態 | 影響 |
|---|---|---|
| ESP32(WROOM)実機 / Wemos ESP32 mini | **無し** | フラッシュ書込・起動・SCO 実測不可(R11) |
| PCM5102A DAC + 出力段 + 3.5mm ジャック | **無し** | I2S 出力・アナログレベル・ノイズ実測不可 |
| SENA SRL-3 実機(HFP-HF) | **無し** | ペアリング・SLC・SCO・音声内容の実測不可(R11) |
| フィールドレコーダー / Insta360 X4 + マイクアダプター | **無し** | T7 / T11 実測不可 |
| Linux PC + USB BT ドングル(CSR8510)+ btmon | **無し** | HCI レベルのプロトコル観測不可(補助手段) |
| オシロスコープ | **無し** | T5(遅延)実測不可 |

---

## 3. ビルド環境健全性検証結果(T04-B / 準備フェーズ)

`build_probe_espidf/`(IDF 例 `examples/bluetooth/bluedroid/classic_bt/hfp_ag` をそのままコピー、
spec §5.1 の sdkconfig を `sdkconfig.defaults` に反映)でビルド検証を実施。
再現: `./tools/build_probe_espidf.sh`

### 3.1 検証項目と結果

| # | 検証内容 | 結果 | 備考 |
|---|---|---|---|
| B1 | `idf.py --version` が v5.4 系か | **OK** | ESP-IDF v5.4 |
| B2 | esp32 ターゲットで `idf.py set-target esp32` が通るか | **OK** | 新規 sdkconfig 生成成功 |
| B3 | HFP-AG 例が esp32 でビルドできるか | **OK** | `idf.py build` 成功。`hfp_ag.bin` 生成 |
| B4 | `CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y` を sdkconfig で設定しリビルドできるか | **OK** | sdkconfig に反映確認、ビルド成功 |
| B5 | `CONFIG_BT_HFP_WBS_ENABLE=n`(CVSD 固定)でビルドできるか | **OK** | sdkconfig `# CONFIG_BT_HFP_WBS_ENABLE is not set`、ビルド成功 |
| B6 | `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y`(BLE 無効)でビルドできるか | **OK** | `CONFIG_BTDM_CONTROLLER_MODE_BR_EDR_ONLY=y` 確認 |
| B7 | `CONFIG_BT_A2DP_ENABLE=n` でビルドできるか | **OK** | sdkconfig に反映 |
| B8 | WBS 有効(Phase 2 / R3)でもビルドできるか | **OK** | `CONFIG_BT_HFP_WBS_ENABLE=y` に切替え → mSBC ソフトデコード込みでビルド成功(将来対応の実現性を確認) |
| B8b | IDF 提供の Voice-over-HCI CI 構成 `sdkconfig.ci.vohci` そのままでビルドできるか | **OK** | `idf.py -DSDKCONFIG_DEFAULTS=sdkconfig.ci.vohci build` 成功、警告 0。結果 config は HCI データパス有効 / BLE 無効 / BR_EDR only。※このファイル単体では WBS は既定の有効のまま・flash 2MB。本プロジェクトは `sdkconfig.defaults`(WBS=n / 4MB)を使用 |
| B9 | 新 I2S ドライバ `driver/i2s_std.h` がこの IDF に存在するか | **OK** | `components/esp_driver_i2s/include/driver/i2s_std.h` 存在。`i2s_new_channel` / `i2s_channel_init_std_mode` / `i2s_channel_enable` / `i2s_channel_write`、`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG` / `I2S_STD_CLK_DEFAULT_CONFIG` を確認 |
| B10 | フラッシュ 4MB / RAM に収まるか | **OK** | §3.2 参照 |
| B11 | ビルド警告 | **OK** | IDF 例コード由来の警告 0(本実装ソースは未着手) |
| B12 | バイナリ生成(bootloader + partition + app) | **OK** | `bootloader.bin` / `partition-table.bin` / `hfp_ag.bin` 生成 |

### 3.2 バイナリサイズ

| 項目 | 値 | 判定 |
|---|---|---|
| app バイナリ `hfp_ag.bin` | 0xB99F0 = 760,304 B(image 約 760 KB) | — |
| app パーティション(`single_app_large` / 4MB flash) | 0x177000 = 1,536 KB。**空き 51%(0xBD610)** | 余裕あり |
| Flash Code | 519,514 B | — |
| Flash Data | 124,512 B | — |
| bootloader | 0x6560 = 25,952 B(空き 9%) | 例の既定。実装で肥大時は要注意 |
| フラッシュ設定 | `CONFIG_ESPTOOLPY_FLASHSIZE="4MB"`(spec §4.1)。例の既定 2MB から拡張 | OK |

※ ESP32 内蔵 SRAM(約 520KB、うち BT/ヒープで多くを消費)への静的 DRAM/IRAM 配置は
本実装(I2S DMA バッファ・リングバッファ・タスクスタック追加)後に `idf.py size` で再確認が必要(S5)。

### 3.3 検証中に判明した事項(CODER / DESIGNER 向け申し送り)

| # | 事項 | 内容 / 対処 |
|---|---|---|
| N1 | Ninja / CMake が IDF ツールに未同梱だった | 初期セットアップ後 `python3 $IDF_PATH/tools/idf_tools.py install cmake ninja` を実行して追加導入済み(CMake 3.30.2 / Ninja 1.12.1)。以後 `export.sh` が PATH に載せる。**pico-env.sh の cmake(3.31.6)とは混在させない**(`build_probe_espidf.sh` は IDF 環境のみ source)。 |
| N2 | IDF 例のデフォルトは PCM データパス | `examples/.../hfp_ag` の `sdkconfig.defaults` は HCI データパス未指定(コントローラ既定=PCM)。本プロジェクトは **`CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y` を明示必須**(spec §5.1)。probe では `sdkconfig.defaults` に追記済み。 |
| N3 | フラッシュ/パーティション | 例の既定は 2MB / `single_app`。spec §4.1 は 4MB 以上。probe では `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` + `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` を設定。CODER は本実装でも同様に。 |
| N4 | 例の `gpio_pcm_config.c` は PCM データパス用 | HCI データパスでは不要。本実装 `main/` では PCM 経路のコードを持ち込まず、recv/send コールバック + I2S(`i2s_std`)経路にする(spec §7.2 `bt_data_path.c` / `audio_i2s.c`)。 |
| N5 | `esp_hf_ag_api.h` の API 名はバージョン依存 | spec §5.3 の `esp_hf_ag_audio_connect` / `esp_hf_ag_slc_connect` / `esp_hf_ag_register_data_callback` / `esp_hf_ag_outgoing_data_ready` 等は v5.4 の `components/bt/host/bluedroid/api/include/api/esp_hf_ag_api.h` に合わせて確認すること(DESIGNER)。 |
| N6 | R4(codec ネゴのハング履歴) | ビルドは通るが、これは実機依存の動作不具合。T3 を実機最優先で検証。btmon(補助)での切り分け手順を §4.2 に記載。 |
| N7 | spec §5.1 `CONFIG_BT_SSP_ENABLED` は廃止 | ESP-IDF v5.4 では該当 Kconfig シンボルが存在せず(Bluedroid Classic は SSP 常時有効)、指定すると `unknown kconfig symbol` 警告。**DESIGNER は spec §5.1 からこの行を削除**すること。IO capability(`ESP_BT_IO_CAP_NONE`)はコード側 `esp_bt_gap_set_security_param` で設定。 |

---

## 4. テスト項目一覧(テスト計画)

凡例 — 実機要否: **要**=ESP32 / SRL-3 / DAC / 出力機材が必要 / **不要**=静的・ビルドのみ。
本環境実施可否: **可**=本環境で実施 / **不可**=実機なしのため未実施(R11)。

### 4.1 ビルド・静的検証(本環境で実施可能)

| ID | 目的 | 前提 | 手順 | 期待結果 | 実機要否 | 本環境実施可否 |
|---|---|---|---|---|---|---|
| S1 | `src/` 本実装の esp32 ビルド | CODER 実装完了 | `idf.py set-target esp32 && idf.py build` | エラー 0 で `*.bin` 生成 | 不要 | 可(実装後) |
| S2 | コンパイラ警告確認 | S1 | ビルドログの warning を精査 | 本実装ソース由来の警告 0 | 不要 | 可(実装後) |
| S3 | sdkconfig 整合 | S1 | spec §5.1 の全項目と `sdkconfig` を突合(HCI データパス / WBS=n / BR_EDR_ONLY / A2DP=n / SSP / ACL=2) | 全項目一致 | 不要 | 可(実装後) |
| S4 | メモリ使用量 | S1 | `idf.py size` / `size-components` / `size-files` | DRAM/IRAM 不足なし。I2S DMA + リングバッファ + タスクスタック確保可 | 不要 | 可(実装後) |
| S5 | パーティション / フラッシュ | S1 | app パーティション空き、NVS パーティション有無を確認 | app 空き十分。NVS(ボンド + ピア BDA)確保 | 不要 | 可(実装後) |
| S6 | GPIO 割り当て静的検証 | S1 + spec §4.3 | I2S(26/25/27)/ LED(32/33/13)/ ボタン(14)がストラッピング・フラッシュ・入力専用ピンと衝突しないか | 衝突なし。spec §4.3 と一致 | 不要 | 可(実装後) |
| S7 | 静的解析 | S1 | `idf.py clang-check` 相当 / `cppcheck`(導入可なら) | Critical / High 指摘なし | 不要 | 可(ツール導入次第) |
| S8 | データパス・排他レビュー | 設計書 + S1 | recv コールバック内処理量、FreeRTOS ringbuffer、I2S タスク、再接続タイマの静的レビュー(spec §6.1 R4/R5) | コールバックは memcpy + ringbuffer 書込のみ。競合なし | 不要 | 可(レビューのみ) |
| S9 | 状態遷移レビュー | 設計書 + S1 | §5.3 SCO 維持 / §8.2 再接続 / §5.2 CIND 応答 がコードに反映されているか | 仕様通りの状態遷移 | 不要 | 可(レビューのみ) |
| S10 | WBS Phase 2 ビルド可否(R3) | S1 | `CONFIG_BT_HFP_WBS_ENABLE=y` でリビルド | ビルド成功(Phase 2 の実現性) | 不要 | 可(実装後) |

### 4.2 HW 実測テスト(仕様 §11。本環境で実施不可 — 手順書化のみ)

| ID | 目的(仕様§11) | 前提 | 手順 | 期待結果 | 実機要否 | 本環境実施可否 |
|---|---|---|---|---|---|---|
| T1 | 起動 | 書込済 ESP32 | 電源投入、`idf.py monitor` でログ確認 | 10 秒以内に discoverable / 既ボンド時は自動接続開始。PWR LED 点灯 | 要 | **不可** |
| T2 | ペアリング | T1、SRL-3 電話ペアリングモード | SRL-3 から `DummyIntercom` を選択(Just Works) | PIN なしで成功。ログに `ESP_HF_CONNECTION_STATE_EVT: SLC_CONNECTED`。NVS にピア BDA 保存 | 要 | **不可** |
| T3 | **SCO 確立(最重要 / R4)** | T2 完了 | SRL-3 でユニバーサルインターコム開始。ログ + btmon(補助)で BCS/SCO パラメータ確認 | 3 秒以内に SCO 確立(`ESP_HF_AUDIO_STATE_EVT` connected)、SCO LED 点灯、J1 に音声。CVSD で確立。**codec ネゴでハングしない** | 要 | **不可** |
| T4 | 音声内容 / グループ音声 | T3 完了、2 台目インカム | 2 台目から通話 + 装着者発話。recv コールバックの RMS ログを取得 | 音声識別可能。グループ音声が HFP に乗るか RMS ログで記録(spec R1) | 要 | **不可** |
| T5 | エンドツーエンド遅延 | T3 完了、オシロ | SRL-3 マイクに拍手、SRL-3 側音声と J1 出力をオシロ比較 | < 100 ms(可能なら < 60 ms)、変動なし(spec §6) | 要 | **不可** |
| T6 | 連続性 | T3 完了 | 30 分連続受信、断/無音区間をログ集計 | 3 秒以上の無音・断が発生しない | 要 | **不可** |
| T7 | 出力レベル | T3 完了、テスタ/オシロ | J1 出力を LINE / MIC 各モードで測定 | LINE: 0.3–1.0 Vrms / MIC: 3–30 mVrms(spec §6) | 要 | **不可** |
| T8 | 再接続 | T2 完了 | SRL-3 電源断→復帰、距離を離す→戻す | 手動操作なしで再接続、LINK / SCO 復帰(spec §5.3-5、再接続タイマ 5 秒) | 要 | **不可** |
| T9 | 無音時ノイズ | T3 完了、レコーダー | 無音時に J1 をレコーダー録音、スペクトル確認 | ハム・ノイズが -60 dBFS 相当以下(絶縁トランス使用時) | 要 | **不可** |
| T10 | 電源断耐性 | 動作中 | 動作中に電源遮断 ×10 | 毎回正常起動、NVS 破損なし、再ペアリング不要(spec §9) | 要 | **不可** |
| T11 | カメラ連携 | T3 + T7(MIC) | J1(MIC)→ Insta360 マイクアダプター → X4 録画 | X4 録画に音声記録、レベル適正(spec R7) | 要 | **不可** |
| T12 | ボンディング消去 | ボンド済 | BOND-CLR ボタン(GPIO14)3 秒長押し | NVS のボンド + ピア BDA 消去 → `esp_restart` → 初回ペアリング可能 | 要 | **不可** |
| T13 | コーデック互換 | ログ / btmon | BRSF / `AT+BAC` / `ESP_HF_BCS_RESPONSE_EVT` / SCO パラメータを確認 | SRL-3 が CVSD を受容。WBS 必須なら R3(Phase 2 = `CONFIG_BT_HFP_WBS_ENABLE=y`) | 要 | **不可** |

### 4.3 追加推奨テスト(仕様§11 外、DEBUGGER 提案)

| ID | 目的 | 手順 | 期待結果 | 実機要否 | 本環境実施可否 |
|---|---|---|---|---|---|
| A1 | 未知 AT コマンド耐性(spec R5) | ログで SRL-3 が送る全 AT を列挙。未対応 AT 受信時の挙動を確認 | UNAT を全ログ化し `esp_hf_ag_unknown_at_send` で `OK`、SLC 維持 | 要 | **不可**(ログ解析手順のみ提示) |
| A2 | LED 状態遷移(spec §7.3) | 各状態(未接続/ページング/ACL/SCO/バッファ異常)を作り LED を目視 | 仕様§7.3 のパターンと一致 | 要 | **不可** |
| A3 | 上り無音送出の確認(spec §5.3-4) | btmon で上り SCO ペイロード / `esp_hf_ag_outgoing_data_ready` の周期を確認 | ゼロ PCM が送出されている | 要 | **不可** |
| A4 | ジッタバッファ挙動(spec §6) | ジッタバッファ長を 40 / 60 / 100 ms で切替え、ドロップ率と遅延を計測 | 60 ms 初期値でドロップなし、遅延許容内 | 要 | **不可** |
| A5 | 長時間エージング | 2〜3 時間連続受信 | メモリリーク・ヒープ枯渇・ウォッチドッグリセットなし | 要 | **不可** |
| A6 | 電源電流 | 5V ラインの電流を測定(接続 + SCO 受信時) | 平均 約 100–160 mA @5V(spec §9) | 要 | **不可** |
| A7 | 内蔵 DAC 代替経路(spec §4.2) | GPIO25/26 内蔵 8bit DAC + RC で音声確認(緊急代替) | 音声は出るが SNR 約 -48dB(記録のみ) | 要 | **不可** |
| A8 | btmon による HCI 切り分け(R4) | Linux PC + CSR8510 ドングルで `btmon` 取得、SLC/BAC/BCS/SCO を確認 | codec ネゴのシーケンスが HFP 1.7 通り | 要(PC + ドングル) | **不可** |

---

## 5. テスト実施結果

### 5.1 実施済み(準備フェーズ — `build_probe_espidf/` によるビルド環境健全性、2026-09-02)

※ B10 の「app ~760KB / 空き 51%」は IDF 例そのままの値。本実装の実測は S1/S4/S5(719KB / factory 1920KB / 空き 63%)。

| ID | 条件 | 操作 / 入力 | 期待結果 | 実測結果 | 判定 |
|---|---|---|---|---|---|
| B1 | ESP-IDF 環境 source | `idf.py --version` | v5.4 系 | ESP-IDF v5.4 | **OK** |
| B2 | `build_probe_espidf/` | `idf.py set-target esp32` | 成功 | sdkconfig 生成成功 | **OK** |
| B3 | 同上 | `idf.py build` | HFP-AG 例がビルド成功 | `hfp_ag.bin` 生成 | **OK** |
| B4 | `sdkconfig.defaults` に追記 | `CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y` | 反映 + リビルド成功 | sdkconfig 反映確認、成功 | **OK** |
| B5 | 同上 | `CONFIG_BT_HFP_WBS_ENABLE=n` | CVSD 固定でビルド成功 | `# ... is not set`、成功 | **OK** |
| B6 | 同上 | `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y` | BLE 無効でビルド成功 | 成功 | **OK** |
| B7 | 同上 | `CONFIG_BT_A2DP_ENABLE=n` | ビルド成功 | 成功 | **OK** |
| B8 | WBS 切替 | `CONFIG_BT_HFP_WBS_ENABLE=y` | Phase 2 経路がビルド成功 | mSBC 込みで成功 | **OK** |
| B9 | IDF コンポーネント | `driver/i2s_std.h` の存在と API 確認 | 新 I2S ドライバあり | ヘッダ + 主要 API 確認 | **OK** |
| B10 | 4MB flash / large 파티션 | `idf.py size` | RAM / Flash に収まる | app パーティション空き 51%、image 760KB | **OK** |
| B11 | ビルド全体 | warning 精査 | 警告 0 | 例コード由来警告 0 | **OK** |
| B12 | 生成物 | bootloader + partition + app | 3 バイナリ生成 | 生成 | **OK** |

### 5.2 実施済み(本環境 — T04 本テスト / `main/` 実装、2026-09-02)

環境: `export IDF_TOOLS_PATH=/home/subcon/.espressif && . /home/subcon/esp/esp-idf/export.sh`。
手順: `idf.py fullclean && idf.py set-target esp32 && idf.py build`。

| ID | 条件 | 操作 / 入力 | 期待結果 | 実測結果 | 判定 |
|---|---|---|---|---|---|
| S1 | `main/` 実装、esp32 target | `idf.py fullclean && idf.py build` | エラー 0、`.bin` 生成 | ビルド成功(exit 0)。`dummy_intercom.bin` 719,488 B 生成。bootloader / partition-table / app の 3 バイナリ生成 | **OK** |
| S2 | 同上 | ビルドログの warning 精査(`-Werror=all` 有効) | 本実装ソース由来の警告 0 | ビルドログ全体で `warning` 0 件。`main/*.c` 7 ファイルすべてコンパイル、警告なし。`idf.py set-target` の kconfig 警告も 0 | **OK** |
| S3 | ビルド後 `sdkconfig` | spec §5.1 / design §9.5 の 12 項目を実効 `sdkconfig` と突合 | 全項目一致、`CONFIG_BT_SSP_ENABLED` は不在 | `BT_HFP_AUDIO_DATA_PATH_HCI=y` / `BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y` / `BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1` / `BT_HFP_WBS_ENABLE`=未設定(=n) / `BTDM_CTRL_MODE_BR_EDR_ONLY=y` / `BT_BLE_ENABLED`=未設定 / `BT_A2DP_ENABLE`=未設定 / `BT_HFP_AG_ENABLE=y` / `BT_HFP_CLIENT_ENABLE`=未設定 / `BT_SPP_ENABLED`=未設定 / `BT_ACL_CONNECTIONS=2` / `ESPTOOLPY_FLASHSIZE="4MB"`。`CONFIG_BT_SSP_ENABLED` は不在(kconfig 警告なし)。全一致 | **OK** |
| S4 | 同上 | `idf.py size` / `size-components` | DRAM / IRAM に静的余裕あり | Flash Code 484,782 B / Flash Data 117,936 B。IRAM 97,150/131,072 B = **74.1%(空き 33,922 B)**。DRAM 静的 43,428 B(.bss 23,952 + .data 19,476)、空き 81,152 B。自作コード(`libmain.a`)は 6,782 B(IRAM 0)。※ Bluedroid の実行時ヒープ消費は実機でしか測れない(部分実施) | **OK(注記あり)** |
| S5 | 同上 | パーティション / フラッシュ確認 | app 空き十分、NVS 確保 | `partitions.csv`: nvs 24K @0x9000 / phy_init 4K / factory **1920K @0x10000**。app 719K → factory の **37% 使用 / 63% 空き**。NVS 24K は Bluedroid ボンド DB + `peer_bda` に十分 | **OK** |
| S6 | `app_config.h` + spec §4.3 | GPIO 重複・禁止ピン(ストラッピング 0/2/5/12/15、フラッシュ 6–11、入力専用 34–39)チェック | 衝突なし、spec §4.3 一致 | 使用ピン: I2S 26/25/27、LED 32/33/13、BTN 14。**重複なし**。ストラッピング/フラッシュ/入力専用ピンとの衝突なし。GPIO13/14 は JTAG(MTCK/MTMS)だが JTAG 不使用のため可(spec §4.3 が明示)。spec §4.3 の表と完全一致 | **OK** |
| S7 | `main/` | 静的解析(`cppcheck` 等) | Critical/High なし | `cppcheck` は本環境に未導入 → 未実施。代替として **`-Werror=all -Wextra` でのクリーンビルド**(S2)+ 手動コードレビュー(S8/S9)で担保 | **代替実施** |
| S8 | `main/` コードレビュー | recv_cb/send_cb の禁止 API、timer cb の直呼び、mem_release 位置、callback 登録順 | 違反なし(設計の意図的例外のみ) | `recv_cb`: RMS 加算ループ + `audio_i2s_write`(内部は `xRingbufferSend(...,0)` と非ブロッキング receive のみ)。**ログ / malloc / ブロッキング / `i2s_channel_write` なし** ✓。`send_cb`: `memset` + `return len` のみ ✓。timer cb(keepalive/reconnect/dial)は全て `hf_post`(dispatch)のみ ✓。**例外**: `tx_ready_timer_cb` が `esp_hf_ag_outgoing_data_ready()` を直呼び(design §5.6 の意図的例外)→ **妥当と評価**(下記 §6-A)。`esp_bt_controller_mem_release(BLE)` は controller init の**前** ✓(app_main.c:50)。`esp_hf_ag_register_data_callback` は `esp_bluedroid_enable()` + `esp_hf_ag_init()` の**後** ✓ | **OK(1 例外は妥当)** |
| S9 | `main/` ロジックレビュー | HFP イベント網羅性 / 通話偽装 / keepalive・reconnect 遷移 / ジッタバッファ分岐 / isqrt / NVS エラー / BOND-CLR | 設計 §4.2 / §6.4 と整合 | HFP イベント: design §4.2 の全イベントを処理、`default:` でログ ✓。状態名ログ配列は enum 範囲内(OOB なし)✓。通話偽装: §6.4 / U-03=(a) 通り `call=1` まで進め、簡略版切替コメントあり ✓。keepalive(3s, `s_slc && !s_sco` guard)/ reconnect(5s, `!s_slc && has_peer` guard)の start/stop 遷移は正しい ✓。ジッタバッファ: プリフィル(30ms)→ 再生、オーバーラン=古い側破棄で最新優先、アンダーラン=無音パディング + 再プリフィル ✓。`isqrt64`(整数ニュートン法)は境界値(0,1,2,4)で検証、正しい ✓。NVS: `nvs_open` 失敗を graceful 処理 ✓(ただし §6-C 参照)。BOND-CLR: §6-B の不具合あり | **OK(§6-B/C の指摘あり)** |
| S10 | `main/` + WBS 有効 | `CONFIG_BT_HFP_WBS_ENABLE=y` を追加してリビルド | ビルド成功(Phase 2 の実現性) | `main/`(`CONNECTED_MSBC` 分岐込み)+ WBS=y で **ビルド成功、警告 0**。app 722,496 B。Phase 2(mSBC/16kHz)への拡張はビルド上は実現可能 | **OK** |

### 5.3 未実施

- T1〜T13、A1〜A8: **未実施**(理由: ESP32 実機・SRL-3 実機・DAC/出力機材・オシロ・BT ドングルが本環境に無い。R11)。手順は §4.2 / §4.3、README「Hardware smoke test」に記載。

---

## 6. 不具合一覧

T04 本テスト(S8/S9 コードレビュー)で検出。ビルド・警告・sdkconfig・サイズ・GPIO(S1〜S7, S10)に不具合なし。

> **最終ステータスは §7.5 の表を参照。** §6-B / §6-C / §6-F は再テストで **Resolved**、§6-D / §6-A / §6-E / §6-G は BOSS 判断で **Accept**。以下は検出時の記録。

### 6-A(妥当と評価 / 不具合ではない): `esp_hf_ag_outgoing_data_ready()` の timer 直呼び

- **箇所**: `bt_data_path.c` `tx_ready_timer_cb`(esp_timer 文脈、7.5ms 周期、SCO 中のみ)。
- **CODER 申し送り**: design §8.5 チェック2(esp_timer cb から `esp_hf_ag_*` 直呼び禁止)の意図的例外。
- **DEBUGGER 評価**: **妥当**。`esp_hf_ag_outgoing_data_ready()` → `btc_hf_ci_sco_data()` → `BTA_AgCiData()` は `osi_malloc(sizeof(BT_HDR))` + `bta_sys_sendmsg()`(BTU タスクキューへ**非同期ポスト**)。Bluedroid の状態を同期的に変更しない = §8.5 チェック2 が対象とする「状態変更系制御 API と BTC タスクの競合」には該当しない。esp_timer 文脈でのコストは µs オーダー(malloc + enqueue)。公式例は semaphore + 専用タスク経由だが、本機の上りは常時無音のためフレームサイズ gating 用のタスク/リングバッファが不要で、この簡略化は妥当。
- **残リスク(Low)**: メモリ逼迫時に `osi_malloc` 失敗で trigger が黙って 1 フレーム落ちる(`BTA_AgCiData` が NULL チェック)。受信専用機では無害。実機で `bta_ag_sco_read_cback: status(2)` が多発したら design §6.5 の通り周期調整。

### 6-B: BOND-CLR が Bluedroid ボンド DB を消し切らない可能性(Medium)

| 項目 | 内容 |
|---|---|
| 発生条件 | SRL-3 とボンド済みの状態で BOND-CLR ボタンを 3 秒長押し |
| 再現手順 | (実機必要 / 静的解析による指摘)`leds.c` `leds_task` → `storage_factory_reset()` → 直後 `esp_restart()` |
| 期待結果 | NVS のボンド情報 + ピア BDA が消去され、再起動後に初回ペアリング可能(spec T12) |
| 実際の結果(推定) | 自前 NVS キー `peer_bda` は同期 `nvs_commit` で確実に消える。しかし `esp_bt_gap_remove_bond_device()` は**非同期**(`btc_transfer_context` で BTC タスクへメッセージポスト、実際の削除と `btc_config_flush()`→NVS 書き込みは BTC タスク文脈)。`storage_factory_reset()` は削除完了を待たずに return し、`leds_task` が即 `esp_restart()` を呼ぶ。**BTC タスクがメッセージを処理する前に再起動すると、Bluedroid の link key が NVS に残る** |
| ログ | (実機 T12 で `idf.py monitor` 確認要) |
| 推定原因 | `esp_bt_gap_remove_bond_device()` の非同期性。design §8.5 は「reset 後即 esp_restart なので競合窓なし」とするが、この論拠は `s_h` / `s_peer` のデータ競合のみを想定しており、**非同期ボンド削除の完了**を考慮していない。`leds.c` の `vTaskDelay(100ms)` は `storage_factory_reset()` の**前**にあり待ちにならない |
| 影響 | 再起動後デバイスは discoverable に戻る(`storage_has_peer()==false`)が、Bluedroid 側に古い link key が残ると、SRL-3 が古いキーで再接続を試みてペアリングが不整合になる。ユーザーが SRL-3 側のペアリングも消せば復旧可能。**BTC タスクは leds タスク(prio 5)より高優先度のため、単一ボンド + 通常タイミングでは処理される可能性が高いが、保証はない**(複数ボンド時や負荷時に顕在化) |
| 修正案 | `storage_factory_reset()` の remove ループ後、`esp_bt_gap_get_bond_device_num()` が 0 になるまでポーリング(≤1s タイムアウト)、または `vTaskDelay(pdMS_TO_TICKS(300))` を入れてから return する。`esp_restart()` はその後で呼ぶ |
| 重大度 | **Medium**(T12 は spec の受け入れ項目。単体アプライアンスで factory reset の信頼性は重要。ただし部分的失敗でユーザー復旧は可能) |

### 6-C: `nvs_flash_init` 二重失敗時の挙動が設計と相違(Low)

| 項目 | 内容 |
|---|---|
| 箇所 | `app_main.c:39-44`。`nvs_flash_init()` 失敗 → `nvs_flash_erase()` → 再 `nvs_flash_init()` → `ESP_ERROR_CHECK(err)` |
| 現行動作 | 再試行も失敗すると `ESP_ERROR_CHECK` が abort → パニック → 再起動ループ |
| 設計(§9.3) | 「ログして BT 無効で LED エラー表示(PWR 遅い点滅)」 |
| 影響 | NVS 全損は flash 破損を示唆し稀。だが再起動ループは PWR 点滅より診断しにくい |
| 修正案 | 設計 §9.3 通り、二重失敗時は `ESP_LOGE` + BT 起動をスキップし PWR 遅い点滅ループへ。または設計を実装に合わせて更新(BOSS 判断) |
| 重大度 | **Low** |

### 6-D: CoD 値が spec / 設計注記と相違(Low / BOSS 既認識)

| 項目 | 内容 |
|---|---|
| 実装値 | `bt_hf_ag.c:133-137` `major=ESP_BT_COD_MAJOR_DEV_PHONE(2)` / `minor=0x03` / `service=ESP_BT_COD_SRVC_TELEPHONY(0x200) | ESP_BT_COD_SRVC_AUDIO(0x100)` → 全 24bit CoD = **0x60020C**(service 0x300<<13 | major 2<<8 | minor 3<<2) |
| spec §5 / design §0-D | 「概ね 0x40020C」(= TELEPHONY のみ) |
| 原因 | 設計書の**内部不整合**: §0-D は「≈0x40020C」だが §4.1 step 10 のコードは `TELEPHONY | AUDIO` の両ビット。CODER は §4.1 を忠実に実装 |
| 影響 | AUDIO サービスビット追加は音声機器として妥当で、SLC には無害の見込み。`app_config.h` にフォールバック(0x20040C)コメントあり |
| 修正案 | DESIGNER が設計 rev.5 で §0-D と §4.1 を整合。AUDIO ビットの要否を判断(実機 SLC 不成立時は literal へ) |
| 重大度 | **Low**(BOSS 既認識) |

### 6-E(観察 / 対応不要): 診断カウンタ・RMS 累積の非アトミックアクセス

`s_rms_acc`(int64、32bit Xtensa で非アトミック)/ `s_overrun` / `s_underrun` は複数タスクから非同期アクセス。design §8.5 が「取りこぼし許容」と明記済み。最悪でも 1 秒に 1 回診断ログの数値が不正確になる程度。**対応不要**。`rms_timer_cb` の `ESP_LOGI` が esp_timer タスク(7.5ms の tx_ready と同居)を数 ms 占有しうる点も Low 観察(上りは無音なので実害小。気になれば RMS ログを BtAppT へ dispatch)。

---

## 7. 再テスト結果

### 7.1 第 1 回再テスト(2026-09-02 / ISS-019・ISS-022 修正 / 変更: `bt_hf_ag.c/.h`, `leds.c`, `app_main.c`)

| ID | 内容 | 結果 | 判定 |
|---|---|---|---|
| RT-1 | S1 リビルド | `idf.py fullclean && idf.py build` → **exit 0、警告 0**。`dummy_intercom.bin` 720,480 B(前回 +992 B、BOSS 報告と一致)。app パーティション 63% 空き維持 | **OK** |
| RT-2 | S3 sdkconfig 変化なし | `sdkconfig.defaults` 未変更。実効 sdkconfig の 10 主要キー = v0.3 と同一(HCI データパス×2 / MAX_SYNC_CONN=1 / WBS=n / BLE無効 / A2DP無効 / BR_EDR_ONLY / SSP不在 / FLASHSIZE 4MB / PARTITION_TABLE_CUSTOM) | **OK** |
| RT-3 | §6-B(ISS-019)修正確認: BOND-CLR 経路 | `leds.c` の BOND-CLR は `bt_hf_ag_request_factory_reset()` を呼ぶだけになり、**`esp_bt_gap_*` / `storage_*` / `esp_restart` の直呼びが無くなった**(include も `storage.h` / `esp_system.h` 削除)。実処理は `HFW_FACTORY_RESET` として **BtAppT** で実行: `storage_factory_reset()` → `esp_bt_gap_get_bond_device_num()==0` を **50ms 刻み・最大 1s** ポーリング → `vTaskDelay(300ms)`(BTC の bond NVS フラッシュ待ち)→ `esp_restart()`。**§6-B の非同期削除レースは解消**。§8.5(GAP API は BtAppT のみ)とも整合 | **OK** |
| RT-4 | §6-B: 起動直後(BtAppT 未起動)のリトライ経路 | `leds_task` は dispatch 失敗時 `reset_sent` を立てず次 tick で再試行する設計。**ただし §6-F の不具合により、この経路は assert パニックになる**(下記)。修正意図(graceful リトライ)は未達 | **NG(§6-F)** |
| RT-5 | §6-C(ISS-022)修正確認 | `app_main.c`: `nvs_flash_erase()` の戻り値を判定(`== ESP_OK` の時のみ再 init)。二重失敗時は `ESP_ERROR_CHECK` の abort ではなく **`ESP_LOGE` + `pwr_blink_forever(900)`**(PWR 遅い点滅、無限、BT 起動せず)= **設計 §9.3 通りの縮退**。`pwr_blink_forever` は PWR GPIO を自前 config してから点滅(leds_init 前でも動作) | **OK** |
| RT-6 | 回帰: S8(コールバック規律) | `bt_data_path.c` / `audio_i2s.c` / `bt_app_core.c` / `storage.c` は **md5 一致 = 無変更**。recv_cb / send_cb / データパスに影響なし | **OK** |
| RT-7 | 回帰: S9(該当箇所以外) | `bt_hf_ag.c` 差分は HFW_FACTORY_RESET(enum + case ハンドラ + `bt_hf_ag_request_factory_reset()`)に**局所化**。577→604 行(+27)。`handle_stack_up` / `handle_conn_state` / `handle_audio_state` / keepalive / reconnect / dial / GAP cb / HF cb は無変更。通常起動シーケンス(NVS→leds→storage→i2s→mem_release(BLE)→controller→bluedroid→BtAppT→hf_ag)は NVS 縮退分岐の追加のみ | **OK** |
| RT-8 | 回帰: `esp_bt_controller_mem_release(BLE)` 位置 | controller init の前(`app_main.c:70`)を維持 | **OK** |

### 7.2 再テストで新規検出した不具合

#### §6-F: `bt_app_work_dispatch()` が queue 未生成時に false を返さず assert パニックする(Low)

| 項目 | 内容 |
|---|---|
| 発生条件 | `bt_app_task_start_up()`(= work queue 生成、`app_main.c:95`)より前、または同関数に到達しない失敗経路(`fatal_blink()` = BT 起動失敗、`pwr_blink_forever()` = NVS 全損)で、BOND-CLR ボタンを 3 秒以上長押し |
| 再現手順 | (静的解析による指摘 / 実機で確認可)`leds_task`(app_main:66 で起動)が btn_held 3000ms で `bt_hf_ag_request_factory_reset()` → `bt_app_work_dispatch()` → `bt_app_send_msg()` → `xQueueSend(bt_app_task_queue, ...)`。この時 `bt_app_task_queue == NULL`(bt_app_core.c:21、start_up まで NULL) |
| 期待結果 | `bt_hf_ag.h` / `leds.c` のコメント通り「queue 未生成なら **false を返し、leds_task が次 tick で再試行**」 |
| 実際の結果 | `xQueueSend()` 冒頭の `configASSERT( pxQueue )`(FreeRTOS queue.c:936)が発火。`CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE=y`(level 2, not silent)のため **abort → パニック → 再起動**。ボタンを押し続けると **再起動ループ**(ボタンを離すと正常起動、ただし factory reset は未完了) |
| ログ | `assert failed: xQueueGenericSend queue.c:936 (pxQueue)` |
| 推定原因 | `bt_app_send_msg()` に **`bt_app_task_queue == NULL` のガードが無い**(`if (msg == NULL)` のみ)。ISS-019 修正で導入した「dispatch 失敗 → リトライ」設計が、この assert により graceful に機能しない |
| 影響 | 通常経路(BT 正常起動、~1〜2s で `bt_app_task_start_up` 到達)では BOND-CLR 3s 発火時に queue は既に生成済みで **問題は起きない**。影響は「BT/NVS 初期化に失敗した状態でユーザーが factory reset を試みる」「起動が異常に遅い(>3s)」場合に限定。自己回復(ボタンを離せば正常起動)可能 |
| 修正案 | `main/bt_app_core.c` の `bt_app_send_msg()`:<br>`if (msg == NULL \|\| bt_app_task_queue == NULL) { return false; }`<br>これで `bt_app_work_dispatch()` が設計通り false を返し、`leds_task` のリトライが機能する(BT が一生上がらなくてもリトライが空振りするだけでクラッシュしない) |
| 重大度 | **Low** — happy path 影響なし・自己回復可。ただし ISS-019 修正が意図した安全経路(リトライ)を無効化しているため修正推奨 |

### 7.3 第 2 回再テスト(2026-09-02 / ISS-024 = §6-F 修正 / 変更: `bt_app_core.c` 1 行)

| ID | 内容 | 結果 | 判定 |
|---|---|---|---|
| RT-9 | S1 リビルド | `idf.py fullclean && idf.py build` → **exit 0、警告 0**。`dummy_intercom.bin` 720,480 B(変化なし。ガード 1 行はコードサイズ影響ほぼゼロ)。app パーティション 63% 空き | **OK** |
| RT-10 | §6-F(ISS-024)修正確認 | `bt_app_core.c` `bt_app_send_msg()`: `if (msg == NULL \|\| bt_app_task_queue == NULL) { return false; }` に変更。`bt_app_task_queue == NULL`(start_up 前)で **`xQueueSend` に到達せず false を返す** → `bt_app_work_dispatch` が false 伝播(`param_len==0` / `>0` 両経路とも `return bt_app_send_msg(&msg)`)→ `bt_hf_ag_request_factory_reset()` が false → `leds_task` は `reset_sent` を立てず次 tick で再試行。**assert パニックは発生しない** | **OK** |
| RT-11 | §6-F: false 伝播経路の全チェーン | `leds_task`(btn 3s)→ `bt_hf_ag_request_factory_reset()` → `bt_app_work_dispatch(hf_work_handler,0,&w,sizeof(w),NULL)` → `bt_app_send_msg()` → **queue NULL なら false**。leds.c の `if (bt_hf_ag_request_factory_reset())` が false → LED 消灯せず・`reset_sent=false` のまま → 次 50ms tick で再試行(ボタン保持中は `btn_held_ms >= BTN_HOLD_MS` が継続 True)。BtAppT 起動後は dispatch 成功して factory reset 実行 | **OK** |
| RT-12 | 回帰(第 2 回) | 変更は `bt_app_core.c` の 1 行のみ。`bt_hf_ag.c` / `leds.c` / `app_main.c` / `bt_data_path.c` / `audio_i2s.c` / `storage.c` は前回から md5 変化なし。通常 dispatch 経路(queue 生成後)の挙動は不変。`idf.py size` = IRAM 74.12% / DRAM .bss 23,952 / .data 19,476(変化なし) | **OK** |

### 7.4 §6-F 修正に伴う観察 §6-G(Very Low / Accept 可)

`bt_app_work_dispatch()` は `param_len > 0` の場合、`bt_app_send_msg()` 呼び出し**前**に `malloc(param_len)` + `memcpy` を実行する。`bt_app_send_msg()` が false(queue NULL or queue full)を返すと、この `msg.param`(≈ `sizeof(hf_work_t)` ≈ 60 B)が **解放されずリーク**する。これは Espressif 公式例のワークキュー実装に由来する既存パターン(`param_len==0` 経路は malloc しないためリークしない)。

- **影響**: §6-F の失敗経路(BT/NVS 初期化失敗 + BOND-CLR 長押し継続)でのみ発生。50ms ごとの再試行で ~60 B/回。ユーザーがボタンを数秒で離せば累計数 KB、次回リセット/電源断で回収。**通常運用では発生しない**(queue 生成後は dispatch 成功、失敗経路自体が異常状態)。
- **対応**: **Accept**(BOSS 判断)。厳密に潰すなら `bt_app_work_dispatch` の `bt_app_send_msg` 失敗時に `if (msg.param) free(msg.param);` を追加(公式例にも共通の改善)。今回のスコープ外。

### 7.5 最終 不具合ステータス

| ID | 内容 | 当初重大度 | 状態 |
|---|---|---|---|
| §6-B / ISS-019 | BOND-CLR 非同期ボンド削除レース | Medium | **Resolved**(RT-3。ポーリング + 300ms + BtAppT 実行) |
| §6-C / ISS-022 | NVS 二重失敗時 abort ループ | Low | **Resolved**(RT-5。縮退モード) |
| §6-F / ISS-024 | dispatch queue 未生成時 assert パニック | Low | **Resolved**(RT-10/11。queue NULL ガード) |
| §6-D / ISS-020(想定) | CoD 実装値 0x60020C vs 注記 0x40020C | Low | **Accept** — 設計書 §0-D / §4.1 の内部不整合。DESIGNER が設計 rev.5 で整合。AUDIO ビットは SLC 無害の見込み、`app_config.h` にフォールバックコメントあり。実機 SLC 不成立時は literal 0x20040C へ |
| §6-A / ISS-021(想定) | `tx_ready_timer_cb` の `esp_hf_ag_outgoing_data_ready()` 直呼び | 観察 | **Accept** — 非同期メッセージポスト(`BTA_AgCiData`→`osi_malloc`+`bta_sys_sendmsg`)で状態非変更。design §8.5 チェック2 の対象外。妥当と評価 |
| §6-E / ISS-023(想定) | 診断カウンタ・RMS 累積の非アトミックアクセス | 観察 | **Accept** — design §8.5 が「取りこぼし許容」と明記。最悪でも 1s に 1 回診断ログの数値が不正確になる程度 |
| §6-G(新規) | dispatch 失敗時の `msg.param` malloc リーク | Very Low | **Accept** — 公式例由来の既存パターン。§6-F の失敗経路でのみ、ボタン保持中に限り発生。数 KB 上限、電源断で回収 |

> ISS 番号(ISS-020/021/023)は BOSS の `issue_list.md` に準拠。本書の §6-D/§6-A/§6-E が対応(想定マッピング)。BOSS 決定により **すべて Accept**。

---

## 8. 未実施項目と理由

| 分類 | 項目 | 理由 | 解消方法 |
|---|---|---|---|
| 静的解析ツール | S7(`cppcheck` 等) | 本環境に未導入 | `-Werror=all -Wextra` クリーンビルド + 手動レビューで代替済み。必要なら BOSS が cppcheck 導入 |
| HW 実測 | T1〜T13 | ESP32 実機 / SRL-3 実機 / DAC・出力機材 / 計測器なし(R11) | ユーザー側で実機テストを実施。本書 §4.2 / README の手順に従う |
| HW 実測 | A1〜A8(追加推奨) | 同上 | 同上 |
| SCO / codec ネゴ | T3 / T13 / R4 | ESP-IDF HFP-AG + HCI データパスの codec ネゴ動作は実機依存(esp-idf#4967 の履歴あり) | ユーザー実機で `idf.py monitor` + btmon(補助)により SLC/BAC/BCS/SCO を確認 |
| コーデック判定 | T13 / R3 | SRL-3 の実挙動(CVSD 受容 / WBS 要求)が実機依存 | ユーザー実機。WBS 必須なら Phase 2(`CONFIG_BT_HFP_WBS_ENABLE=y`、ビルド可は S10 で確認済み) |
| ランタイムヒープ | S4 の一部 | Bluedroid の実行時ヒープ消費は実機起動でしか測れない | ユーザー実機で `idf.py monitor` 起動ログの `heap_caps_get_free_size` / `esp_get_free_heap_size` を確認 |

---

## 9. 最終判定

対象 commit: 未 commit(作業ツリー、`git add` 済)。最終ビルド `dummy_intercom.bin` 720,480 B / IRAM 74.12% / factory 63% 空き。

| 区分 | 判定 |
|---|---|
| ビルド環境健全性(準備フェーズ) | **合格** — §3(ESP-IDF v5.4 / esp32 で HFP-AG + HCI データパス + CVSD がビルド可、新 I2S ドライバあり)|
| 本実装 ビルド / 静的検証(S1〜S10) | **合格** — S1(ビルド exit 0)/ **S2(警告 0、`-Werror=all` 有効)** / S3(sdkconfig 12 項目一致、`CONFIG_BT_SSP_ENABLED` 廃止確認)/ S4(サイズ余裕、IRAM 74.1% は注記)/ S5(パーティション factory 63% 空き)/ S6(GPIO 25/26/27/32/33/13/14 衝突なし、ストラッピング/フラッシュ/入力専用ピン不使用、spec §4.3 一致)/ S8(recv/send cb に禁止 API なし、制御 API は BtAppT dispatch、直呼び例外は妥当)/ S9(HFP イベント網羅・通話偽装・keepalive/reconnect 遷移・ジッタバッファ分岐・isqrt・NVS エラー処理 整合)/ S10(WBS Phase 2 ビルド可)。S7 は `-Werror=all -Wextra` + 手動レビューで代替 |
| 再テスト(ISS-019 / ISS-022 / ISS-024) | **全 Resolved** — §6-B(Medium)/ §6-C(Low)/ §6-F(Low)。3 回のリビルドすべて警告 0、sdkconfig 不変、回帰なし(§7.1 / §7.3)|
| 不具合(最終) | **Critical 0 / High 0 / Medium 0 / Low(Open)0**。Resolved 3 件(§6-B / §6-C / §6-F)。**Accept 4 件**(§6-D CoD=設計側で整合、§6-A timer 直呼び=妥当、§6-E 非アトミック統計=許容、§6-G malloc リーク=公式例由来・失敗経路限定)|
| HW 実測テスト(T1〜T13、A1〜A8) | **未実施(実機なし。R11)** — ESP32 実機 / SRL-3 実機 / PCM5102A・出力機材 / オシロ / USB BT ドングルが本環境に無い。手順は §4.2 / §4.3 / README「Hardware smoke test」に記載。<br>**最大の未検証点 = T3(SCO 確立)**: ESP-IDF HFP-AG + HCI データパスの codec ネゴが SRL-3 実機で成立するか(spec R4 / ISS-008)。次点は T4(グループ音声が HFP に乗るか, R1)、T13(CVSD 受容, R3)、T6/T7(30 分連続の SCO 安定性・クロックドリフト)|
| **総合最終判定(本環境で可能な範囲)** | **合格** — 要求機能はビルド可能な形で実装され、`idf.py build` は警告 0 で成功、sdkconfig は spec §5.1 に完全準拠、静的検証(GPIO / コールバック規律 / 初期化順序 / 状態遷移ロジック)に不整合なし、回帰なし。**Critical / High / Medium 不具合ゼロ、Open な Low 不具合ゼロ**。<br>**機能保証は実機テスト待ち** — 音声疎通(T3/T4)、遅延(T5)、出力レベル(T7)、連続性(T6/T8)、再接続(T8)、カメラ連携(T11)、BOND-CLR(T12)、コーデック互換(T13)は ESP32 + SRL-3 実機でユーザーが実施する必要がある。特に T3 は本開発の最大の未検証点。 |

---

## 10. 参考 / 再現手順

```bash
export IDF_TOOLS_PATH=/home/subcon/.espressif
. /home/subcon/esp/esp-idf/export.sh
cd /home/subcon/SOURCE_CODE/android_BT_TEST

# S1〜S10(本実装のビルド + 静的検証)の再現
idf.py fullclean && idf.py set-target esp32 && idf.py build   # S1/S2: 警告0で dummy_intercom.bin 生成
grep -E 'CONFIG_BT_HFP_(AUDIO_DATA_PATH_HCI|WBS_ENABLE|AG_ENABLE)|CONFIG_BTDM_CTRL_(BR_EDR_SCO_DATA_PATH_HCI|BR_EDR_MAX_SYNC_CONN|MODE_BR_EDR_ONLY)|CONFIG_BT_(BLE_ENABLED|A2DP_ENABLE)' sdkconfig   # S3
idf.py size ; idf.py size-components                          # S4/S5

# ビルド環境検証(本書 §3、準備フェーズ)の再現
./tools/build_probe_espidf.sh
```

> 注意: 本テスト時、別セッションと `build/` ディレクトリでビルドが衝突すると `fatal error: sdkconfig.h: No such file` 等で中断する。同一 `build/` での同時ビルドは避けること。

### 実機テスト時の準備(ユーザー向け)

1. `src/` の本ファームを `idf.py set-target esp32 && idf.py build flash monitor`
2. 起動後 `idf.py monitor` のシリアルログで状態遷移を確認
   （`ESP_HF_CONNECTION_STATE_EVT` / `ESP_HF_AUDIO_STATE_EVT` / `ESP_HF_BCS_RESPONSE_EVT` / UNAT）
3. SRL-3 を「電話(Phone)ペアリング」モードにして `DummyIntercom` と接続
4. 必要に応じ Linux PC + USB BT ドングル(CSR8510 系)で `btmon` を取り、HCI レベルで
   SLC / `AT+BAC` / `AT+BCS` / SCO パラメータ(air mode, pkt type)を確認(R4 切り分け)
5. 本書 §4.2(T1〜T13)/ §4.3(A1〜A8)の手順で実測し、本書 §5 に結果を追記


---

## 11. CR-010 SENA SRL3 ペアリング対応 テスト(DEBUGGER)

> 状態: **C1〜C13 実施済み(2026-09-30、ユーザー指示により実施)**。実機項目 H1〜H7 は未実施。

### 11.1 テスト対象 / 環境 / revision
| 項目 | 内容 |
|---|---|
| 対象 | CR-010(`software_design.md` §13): SSP 無効化 → レガシー PIN "0000"(FIXED)、SLC 未接続中は常時 discoverable、CoD プロファイル切替マクロ |
| 対象ファイル | `main/app_main.c` / `main/bt_hf_ag.c` / `main/app_config.h` / `sdkconfig.defaults`(コメント)/ `dummy_intercom_spec.md` / README.md |
| 環境 | ESP32 / ESP-IDF v5.4 / `IDF_TOOLS_PATH=/home/subcon/.espressif`、`. ~/esp/esp-idf/export.sh` |
| revision | ベース commit `50dd7eb`(声のこもりを修正)+ **作業ツリー(CR-010 未コミット差分)**。実測時に `git rev-parse --short HEAD` と `git status --short main docs sdkconfig*` を記入: `50dd7eb` + 作業ツリー(main/app_config.h, app_main.c, bt_hf_ag.[ch], sdkconfig.defaults, docs 変更あり・未コミット)。ビルドは実行順に c1→c2→c3→c1b(既定へ復元)。最終ビルド成果物は既定値(LEGACY=1, COD=0) |
| 日付 | 2026-09-30 |
| 実機 | SENA SRL3 / HF ユニット実機 / ESP32 実機は**無し**(ビルド+静的検証のみ) |

### 11.2 テスト項目(§13.9 対応)
| ID | 種別 | 条件 | 操作 | 期待結果 | 実測 | 判定 |
|---|---|---|---|---|---|---|
| C1 | ビルド | 既定値(LEGACY=1, COD_PROFILE=0) | `idf.py build` | エラー/警告 0、bin 生成 | rc=0、warning/error 0。app bin 0xb1230(725,552B、パーティション 0x1E0000 の 63% 空き) | 合格 |
| C2 | ビルド | `BT_LEGACY_PAIRING 0` | 定数変更しビルド後、元へ戻す | ビルド成功(警告 0、`#if` 内の未使用変数なし) | BT_LEGACY_PAIRING 0 で rc=0、警告 0(io 未使用警告なし)。試験後に元へ復元(app_config.h 差分は CODER 変更の 8 行のみ) | 合格 |
| C3 | ビルド | `BT_COD_PROFILE 1` | 同上 | ビルド成功(警告 0) | BT_COD_PROFILE 1 で rc=0、警告 0。復元後に既定で再ビルド rc=0/警告 0 | 合格 |
| C4 | サイズ | 既定 | `idf.py size` | CR-010 前(bin/IRAM/DRAM)から有意な増加なし | Flash Code 488,922B / IRAM 97,150B(74.12%、残 33,922B)。HEAD 記録の bin 725,376B → 725,552B(+176B)。有意な増加なし | 合格 |
| C5 | 静的 | app_main.c | grep `ssp_en` | `bd_cfg.ssp_en = !BT_LEGACY_PAIRING;` が `esp_bluedroid_init_with_cfg` 前にある | app_main.c:83 `bd_cfg.ssp_en = !BT_LEGACY_PAIRING;` が `esp_bluedroid_init_with_cfg`(84)の前 | 合格 |
| C6 | 静的 | bt_hf_ag.c | grep `set_security_param` | `#if !BT_LEGACY_PAIRING` 内のみ(LEGACY=1 時に呼ばれない) | `set_security_param` は bt_hf_ag.c:153 のみで `#if !BT_LEGACY_PAIRING` 内(LEGACY=1 では未コンパイル)。`ssp_confirm_reply`(454)は CFM_REQ 内で ssp_en=false 時は到達しない | 合格 |
| C7 | 静的 | bt_hf_ag.c | grep `set_pin` / `BT_PIN_CODE` | `ESP_BT_PIN_TYPE_FIXED`、長さ 4、`_Static_assert(sizeof(BT_PIN_CODE)-1==4)`、戻り値 WARN ログ、PIN は "0000" | `ESP_BT_PIN_TYPE_FIXED`、長さ 4、`_Static_assert(sizeof(BT_PIN_CODE)-1==4)`、`BT_PIN_CODE "0000"`、失敗時 WARN ログを確認 | 合格 |
| C8 | 静的 | `bt_hf_ag_apply_scan_mode` | コードレビュー | 常に CONNECTABLE、`s_slc ? NON_DISCOVERABLE : GENERAL_DISCOVERABLE`、`s_slc` 宣言順序 OK、SLC_DISCONNECTED で `s_slc=false` の**後**に再呼出し | 常に CONNECTABLE、`s_slc`(61 行で宣言済み)で DISCOVERABLE/NON を切替、失敗時 WARN。SLC_CONNECTED(187)で不可視化、DISCONNECTED は `s_slc=false` の後(206)で再 discoverable。SAVE_PEER は s_slc=false の間 discoverable 維持。再接続タイマ不変。Low 指摘 L-1 あり | 合格 |
| C9 | 静的 | `bt_gap_cb` | コードレビュー | AUTH_CMPL 成功/失敗ログに BDA・device_name。PIN_REQ/CFM_REQ/KEY_NOTIF 残置。`HFW_SAVE_PEER`・再接続タイマ不変 | AUTH_CMPL 成功ログ `paired: <BDA> (<name>)`、失敗ログに BDA 追加。PIN_REQ/CFM_REQ/KEY_NOTIF 残置。HFW_SAVE_PEER 不変 | 合格 |
| C10 | 静的 | CoD | `BT_COD_PROFILE==1` 分岐 | major=AV(4)、minor は Assigned Numbers で 0x02(Hands-free)相当か確認、service=Audio\|Telephony。既定は既存値 | BT_COD_PROFILE==1: major=AV、minor=0x02、service は共通(Telephony|Audio)。既定値は従来と同一。minor 0x02 は Hands-free の値(Bluetooth Assigned Numbers 記憶に基づく確認。原文未参照) | 合格(注記あり) |
| C11 | 静的 | sdkconfig | `git diff sdkconfig sdkconfig.defaults` | defaults はコメントのみ変更。CONFIG 行の追加なし。build 後 `sdkconfig` に不要差分なし | sdkconfig.defaults はコメントのみ(3 行置換、CONFIG 行変更なし)。`sdkconfig` は変更なし。`sdkconfig.old` に WBS 行の差分あり(ビルド生成物の副産物・CR-010 由来ではない。L-2) | 合格 |
| C12 | 回帰 | 差分全体 | `git diff` 確認 | `bt_data_path.c` / `audio_*` / `storage.c` / mSBC・CVSD 切替 / キープアライブ / BOND-CLR / LED 無変更 | `git diff --stat` で bt_data_path.c / audio_*.c / storage.c / leds.c は無変更。差分は app_config.h/app_main.c/bt_hf_ag.[ch]/sdkconfig.defaults のみ(+41/-12)。mSBC・キープアライブ・BOND-CLR・LED のコード無変更(実機動作は未確認) | 合格 |
| C13 | 文書 | spec / design / README | grep `SSP`, `rev` | ペアリング記述が更新済み、rev と改訂表に CR-010、`§5.1` の旧 SSP 記述訂正、メモリ規約どおり同期 | spec rev 0.10(改訂表 CR-010 行、L171/203/205/293/318/321)、design §5.1 コメント訂正・§13、README(L71-73)を確認。整合 | 合格 |
| H1〜H7 | 実機 | §13.9 の 0〜7 | 下記 §11.5 | 下記 | **未実施(理由: 実機無し)** | 未実施 |

### 11.3 不具合一覧
不具合(合格判定に影響するもの)はなし。軽微な指摘:
| ID | 発生条件 | 期待 | 実際 | 推定原因 | 修正案 | 重大度 |
|---|---|---|---|---|---|---|
| L-1 | 保存ピアありで起動 | 起動ログが状態を正確に表す | `HFP-AG ready ... (reconnecting to stored peer)` / `(discoverable)` の表記が bt_hf_ag.c:172 のまま。CR-010 後はピア保存時も discoverable なので `(discoverable)` 表記が誤解を招く | ログ文言未更新 | 「reconnecting to stored peer, discoverable」等に更新(機能影響なし) | Low |
| L-2 | ビルド後 | 不要差分なし | `sdkconfig.old` に WBS 行の差分(`sdkconfig` 本体は無変更) | ビルド時に生成される旧設定のバックアップ | コミット対象から除外/無視 | Low |
| L-3 | 実機 | — | PIN が 0000 以外の相手や SSP 必須の相手は認証失敗(`auth failed` ログで判別)。また FIXED PIN では PIN_REQ_EVT が上がらず、設計どおり | 設計上のトレードオフ(U-1/R-2) | 実機で確認 | 情報 |

### 11.4 再テスト
(なし)

### 11.5 未実施項目と理由 / ユーザー向け実機確認手順
**未実施: 実機テスト全般(理由: SENA SRL3、ESP32 実機、HF ユニット実機が開発環境に無い)。**

手順:
1. 書込み後の初回は旧 SSP ボンドが残るため、BOND-CLR(ボタン 3 秒長押し)で消去してから開始(H0)。PC 側の `DummyIntercom` 登録も削除。
2. `idf.py flash monitor`。起動ログで `set_pin` 成功、`set_security_param` の ERROR ログが**無い**ことを確認(H1)。
3. PC(Windows/Android)から検索 → 番号比較/パスキーが出ず PIN "0000" のみ求められること(H2)。
4. SRL3 のユニバーサルインターコム・ペアリングを開始 → `DummyIntercom` が検出され、PIN 自動応答 → `AUTH_CMPL success`(相手名/BDA ログ)→ SLC 確立 → SCO 受信(H3)。
5. PC 等のピア保存済み・SLC 未接続でも SRL3 から検出できること、SLC 接続中は不可視(H4)。
6. 電源再投入後の自動再接続(H5)。
7. 検出されない場合: `BT_COD_PROFILE 1` で再ビルドして再試験。`auth failed` なら PIN 不一致(SENA が 0000 以外を使用)を疑い `BT_PIN_CODE` を変更(H6)。
8. 回帰: mSBC/CVSD、キープアライブ、BOND-CLR、LED(H7)。
9. HF ユニット(rev 0.8)との接続(レガシー PIN 側とのペアリング整合。設計 U-4)も確認。

期待ログ: `AUTH_CMPL: success, peer=<name> <bda>` / `SLC connected` / `scan mode: connectable+discoverable`(SLC 未接続時)/ `non-discoverable`(SLC 中)。※実際の文言は CODER 実装に合わせ記入。

### 11.6 最終判定
**ビルド・静的検証(C1〜C13)は合格**(警告 0、切替両値でビルド可、既存機能のコード無変更、文書同期済み)。Low 指摘 L-1/L-2 のみ。**実機項目(H1〜H7、SRL3/HF ユニット実機)は未実施(実機無し)のため、SRL3 で検出・ペアリングできるかは未検証**。ユーザーによる実機確認が必要。


## 12. CR-012 2×ESP32 I2S デジタル直結 テスト(DEBUGGER)

> 状態: **ベースライン準備フェーズ**。設計書 §15(承認済み)の §15.11 / T-1〜T-8 に合わせて項目を改訂済み。CODER 実装完了後にビルド・静的検証・回帰を実施する。ソースコードは変更していない。

### 12.1 テスト対象
- CR-012: #1(ルート `main/`)が HFP-HF(既定 BT_ROLE_HF。AG でも可)で受信した SCO 音声を 16kHz I2S マスタで出力し、#2(`hf_unit/`)が I2S スレーブ受信して HFP-HF で無線マイク送信する。ADC / PCM5102A 経路は廃止。
- 対象: `main/`(dummy_intercom)、`hf_unit/`(dummy_intercom_hf)、設計書 §15(承認済み)。

### 12.2 環境
- ESP-IDF v5.4(`IDF_TOOLS_PATH=/home/subcon/.espressif`、`~/esp/esp-idf/export.sh`)、ターゲット ESP32、Python 3.13 env。
- ビルドは別ディレクトリ(`idf.py -B <scratchpad>/bl_main`、`idf.py -C hf_unit -B <scratchpad>/bl_hf`)で実施し、リポジトリの `build/` は未変更。
- 実機(ESP32×2、SENA SRL3、配線)は無し。

### 12.3 ベースライン結果(CR-012 実装前・HEAD b6d211d)
| 対象 | ビルド | app サイズ | 空き(app パーティション 0x1e0000) | ブートローダ |
|---|---|---|---|---|
| main/ (`dummy_intercom.bin`) | 成功 | 0xb6640 (約 747KB) | 0x1299c0 (62% 空き、使用 約 38%) | 0x6560、空き 9% |
| hf_unit/ (`dummy_intercom_hf.bin`) | 成功 | 0xaba20 (約 703KB) | 0x1345e0 (64% 空き、使用 約 36%) | 0x6560、空き 9% |

- 警告: ビルドログ(idf_py stdout/stderr)に "warning" の出力は 0 件(下記追記を参照)。
- 実装後の比較基準: フラッシュ使用率(main 約 38% / hf 約 36%)、警告 0 を維持すること。

### 12.4 テスト項目(設計書 §15.11 準拠)
方式: `AUDIO_OUT_MODE`(main、既定 I2S16K=1)/ `AUDIO_IN_MODE`(hf_unit、既定 I2S=1)のコンパイル時切替。ビルドは別ディレクトリで実施し `build/` を汚さない。設定切替は `-D`/一時コピーで行い、ソースは変更しない。

**ビルド(B)** — 警告はベースライン(0 件)から増加なし
| ID | 項目 | 合格基準 |
|---|---|---|
| B-1 | main/ `AUDIO_OUT_MODE=1`, `BT_ROLE_HF` | 成功、警告 0 |
| B-2 | main/ `AUDIO_OUT_MODE=1`, `BT_ROLE_AG` | 成功、警告 0 |
| B-3 | main/ `AUDIO_OUT_MODE=0`(旧 PCM5102A 構成) | 成功、警告 0、旧 48k/ステレオ経路が残る |
| B-4 | hf_unit `AUDIO_IN_MODE=1` | 成功、警告 0、`adc_capture` が最終バイナリに残らない(map 確認) |
| B-5 | hf_unit `AUDIO_IN_MODE=0` | 成功、警告 0 |
| B-6 | サイズ回帰(ベースライン main 38% / hf 36%) | 双方パーティション内、増加は僅少 |

**静的検証(S)**
| ID | 観点(§15.11 静的検証・§15.9) | 確認方法 |
|---|---|---|
| S-1 | (a) `s_up_mono`(64×`I2S_UP_MAX`)/`s_stereo` のバッファ計算が I2S16K/DAC48K 両方でオーバーランなし。`s_stereo` は DAC48K のみ定義 | コード読解 |
| S-2 | (b) hf_unit の BTC/ISR 文脈に新規ブロッキング呼び出しなし(`send_cb` は `xRingbufferReceiveUpTo(...,0,...)` のみ、`fill_bytes` も非ブロック) | コード読解 |
| S-3 | (c) #1/#2 の I2S 設定一致: 16kHz/16bit/モノ/Philips/LEFT スロット、ピン 26=BCLK 25=WS 27(#1 DOUT/#2 DIN)、#1=MASTER・#2=SLAVE、MCLK なし | grep/読解 |
| S-4 | (d) `tools/gen_fir.py` 再生成で既存 ×6/×3 表の差分なし | 再生成して diff(一時出力先) |
| S-5 | (e) ×2 係数(`s_coef_x2`)の各フェーズ DC ゲイン≈1、32 taps/phase、L=2 | 数値計算 |
| S-6 | 半帯域 FIR `(-x0+9x2+16x3+9x4-x6)>>5` の DC ゲイン=1(和=32)、int16 クリップ、履歴のチャンク跨ぎ保持、2 サンプル毎出力 | 読解/数値 |
| S-7 | ドリフト補正: 1 ブロック最大 1 サンプル、間隔≥100ms、閾値バイト換算(8k/16k)が §15.6.3 の値(480/640/±320、960/1280/±640)と一致、`TX_RB_BYTES`(4096)>TARGET+HYST | 読解 |
| S-6b | 無音補填: 取得分コピー+0 埋めで `len` 返却、200ms 超で再プリフィル、`s_underrun++`。ADC モードでは従来コードパスが完全に残る | 読解 |
| S-8 | `i2s_capture` 失敗時は BT 起動を継続(縮退)、静的バッファのみ・malloc なし、`i2s_channel_read` 50ms タイムアウト、LOST 200ms 判定 | 読解 |
| S-9 | `i2s_channel_write` の戻り値処理(レート制限ログ)、`auto_clear` 設定、DMA 6×64 | 読解 |
| S-10 | `handle_audio_state` のレート通知順(`set_rate` → `bt_data_path_reset` 前、`set_active(true/false)` の CONNECTED/DISCONNECTED/ACL 切断) | 読解 |
| S-11 | CMake: `i2s_capture.c`/`esp_driver_i2s` 追加、`esp_adc`/`adc_capture.c` 維持。`sdkconfig`/`sdkconfig.defaults`/`patches/`/`build/` に手編集なし(git diff) | git diff |
| S-12 | 残存参照: 既定構成で ADC/48k/PCM5102A 前提の文言・コメント残り(`audio_dsp.c` 等のコメント更新含む)、README/hf_unit README/配線書の同期 | grep |

**回帰(R)**
| ID | 項目 | 基準 |
|---|---|---|
| R-1 | `bt_hf_client.c`(main)/`bt_hf_ag.c`/`bt_data_path.c`(main)/`leds.c`/`app_main.c`(main)/`patches/` に意図しない差分なし(§15.3「変更なし」区分) | git diff で空 |
| R-2 | CR-010/011(PIN 0000、CoD、scan mode、HF 役)関連コードの差分なし | git diff |
| R-3 | HF 役 #1 で送信タイマーを起動しない仕様(§15.2/R-8)が維持 | 読解 |
| R-4 | `AUDIO_OUT_MODE=0` で旧挙動(`I2S_OUTPUT_RATE_HZ=48000`/6/2ch/8/128)が保持 | 定数比較 |

**実機(T、未実施)** — 設計書 T-1〜T-8
| ID | 項目 | 合格基準 |
|---|---|---|
| T-1 | 配線導通(26/25/27/GND/5V) | 導通あり |
| T-2 | #1 に 1kHz を SCO 経由入力 → #2 RX で 16kHz 上に 1kHz(左スロット) | 1kHz 検出(R-1/R-4 のスロット・エッジ不一致リスク確認) |
| T-3 | CVSD/mSBC 双方で X4 に音声到達(#2 のレート切替追従) | 明瞭 |
| T-4 | 30 分連続、ドリフト補正動作 | `overrun/underrun` が増え続けない、`drift_ins/del` 平均 ±0.64/s 以内 |
| T-5 | #1 リセット/未接続 → #2 クロック喪失ログ、200ms 後 0 返却、復帰で自動再プリフィル | 自動復旧 |
| T-6 | #2 単独起動でも BT ペアリング可 | 可 |
| T-7 | `AUDIO_OUT_MODE=0` で PCM5102A 従来動作 | 従前どおり |
| T-8 | SRL3 との HF 役接続・SCO 確立が CR-011 と同一 | 同一 |

### 12.5 未実施項目
- 実機 T-1〜T-8: 実機(ESP32×2、SENA SRL3、Insta360 X4、配線)が無いため未実施。ロジアナ/オシロでの BCLK・WS・波形確認も含む。
- B-1〜B-6 / S-1〜S-12 / R-1〜R-4: CODER の実装完了連絡後に実施(現時点ではソース未変更のため保留)。
- 設計書 §15.12 未解決事項(U-1〜U-8、既定値承認・ドリフト方式・無音補填方針など)はユーザー判断待ち。実装後、既定値が仮定どおりかを確認する。

### 12.6 ベースライン警告に関する補足
ベースラインのビルドログで "warning" は 0 件(§12.3)。実装後は B-1〜B-5 で同一方法(`idf_py_stdout/stderr` ログ grep)で比較する。

### 12.7 CR-012 実装後テスト結果(DEBUGGER / ビルド・静的検証・回帰)
ビルドはリポジトリを scratchpad にコピー(`build/` 除外)し、設定切替は sed で当該コピー上のみ変更。リポジトリのソース・`build/` は未変更。

| ID | 結果 | 備考 |
|---|---|---|
| B-1 main I2S16K/HF | 合格 | 警告 0、0xb6340(62% 空き。ベースライン 0xb6640 より小) |
| B-2 main I2S16K/AG | 合格 | 警告 0、0xb7430(62% 空き) |
| B-3 main DAC48K/HF | 合格 | 警告 0、0xb66d0(ベースライン 0xb6640 とほぼ同等=旧経路保持) |
| B-4 hf_unit I2S | 合格 | 警告 0、0xadd90(64% 空き)。map に `adc_capture.c.obj` なし(ADC 関連は IDF 内部シンボルのみ)、`i2s_capture.c.obj` あり |
| B-5 hf_unit ADC | 合格 | 警告 0、0xaba20 = ベースラインとバイト一致(旧経路が完全保持) |
| B-6 サイズ | 合格 | 全て 62〜64% 空き |
| S-4 gen_fir 再生成 | 合格 | 出力(×6/×3/×2)の全 352 係数がソース表に存在=既存表の差分なし |
| S-5 ×2 係数 | 合格 | 2 フェーズ×32 tap、各フェーズ DC ゲイン 1.000000(×6/×3 も 1.0) |
| S-1 バッファ | 合格 | `s_up_mono`=64×`I2S_UP_MAX`、`s_stereo` は `I2S_OUT_CHANNELS==2` のみ定義 |
| S-2 ブロッキング | 合格 | `send_cb` は `xRingbufferReceiveUpTo(...,0,...)` のみ(折り返し対応で最大 2 回)。`fill_bytes` は非ブロック |
| S-3 I2S 設定一致 | 合格 | 両側 16kHz/16bit/MONO/Philips(LEFT)、26/25/27、#1 MASTER・#2 SLAVE、MCLK なし |
| S-6 半帯域 FIR | 合格 | タップ和 32(−1+9+16+9−1)=DC ゲイン 1、x[0]=最新で中心 x[-3]、履歴 6 サンプルを `s_ext[n..]` からチャンク跨ぎ保持、`hb_phase` 保持し 2 サンプル毎出力、clip16 あり。レート切替/再接続/クロック復帰で hist・phase リセット |
| S-7 ドリフト補正 | 合格 | 最大 1 サンプル/ブロック、100ms 間隔、bytes/ms×(TARGET/HYST)= 8k: 640/±320、16k: 1280/±640。`s_out[128+1]` で挿入時の境界 OK(m≤128)、`m==1` の削除→0 は `if(m)` で保護 |
| S-6b 無音補填 | 合格 | 取得分コピー+0 埋めで `len` 返却、200ms 超で再プリフィル+0 返却、ADC 時は従来パス完全保持(B-5 サイズ一致で裏付け) |
| S-8 縮退/静的バッファ | 合格 | init 失敗は ERROR ログのみで BT 継続、malloc なし、50ms タイムアウト、200ms で LOST |
| S-9 main I2S | 合格 | `auto_clear`(I2S16K)、DMA 6×64、`i2s_channel_write` 戻り値をレート制限ログ、モノ直書き込み |
| S-10 通知順 | 合格 | `set_rate`(bt_data_path/i2s_capture)→`bt_data_path_reset`→`set_active(true)`→`on_audio_connected`。DISCONNECTED/ACL 切断で `set_active(false)` |
| S-11 CMake/設定 | 合格 | `i2s_capture.c`・`esp_driver_i2s` 追加、`esp_adc`/`adc_capture.c` 維持、`sdkconfig.defaults`・`patches/` 差分なし |
| S-12 文書 | 概ね合格 | README/hf_unit README 更新あり。`docs/wiring_dual_esp32.md`・PDF は未追跡(コミット時に追加要) |
| R-1〜R-3 回帰 | 合格 | `main/{bt_hf_client,bt_hf_ag,bt_data_path,leds,app_main}.c`・`CMakeLists.txt`・`patches/` は差分なし。HF 役は送信タイマー非起動のまま |
| R-4 旧定数 | 合格 | `AUDIO_OUT_MODE=0` で 48000/6/2ch/8/128 を保持(B-3 も成功) |

**不具合**: 合格判定に影響するものなし。
| ID | 内容 | 重大度 |
|---|---|---|
| L-1 | `sdkconfig.old`(ルート/`hf_unit/`)がステージされている。ビルド副産物のためコミット対象外が望ましい(CR-010 L-2 と同様) | Low |
| L-2 | `docs/wiring_dual_esp32.md`・`esp32-esp32-wiring.pdf` が未追跡(コミット時に追加漏れに注意) | Low/情報 |
| L-3 | I2S 版 `send_cb` はプリフィル前に 0 返却するたび `s_underrun++`(従来 ADC 版と同挙動)。SCO 接続直後の LED(SCO_ERR 判定)への影響は実機で確認 | 情報 |

**再テスト**: なし。

### 12.8 最終判定(CR-012、実機なし範囲)
**ビルド 5 通り(B-1〜B-5)・静的検証(S)・回帰(R)は合格**(警告 0、旧経路はバイト一致/定数保持、既存機能・パッチ無変更)。**実機 T-1〜T-8 は未実施**(2×ESP32・SRL3・X4・配線が開発環境に無い)。特に実機で要確認: スロット/エッジ整合(設計 R-1/R-4)、16kHz スレーブ同期、30 分ドリフト補正(T-4)、CVSD 時の音質(半帯域 FIR)、設計 U-1〜U-8 のユーザー判断。

### 12.9 最終ビルド確認(CODER の bt_data_path.c コメント追記後 / ISS-C12-05)
最新の `hf_unit/main/bt_data_path.c` で B-4(I2S)・B-5(ADC)を再ビルド: 両方成功・警告 0。B-4 = 0xadd90(64% 空き)、B-5 = 0xaba20(ベースラインとバイト一致)。B-4 の map に `adc_capture.c.obj` は 0 件(不在確認済み)。§12.1 の「#1 の役」は既定 BT_ROLE_HF に訂正済み(ISS-C12-07)。**最終判定: 実機無し範囲は合格、実機 T-1〜T-8 は未実施(§12.8 参照)。**

### 12.10 CR-013(#2 デジタルゲイン `I2S_IN_GAIN`=4)確認
- ビルド: hf_unit I2S / ADC とも成功・警告 0(ADC 構成は `#if AUDIO_IN_MODE` 外の変更なし)。
- 静的: `s_out[i]` は int16×4 を int32 で計算し `clip16` で飽和クリップ(折り返しなし)。適用位置は間引き/素通し後・ドリフト補正前のため、補正で複製する末尾サンプルもゲイン後の値で一貫(問題なし)。`m==0` の continue より後で範囲外アクセスなし。`I2S_IN_GAIN 1` で無効化可。
- 実機未実施: 4 倍で実音声・ノイズが飽和/歪まないか(SRL3 音声の実レベル)、ノイズフロアの増幅(HPF 30Hz は #1 側のみ)は実機で確認が必要。

### 12.11 CR-013 追補(`I2S_IN_GAIN` 4→8)
定数変更のみ(コード無変更)。int16×8 は int32 に収まり、`clip16` 飽和で折り返しなし(§12.10 の静的確認がそのまま成立)。CODER 側ビルド成功・警告 0。飽和頻度・ノイズ増幅(実質 +18 dB)は実機未確認: 8 倍は通常音声でも 4096 超で頻繁にクリップし得るため、実音声レベルでの歪み確認を推奨。

### 12.12 CR-013 撤去確認
`I2S_IN_GAIN` の定義と `i2s_capture.c` のゲインブロックが撤去され、コード上に参照が残っていないことを grep で確認。CR-012 実装直後の状態(§12.7〜12.9 の結果が有効)。§12.10/12.11 は撤去済み変更の記録。CODER 側ビルド成功・警告 0。
