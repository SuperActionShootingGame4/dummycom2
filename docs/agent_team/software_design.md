# ソフト設計書 — ダミーインカム(受信専用 / アナログ出力型)ファームウェア

- 対象仕様: `dummy_intercom_spec.md` **rev 0.6**(2026-09-02)
- ターゲット: **ESP32-WROOM-32**(Wemos/LOLIN ESP32 mini 形状)
- SDK: **ESP-IDF v5.4**(`~/esp/esp-idf` 導入済み)/ ホスト = **Bluedroid**
- 出力: **PCM5102A I2S DAC → 出力段 → 3.5mm TRS(mono)**
- 作成: DESIGNER / 最終更新: 2026-09-02
- 状態: **初版(設計)/ rev 0.6(ESP32/ESP-IDF)全面再設計**

### 改訂履歴
| rev | 日付 | 内容 |
|---|---|---|
| 1 | 2026-09-02 | 初版(Pico 2 W + BTstack + UAC)。ISS-007(SCO-over-HCI 不具合 #1461)により破棄 |
| 2 | 2026-09-02 | DEBUGGER 申し送り反映(Pico 版) |
| **3** | **2026-09-02** | **プラットフォームを ESP32 + ESP-IDF v5.4 / Bluedroid / アナログ 3.5mm 出力へ全面変更(仕様 rev 0.6、ISS-007 の解決策=案B)。旧 Pico 設計は本ファイルから完全に破棄** |
| 3a | 2026-09-02 | BOSS 補足反映: 公式 `hfp_ag/sdkconfig.ci.vohci` をベースに §5.1 を再構成。`btc_hf_init` が `esp_bredr_sco_datapath_set(HCI)` を実行時呼び出しする事実を §2.7 に追加(controller SCO Kconfig は必須でない)。`BT_HFP_WBS_ENABLE` 既定 y のため明示 OFF が必須と明記。環境有効化コマンドを §9.1 に追加 |
| **4** | **2026-09-02** | **BOSS T02 レビュー(条件付き承認)+ DEBUGGER ビルド検証(ISS-009〜011)反映。§5.1 から廃止 Kconfig `CONFIG_BT_SSP_ENABLED` 削除。パーティションを `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`(built-in)に変更。§11 に BOSS 決定を追記。§8.1 に issue_list 参照(ISS-012〜018)。§2.4 に v6 系との API 差分注記。B8: WBS Phase 2 は esp32 でビルド可** |
| **5** | **2026-09-02** | **T05 実装レビュー。§10 に実装後レビュー結果(条件付き合格 / Medium 1 = BOND-CLR の再起動前 delay)。パーティションをカスタム `partitions.csv`(factory 0x1E0000)に再統一(BOSS 指示、CODER 実装に整合)。§0-D プローズを `≈0x60020C`(Telephony\|Audio)に訂正。§4.2 を実ヘッダのシンボルに整合。§8.5 チェック2 に `esp_hf_ag_outgoing_data_ready()` 例外を追記。§8.5/§9.3 に非同期完了待ち・NVS 縮退の注記** |
| 5a | 2026-09-02 | **T05 二次レビュー(§10.7)。CODER の ISS-019(`HFW_FACTORY_RESET` を BtAppT dispatch + bond 数 0 をポーリング確認 → restart)/ ISS-022(NVS 二重失敗の縮退 `pwr_blink_forever(900)`)修正を確認 → 合格。Critical/High/Medium 0** |
| **6** | **2026-09-07** | **rev 0.7 音質改善(ISS-026)。§3 に `audio_dsp.c/.h` 追加。§5.5 / §6.1 を全面改訂: I2S 48kHz 固定 + `audio_dsp`(120Hz HPF + 下方エキスパンダ型ゲート + ポリフェーズ FIR、CVSD 8k×6 / mSBC 16k×3)。§5.1 `CONFIG_BT_HFP_WBS_ENABLE=y`。`bt_hf_ag.c` が `CONNECTED_MSBC` で 16kHz 経路へ自動切替。ISS-017 Resolved、R3/U-06 対応済み。fullclean ビルド警告 0、app 723KB** |
| **CR-010** | 2026-09-30 | **SENA SRL3 ペアリング対応(§13 追記)**。SSP 無効化は Kconfig ではなく実行時 `esp_bluedroid_config_t.ssp_en=false`(ISS-009 の「SSP 常時有効」は誤り、§13.2)。固定 PIN "0000" レガシーペアリング化、常時 discoverable(SLC 未接続中)、CoD は Phone 維持+切替マクロ。sdkconfig 変更なし |
| **CR-011** | 2026-09-30 | **HF 役追加(§14 追記、設計のみ)**。SRL3 ユニバーサルインターコム対応のため dummycom を HFP-HF 役でも動作可能にする。AG+HF は Kconfig 上コンパイル共存可だが**同時動作不可**(§14.2)→ 起動時に 1 役のみ init。Phase 1=ビルド時マクロ `BT_ROLE`、Phase 2=NVS+ボタン切替。SDP 確認仮説は**未検証** |

---

## 0. エグゼクティブサマリー(BOSS 必読)

| # | 事項 | 重大度 | 結論 |
|---|---|---|---|
| A | **ESP-IDF HFP-AG + HCI データパスの codec ネゴ・ハング履歴**(ISS-008 / #4967 = BT-776) | High(実機依存) | #4967 は **2020-11 に completed でクローズ**(パッチが IDF v4.2 に取り込み済み)。v5.4 は公式 `hfp_ag/sdkconfig.ci.vohci` が CI で回っており HCI データパスは動作する。**要点は `CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y`(これで `btc_hf_init` が実行時に `esp_bredr_sco_datapath_set(HCI)` を呼ぶ)+ `MAX_SYNC_CONN=1` + 本機は `WBS_ENABLE=n`(CVSD 固定)**。§5.1 参照。**T3(SCO 確立)を最優先の実機検証項目**とする。派生不具合(#6218 の送信バッファサイズ、forum #28057 の送信音切れ)は send コールバックの `sz` 厳守で回避。 |
| B | 実機(ESP32 / SRL-3)が開発環境に無い(ISS-002 継続) | Medium | コンパイル/静的検証まで実施。T1〜T13 は手順書化し「未実施」で明示。 |
| C | `AUDIO_STATE_EVT` に `preferred_frame_size` は **v5.4 の API に存在しない**(旧 §5.3-1 の記述と相違) | Low(**解決**) | `struct hf_audio_stat_param` は `remote_addr` / `state` / `sync_conn_handle` のみ。送信最適化は send コールバックの `sz` 引数で。**仕様 rev 0.6 は BOSS が当該記述を削除済み**。 |
| D | CoD `0x20040C` はビット的に major=A/V, service=Audio になり「Phone/Telephony」表記と不一致 | Low(**解決**) | intent どおり major=Phone(0x02) / minor=Smartphone(0x03) / service=Telephony\|Audio(0x300)を設定 → **実効 ≈ `0x60020C`**(§4.1 コード = `ESP_BT_COD_SRVC_TELEPHONY \| ESP_BT_COD_SRVC_AUDIO`、実装もこれ)。HFP-AG は Audio + Telephony 両サービスを提供するので両ビット妥当。SLC 不成立時に literal `0x20040C` へ切替(U-05)。 |
| E | 内蔵 8bit DAC(§4.2 代替)は v5.4 で I2S 直結不可(`dac_continuous` ドライバ別系統) | Low | Phase 1 は PCM5102A(外部 I2S DAC)前提で設計。内蔵 DAC 対応は将来 §13(U-07)。 |
| F | `CONFIG_BT_SSP_ENABLED` は v5.4 で廃止(ISS-009、DEBUGGER 検証) | Low(**解決**) | §5.1 から SSP 行削除。IO cap は `esp_bt_gap_set_security_param` で(§5.7)。 |
| G | パーティション: rev.4 で SINGLE_APP_LARGE としたが、CODER はカスタム `partitions.csv`(factory 0x1E0000)で実装・ビルド検証。BOSS が仕様を「カスタム」に再統一 | Low(**解決**) | rev.5 でカスタム `partitions.csv` に統一(§3/§5.1)。factory 0x1E0000 に app 719KB で 63% 空き(ビルド実測)。 |

**CODER 着手可否**: **可**。ゴール = 「`idf.py build` 成功 + 静的検証 + 構造の正しさ」。実機音声疎通(T3/T4)はユーザー実機テストに委譲。

---

## 1. 変更要求(要求仕様 rev 0.6 の整理)

新規開発。既存コード無し。DESIGNER が抽出した必達要件:

| ID | 要件 | 出典 | 実装方針 |
|---|---|---|---|
| RQ-01 | HFP **Audio Gateway (AG)** ロールで SRL-3(HF)と SLC 確立 | §5 | Bluedroid `esp_hf_ag`、`examples/.../classic_bt/hfp_ag` ベース |
| RQ-02 | SCO(**CVSD 8kHz** または **mSBC 16kHz**、16bit/mono)を **HCI データパス**で受信 → デコード済み PCM 取得 | §5, §5.1, §6 | `esp_hf_ag_register_data_callback(recv, send)`。rev 0.7: WBS 有効、コーデックで 8k/16k を判別 |
| RQ-03 | **リサンプルなし**で 8kHz PCM を I2S0 master TX(8kHz/16bit、L=R mono→stereo)へ | §6, §6.1 | 新 I2S ドライバ `driver/i2s_std.h`。PCM5102A へ |
| RQ-04 | ジッタバッファ 40–100ms 可変(初期 60ms) | §6 | FreeRTOS `RingbufHandle_t`(`RINGBUF_TYPE_BYTEBUF`)+ プリフィル |
| RQ-05 | 上り SCO には**無音(ゼロ)PCM** を送出 | §5.3-4 | send コールバックで `memset(0)` + 周期 `esp_hf_ag_outgoing_data_ready()` |
| RQ-06 | SLC 確立後 **3 秒間隔で SCO 維持**、無ければ `esp_hf_ag_audio_connect()` | §5.3-3 | `esp_timer` 周期タイマ |
| RQ-07 | 発呼系 AT(`ATD`/`AT+BLDN`=`ESP_HF_DIAL_EVT` / `AT+CKPD`)受信時は**通話中を偽装** | §5.3-2 | §6.4 の API シーケンス |
| RQ-08 | ACL 切断時、NVS のピア BDA へ **5 秒周期で `esp_hf_ag_slc_connect()`** | §5.3-5 | `esp_timer` 周期タイマ |
| RQ-09 | ボンディング(Bluedroid 自動 NVS)+ **ピア BDA を明示的に NVS 保存**。対象 1 台 | §5 | `nvs` API。`ESP_BT_GAP_AUTH_CMPL_EVT` で保存 |
| RQ-10 | GAP: 名前 `DummyIntercom` / CoD / SSP Just Works / IO cap = `ESP_BT_IO_CAP_NONE` | §5 | `esp_bt_gap_*` |
| RQ-11 | 初回ボンドまで discoverable、以後 connectable のみ | §8.1 | `esp_bt_gap_set_scan_mode()` をボンド有無で切替 |
| RQ-12 | SLC 応答: service=1 / signal=5 / roam=0 / battchg=5、未知 AT は `OK`(ERROR ではなく) | §5.2 | `esp_hf_ag_cind_response` 等 + `esp_hf_ag_unknown_at_send` |
| RQ-13 | 受信 AT / RMS / codec / SCO 状態をシリアルログ | §5.4 | `ESP_LOGI`。RMS は recv コールバックで累積し 1s ごと出力 |
| RQ-14 | LED ×3(PWR/LINK/SCO)、GPIO32/33/13 | §4.3, §7.3 | GPIO 出力 + 状態機械 |
| RQ-15 | BOND-CLR ボタン(GPIO14)3 秒長押し → NVS 消去 → `esp_restart()` | §4.3, §8.3 | ボタン監視タスク or 周期タイマ |
| RQ-16 | SCO 未確立時の I2S 出力は無音(ゼロ) | §6.1 | I2S TX タスクが無音フレームを出力 |
| RQ-17 | E2E 遅延 目標 < 100ms、可能なら < 60ms。遅延一定 | §6 | ジッタバッファ 60ms(短縮時 40ms)。負帰還なし・固定オフセット |
| RQ-18 | BLE 無効化(`BR_EDR_ONLY`) | §5 | sdkconfig |

### やらないこと(スコープ外)
上り音声(無音のみ)/ 電話発着信の実処理 / A2DP / AVRCP / USB / Wi-Fi / カメラへの BT マイク送信。
(WBS・mSBC は rev 0.7 で対応済み。)

---

## 2. 現行ソフト解析結果(参照実装)

新規開発のため「現行ソフト」は無い。以下は **ESP-IDF v5.4 同梱の流用元コードの解析結果**(`~/esp/esp-idf`)。

### 2.1 `examples/bluetooth/bluedroid/classic_bt/hfp_ag/main/main.c`

起動シーケンス:
```
nvs_flash_init()  (NVS_NO_FREE_PAGES なら erase→再 init)
esp_bt_controller_mem_release(ESP_BT_MODE_BLE)      // BLE 分の RAM 解放
esp_bt_controller_init(BT_CONTROLLER_INIT_CONFIG_DEFAULT())
esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)
esp_bluedroid_init_with_cfg(BT_BLUEDROID_INIT_CONFIG_DEFAULT())
esp_bluedroid_enable()
bt_app_task_start_up()                              // FreeRTOS ワークキュータスク(BtAppT)
bt_app_work_dispatch(bt_hf_hdl_stack_evt, BT_APP_EVT_STACK_UP, ...)
  → bt_hf_hdl_stack_evt():
       esp_bt_gap_set_device_name(name)
       esp_hf_ag_register_callback(bt_app_hf_cb)
       esp_hf_ag_init()
       esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 4, "0000")
       esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE)
(以降 console REPL。本機では不要 → 削除)
```
- **`bt_app_task` / `bt_app_work_dispatch`**: BT スタックのコールバック文脈から重い処理を切り離すためのワークキュー。**本機でも流用**(GAP/HF コールバック → dispatch → `BtAppT` タスクで実処理)。
- console REPL(`esp_console_*`, `app_hf_msg_set.c`)は**削除**(単体アプライアンス)。
- `gpio_pcm_config.c` は PCM(I2S 経由の SCO ハード配線)用。本機は **HCI データパス**なので不要。

### 2.2 `main/bt_app_hf.c`(HF コールバック)

- `bt_app_hf_cb(event, param)` が全 `ESP_HF_*_EVT` を処理。イベント名テーブル `c_hf_evt_str[]`。
- **HCI データパス部(`#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI`)**:
  - `bt_app_hf_incoming_cb(const uint8_t *buf, uint32_t sz)`: **受信 PCM コールバック**。例では速度計測のみ。**本機はここで `audio_i2s_write(buf, sz)` + RMS**。
  - `bt_app_hf_outgoing_cb(uint8_t *p_buf, uint32_t sz)`: **送信 PCM コールバック**。例はリングバッファから読む。戻り値 = 実際に書いたバイト数、不足なら 0。**本機は `memset(p_buf,0,sz); return sz;`(無音)**。
  - `bt_app_send_data()`: `esp_timer`(4ms 周期)+ セマフォ + タスクで、`esp_hf_ag_outgoing_data_ready()` を「リングバッファに 1 フレーム以上あるとき」呼ぶ。**本機は無音送出なので簡略化**(§6.5)。
  - 定数: `ESP_HFP_RINGBUF_SIZE 3600`、`PCM_BLOCK_DURATION_US 7500`(= eSCO の Tesco に整合)、`PCM_SAMPLING_RATE_KHZ 8`、`BYTES_PER_SAMPLE 2`、`PCM_INPUT_DATA_SIZE = 8*7500/1000*2 = 120`。
  - `esp_hf_ag_register_data_callback(recv, send)` は **`ESP_HF_AUDIO_STATE_EVT` = connected のとき呼んでいる**(接続ごと)。→ 本機は **init 時に 1 回登録**に変更(API doc 上 `esp_bluedroid_enable()` 後ならいつでも可)。ハングするようなら例と同じ「接続時登録」に戻す(U-02)。
- **`ESP_HF_AUDIO_STATE_EVT`**: `param->audio_stat.state` は `ESP_HF_AUDIO_STATE_{DISCONNECTED,CONNECTING,CONNECTED,CONNECTED_MSBC}`。`sync_conn_handle` も取得可。
- **`ESP_HF_CIND_RESPONSE_EVT`**: `esp_hf_ag_cind_response(addr, call, callsetup, ntk_state, signal, roam, batt, callheld)` で応答。
- **`ESP_HF_IND_UPDATE_EVT`**: `esp_hf_ag_ciev_report(addr, ESP_HF_IND_TYPE_*, value)` を各 indicator について送る。
- **`ESP_HF_COPS_RESPONSE_EVT`**: `esp_hf_ag_cops_response(addr, name)`。
- **`ESP_HF_CLCC_RESPONSE_EVT`**: `esp_hf_ag_clcc_response(addr, index, dir, status, mode, mpty, number, type)`。**index=0 で終端(OK)**。通話偽装時は index=1 の 1 件 + index=0、非通話時は index=0 のみ。
- **`ESP_HF_CNUM_RESPONSE_EVT`**: `esp_hf_ag_cnum_response(addr, number, number_type, service_type)`。
- **`ESP_HF_UNAT_RESPONSE_EVT`**: `param->unat_rep.unat` に AT 文字列。例は `esp_hf_ag_unknown_at_send(addr, NULL)` = **ERROR を返す**。**本機は §5.2 に従い、CKPD 等は解釈しつつ基本 `OK`**(`esp_hf_ag_unknown_at_send(addr, "")` or 個別対応。§6.4)。
- **`ESP_HF_DIAL_EVT`**: `param->out_call.{type, num_or_loc, remote_addr}`。例:`esp_hf_ag_cmee_send(addr, ESP_HF_AT_RESPONSE_CODE_OK, ...)` → `esp_hf_ag_out_call(addr, 1,0, 1,0, num, 0)`。
- **`ESP_HF_BCS_RESPONSE_EVT`**: `param->bcs_rep.mode` = 最終 codec(`c_codec_mode_str[]`: 0=CVSD only, 1=Use CVSD, 2=Use MSBC)。**mSBC が来たらログ WARN**(WBS 無効なので通常来ない)。
- **`ESP_HF_PROF_STATE_EVT`**: `param->prof_stat.state` = `ESP_HF_INIT_SUCCESS` / `ESP_HF_DEINIT_SUCCESS`。init 完了同期に使える。

### 2.3 `main/bt_app_core.c`

- `bt_app_work_dispatch(cb, event, params, len, copy_cb)` → `xQueueSend` → `BtAppT` タスク(`configMAX_PRIORITIES-3`, stack 2048)が `cb(event, param)` を実行、`param` を `free`。
- **本機で流用**。GAP/HF コールバックは軽量処理のみ行い、実処理は dispatch する。

### 2.4 `esp_hf_ag_api.h`(v5.4 の実 API)— 確定シグネチャ

| API | 用途 |
|---|---|
| `esp_hf_ag_register_callback(esp_hf_cb_t)` | HF イベントコールバック登録 |
| `esp_hf_ag_init(void)` / `esp_hf_ag_deinit(void)` | プロファイル init/deinit(`ESP_HF_PROF_STATE_EVT` で完了通知) |
| `esp_hf_ag_slc_connect(esp_bd_addr_t)` / `esp_hf_ag_slc_disconnect(...)` | SLC 確立/切断 |
| `esp_hf_ag_audio_connect(esp_bd_addr_t)` / `esp_hf_ag_audio_disconnect(...)` | SCO 確立/切断(SLC 必須) |
| `esp_hf_ag_cind_response(addr, call, callsetup, ntk, signal, roam, batt, callheld)` | AT+CIND=? / AT+CIND? 応答 |
| `esp_hf_ag_cops_response(addr, char *name)` | AT+COPS 応答 |
| `esp_hf_ag_clcc_response(addr, int index, dir, status, mode, mpty, char *number, type)` | AT+CLCC 応答(index=0 で終端) |
| `esp_hf_ag_cnum_response(addr, char *number, int number_type, service_type)` | AT+CNUM 応答 |
| `esp_hf_ag_ciev_report(addr, esp_hf_ciev_report_type_t ind_type, int value)` | +CIEV 単発通知(推奨。`devices_status_indchange` は deprecated) |
| `esp_hf_ag_cmee_send(addr, esp_hf_at_response_code_t, esp_hf_cme_err_t)` | +CME ERROR / OK |
| `esp_hf_ag_unknown_at_send(addr, char *unat)` | 未知 AT 応答(NULL で "ERROR"、`""` で "OK" 相当) |
| `esp_hf_ag_out_call(addr, num_active, num_held, call_state, call_setup_state, char *number, type)` | AG 発呼(HF 駆動時は OK/ERROR 応答が必要) |
| `esp_hf_ag_answer_call(...)` / `esp_hf_ag_reject_call(...)` / `esp_hf_ag_end_call(...)` | 着信応答/拒否/終話(call state 同期) |
| `esp_hf_ag_bsir(addr, state)` | in-band ring tone 有無通知 |
| `esp_hf_ag_register_data_callback(recv, send)` | **Voice over HCI 用**。recv/send コールバック登録 |
| `esp_hf_ag_outgoing_data_ready(void)` | 下位層に送信データ取得を促す(send コールバックが呼ばれる) |

コールバック型:
```c
typedef void     (*esp_hf_incoming_data_cb_t)(const uint8_t *buf, uint32_t len);  // buf はスタック内、戻ると解放
typedef uint32_t (*esp_hf_outgoing_data_cb_t)(uint8_t *buf, uint32_t len);        // 非ブロッキング。書けた分を返す。0 可
```
- **recv `buf` はコールバック終了で解放** → **必ずコピー**。
- **send は非ブロッキング必須**。データが無ければ 0 を返す(ただし本機は常に無音 `len` を返す)。
- コールバック文脈 = Bluedroid **BTC タスク**。ブロック・重処理厳禁。
- **API 名・シグネチャは v5.4 実ヘッダで確定済み(N5)**: `esp_hf_ag_register_callback` / `esp_hf_ag_init` / `esp_hf_ag_slc_connect` / `esp_hf_ag_audio_connect` / `esp_hf_ag_register_data_callback` / `esp_hf_ag_outgoing_data_ready` / `esp_hf_ag_cind_response` / `esp_hf_ag_ciev_report` / `esp_hf_ag_cops_response` / `esp_hf_ag_clcc_response` / `esp_hf_ag_cnum_response` / `esp_hf_ag_cmee_send` / `esp_hf_ag_unknown_at_send` / `esp_hf_ag_out_call` / `esp_hf_ag_answer_call` / `esp_hf_ag_end_call` — 全て存在。
- **v6 系(将来の ESP-IDF)とは recv/send コールバックのシグネチャが異なる**。本設計は **v5.4 系の上記型**を前提。IDF をアップグレードする場合は `esp_hf_ag_api.h` を再確認して本節を更新すること。

`AUDIO_STATE_EVT` param 構造体(v5.4):
```c
struct hf_audio_stat_param { esp_bd_addr_t remote_addr; esp_hf_audio_state_t state; uint16_t sync_conn_handle; };
// ※ preferred_frame_size は存在しない(§0-C)
```

### 2.5 `esp_gap_bt_api.h` — GAP

| API | 用途 |
|---|---|
| `esp_bt_gap_register_callback(esp_bt_gap_cb_t)` | GAP イベント |
| `esp_bt_gap_set_device_name(const char *)` | デバイス名 |
| `esp_bt_gap_set_cod(esp_bt_cod_t, esp_bt_cod_mode_t)` | CoD 設定(`ESP_BT_SET_COD_ALL`) |
| `esp_bt_gap_set_scan_mode(esp_bt_connection_mode_t, esp_bt_discovery_mode_t)` | `{ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE / ESP_BT_NON_DISCOVERABLE}` |
| `esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, 1)` | `iocap = ESP_BT_IO_CAP_NONE`(Just Works) |
| `esp_bt_gap_ssp_confirm_reply(addr, true)` | `ESP_BT_GAP_CFM_REQ_EVT` 自動許可 |
| `esp_bt_gap_remove_bond_device(addr)` / `esp_bt_gap_get_bond_device_list(&n, list)` / `esp_bt_gap_get_bond_device_num()` | ボンド管理(BOND-CLR) |

`esp_bt_cod_t` = ビットフィールド `{reserved_2:2, minor:6, major:5, service:11, reserved_8:8}`。
GAP イベント: `ESP_BT_GAP_AUTH_CMPL_EVT`(`param->auth_cmpl.{bda, stat, device_name}`)、`ESP_BT_GAP_PIN_REQ_EVT`、`ESP_BT_GAP_CFM_REQ_EVT`、`ESP_BT_GAP_KEY_NOTIF_EVT` 等。

### 2.6 `driver/i2s_std.h` / `driver/i2s_common.h` — 新 I2S ドライバ(v5.x)

```c
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);          // TX のみ(rx=NULL)
i2s_std_config_t std_cfg = {
  .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(8000),         // {sample_rate_hz, I2S_CLK_SRC_DEFAULT, MCLK x256}
  .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
  .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = 26, .ws = 25, .dout = 27, .din = I2S_GPIO_UNUSED,
                .invert_flags = {0,0,0} },
};
i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
i2s_channel_enable(s_tx_chan);
esp_err_t i2s_channel_write(i2s_chan_handle_t, const void *src, size_t size, size_t *written, uint32_t timeout_ms);
```
- DMA バッファ本数/長は `chan_cfg.dma_desc_num` / `dma_frame_num`(`I2S_CHANNEL_DEFAULT_CONFIG` は 6 / 240)。本機は `dma_desc_num=8`, `dma_frame_num=128`(= 128 frame × 8 = ~128ms 相当の DMA)程度に調整。
- PCM5102A: SCK=GND(内部 PLL)。8kHz 対応(PCM5102A は 8k–384k)。MCLK 不要。
- 出力は **stereo フレーム**(L/R とも同一 mono 値)。PCM5102A は mono 専用モード無し。

### 2.7 ISS-008(#4967 / BT-776)+ 公式 vohci リファレンス

- **#4967 は 2020-11-03 に `completed` でクローズ**。`boblane1` のパッチが IDF **v4.2** に取り込まれ、以後の IDF では例が HCI データパスで動作する(patch 不要)。
- **公式 Voice-over-HCI リファレンス = `examples/.../hfp_ag/sdkconfig.ci.vohci`**(BOSS 指摘)。実内容:
  ```
  CONFIG_BT_ENABLED=y
  CONFIG_BT_BLE_ENABLED=n
  CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y
  CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1
  CONFIG_BT_BLUEDROID_ENABLED=y
  CONFIG_BT_CLASSIC_ENABLED=y
  CONFIG_BT_HFP_ENABLE=y
  CONFIG_BT_HFP_AG_ENABLE=y
  CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y
  ```
  → **controller の SCO データパス Kconfig は設定していない**。理由: `esp_hf_ag_init()` → `btc_hf_init()`(`components/bt/host/bluedroid/btc/profile/std/hf_ag/btc_hf_ag.c:361`)が
  `BTM_SCO_HCI_INCLUDED`(= `BT_HFP_AUDIO_DATA_PATH_HCI=y` で有効)なら **`esp_bredr_sco_datapath_set(ESP_SCO_DATA_PATH_HCI)` を実行時に呼ぶ** → controller のデフォルト(PCM)を上書きする。
  → **`CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y` だけで SCO は HCI に載る**。
- **`CONFIG_BT_HFP_WBS_ENABLE` は `BT_HFP_AUDIO_DATA_PATH_HCI=y` のとき既定 `y`**(`components/bt/host/bluedroid/Kconfig.in:166`)。vohci CI 構成は WBS を無効化していない=CI は WBS 有効で回している。
  → **本機は仕様 §5.1(CVSD 固定)に従い `CONFIG_BT_HFP_WBS_ENABLE=n` を明示的に上書きする**(vohci リファレンスとの意図的な差分)。これで AG は mSBC を提示せず SRL-3 は CVSD で確立。
- 現代の IDF(v5.4)で HCI SCO を通す設定(vohci + 本機の CVSD 固定要件):
  | # | Kconfig | 必須度 | 備考 |
  |---|---|---|---|
  | 1 | `CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y` | **必須** | これだけで `btc_hf_init` が SCO を HCI に切替 |
  | 2 | `CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1` | **必須** | 0 だと SCO 不可。vohci も 1 |
  | 3 | `CONFIG_BT_HFP_WBS_ENABLE=n` | **必須(本機)** | CVSD 固定(仕様)。vohci は未設定=既定 y |
  | 4 | `CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y` | 推奨(必須でない) | 実行時上書きされるが intent を明示 + init 前の窓を塞ぐ |
  | 5 | `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y` / `CONFIG_BT_BLE_ENABLED=n` | 必須 | BLE 無効(仕様 §5) |
  | 6 | `CONFIG_BT_A2DP_ENABLE=n` | 推奨 | 不要機能・RAM 節約 |
- 残存する派生問題と本機での回避:
  - **#6218**: send コールバックに渡る `sz` より確保バッファが小さいことがある/`sz` 未満しか書かないと不整合。→ **本機は `memcpy` せず `memset(buf, 0, sz); return sz;`** なので影響なし。
  - **forum #28057**: WBS + 送信データ不足で `bta_ag_sco_read_cback: status(2)` 連発・音切れ。→ 本機は **CVSD 固定 + 無音を常時 `sz` バイト供給** で回避。`status(2)` の warning が出ても受信側(下り)には影響しないことを確認する。
  - **#6503**: audio_connect 後しばらくして SCO data path が開けなくなる。→ **T3/T6 で 30 分連続の SCO 安定性を重点検証**。詰まったら keepalive/reconnect で復旧。
- **結論**: v5.4 + 上表の設定で「動くはず」(公式 vohci CI が回っている)。ただし SRL-3 実機での確証は取れない → **ISS-008 は Open のまま、T3 を最優先実機検証**。

---

## 3. 関連モジュール / ファイル構成(確定)

```
android_BT_TEST/
├─ CMakeLists.txt                # プロジェクト(idf 標準)
├─ sdkconfig.defaults            # §5.1 の設定を全て記述
├─ partitions.csv                # nvs 0x6000 / phy_init 0x1000 / factory 0x1E0000(4MB flash)
├─ README.md                     # ビルド/書き込み/配線手順
└─ main/
   ├─ CMakeLists.txt             # idf_component_register(SRCS ... REQUIRES ...)
   ├─ app_config.h               # 全定数(GPIO, CoD, 名前, バッファ, タイマ周期, NVS キー)
   ├─ app_main.c                 # app_main(): NVS→BT controller(BR/EDR only)→Bluedroid→
   │                             #   GAP/HF コールバック登録→esp_hf_ag_init→GAP 設定→
   │                             #   audio_i2s_init / bt_data_path_init / leds_init / storage_init
   ├─ bt_hf_ag.c / .h            # GAP + HF イベント処理、通話偽装、SCO keepalive / 再接続タイマ、
   │                             #   discoverable 制御、UNAT ログ、bt_app ワークキュー流用
   ├─ bt_data_path.c / .h        # recv/send コールバック、RMS、outgoing_data_ready 周期タイマ、
   │                             #   audio_i2s へのブリッジ
   ├─ audio_i2s.c / .h           # I2S0 master TX @ 48kHz 固定、ジッタバッファ(RingbufHandle_t)、
   │                             #   audio_dsp 呼出、mono→stereo、プリフィル、SCO 断時ゼロ出力、TX タスク
   ├─ audio_dsp.c / .h           # rev 0.7: 120Hz HPF + ノイズゲート + ポリフェーズ FIR アップサンプル
   │                             #   (CVSD 8k×6 / mSBC 16k×3 → 48k)。係数生成 = tools/gen_fir.py
   ├─ storage.c / .h             # NVS ピア BDA 保存/読出/消去、ボンド全消去
   └─ leds.c / .h                # PWR/LINK/SCO LED 状態機械、BOND-CLR ボタン監視
```

### 3.1 タスク / 実行文脈

| タスク/文脈 | 生成 | 役割 | 優先度(目安) |
|---|---|---|---|
| `app_main`(→ 終了) | IDF | 初期化のみ。戻ってよい(FreeRTOS 継続) | - |
| Bluedroid BTC / BTU タスク | IDF/Bluedroid | HF/GAP コールバック、**recv/send データコールバック** | 高(IDF 既定) |
| `BtAppT`(bt_app_core) | `bt_app_task_start_up()` | dispatch された GAP/HF 実処理、`esp_hf_ag_*` API 呼び出し | `configMAX_PRIORITIES-3` |
| `i2s_tx`(audio_i2s) | `audio_i2s_init()` | ジッタバッファ → mono→stereo → `i2s_channel_write` | `configMAX_PRIORITIES-4`(BtAppT より低、但し十分高く) |
| `leds`(leds) | `leds_init()` | 50ms 周期で LED 点滅生成 + ボタン監視 | 低(5) |
| `esp_timer` コールバック | IDF | SCO keepalive(3s)、再接続(5s)、outgoing_data_ready(7.5ms)、RMS ログ(1s) | esp_timer タスク文脈 |

- **recv コールバック(BTC 文脈)は `xRingbufferSend(rb, buf, len, 0)` のみ**(ブロックなし)。重い処理・ログ・`i2s_channel_write` を呼ばない。
- **`esp_hf_ag_*` API はコールバック文脈から直接呼ばない**方針 → `bt_app_work_dispatch` 経由で `BtAppT` から呼ぶ(例外: `esp_hf_ag_cind_response` 等の即応答系はコールバック内で可。例がそうしている。**タイマ発火の `audio_connect`/`slc_connect` は `BtAppT` へ dispatch**)。
- `esp_timer` コールバックも短時間限定。`audio_connect` 等は dispatch する。

---

## 4. 処理フロー(新規=設計フロー)

### 4.1 起動シーケンス(`app_main`)

```
1. esp_err_t r = nvs_flash_init();
   if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
2. leds_init();                       // GPIO out 32/33/13, in 14(pullup)。PWR 点灯
3. storage_init();                    // NVS namespace open、peer BDA 読出(有無フラグ)
4. audio_i2s_init();                  // I2S0 TX + ジッタバッファ + i2s_tx タスク。初期=無音出力
5. ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
6. esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
   esp_bt_controller_init(&cfg);
   esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
7. esp_bluedroid_config_t bcfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
   esp_bluedroid_init_with_cfg(&bcfg);
   esp_bluedroid_enable();
8. bt_app_task_start_up();            // BtAppT
9. bt_hf_ag_start();                  // 内部で:
     esp_bt_gap_register_callback(bt_gap_cb);
     esp_hf_ag_register_callback(bt_hf_cb);
     esp_hf_ag_init();
     esp_timer_create(keepalive / reconnect / dial / rms);   // start はしない
     bt_data_path_init();             // esp_hf_ag_register_data_callback(recv_cb, send_cb) + tx_ready_timer create
     bt_app_work_dispatch(bt_hf_stack_up, 0, NULL, 0, NULL);
      → bt_hf_stack_up() (BtAppT 文脈):
          esp_bt_gap_set_device_name("DummyIntercom");
          esp_bt_cod_t cod = { .major = ESP_BT_COD_MAJOR_DEV_PHONE, .minor = 0x03 /*Smartphone*/,
                               .service = ESP_BT_COD_SRVC_TELEPHONY | ESP_BT_COD_SRVC_AUDIO };
          esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);  // 実効 CoD ≈ 0x60020C(§0-D / U-05)。
                                                        // SLC 不成立時は literal 0x20040C へ切替

          esp_bt_io_cap_t io = ESP_BT_IO_CAP_NONE;   // NoInputNoOutput = Just Works
          esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io, sizeof(io));
          // ↑ v5.4 実シグネチャは 3 引数: (esp_bt_sp_param_t, void *value, uint8_t len)。
          //   `&io, sizeof(io)`(len=1)が正。`CONFIG_BT_SSP_ENABLED` は書かない(ISS-009、v5.4 で廃止・常時有効)
          bt_hf_ag_apply_scan_mode();   // storage_has_peer() ? {CONN, NON_DISC} : {CONN, GEN_DISC}
          if (storage_has_peer()) bt_hf_ag_start_reconnect_timer();   // 即再接続開始
11. (app_main 戻る)
```

### 4.2 SLC / SCO シーケンス(`bt_hf_cb` → dispatch → `BtAppT`)

```
ESP_HF_PROF_STATE_EVT (INIT_SUCCESS)     → ログ「HFP-AG ready」
ESP_HF_CONNECTION_STATE_EVT:
  state==CONNECTING                      → LINK 点滅
  state==SLC_CONNECTED                   → s_peer=remote_bda; storage_save_peer(remote_bda);
                                            bt_hf_ag_apply_scan_mode()  // discoverable off
                                            LINK 点灯; stop_reconnect_timer(); start_sco_keepalive_timer();
                                            dispatch: esp_hf_ag_audio_connect(s_peer)   // 即 SCO
  state==DISCONNECTED                    → s_acl=false; LINK 点滅; stop_sco_keepalive_timer();
                                            audio_i2s_set_sco_active(false);
                                            if (storage_has_peer()) start_reconnect_timer();
ESP_HF_CIND_RESPONSE_EVT                 → esp_hf_ag_cind_response(addr, s_call, s_callsetup, 1/*svc*/, 5/*sig*/, 0/*roam*/, 5/*batt*/, 0/*held*/)
ESP_HF_COPS_RESPONSE_EVT                 → esp_hf_ag_cops_response(addr, "DummyNet")
ESP_HF_CLCC_RESPONSE_EVT                 → if (s_call) esp_hf_ag_clcc_response(addr, 1, ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING, ESP_HF_CURRENT_CALL_STATUS_ACTIVE, ESP_HF_CURRENT_CALL_MODE_VOICE, ESP_HF_CURRENT_CALL_MPTY_TYPE_SINGLE, "", ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
                                            esp_hf_ag_clcc_response(addr, 0, ...);  // index=0 で終端 OK
ESP_HF_CNUM_RESPONSE_EVT                 → esp_hf_ag_cnum_response(addr, "", 129, ESP_HF_SUBSCRIBER_SERVICE_TYPE_UNKNOWN)
ESP_HF_IND_UPDATE_EVT                    → ciev_report: SERVICE=1, SIGNAL=5, ROAM=0, BATTCHG=5, CALL=s_call, CALLSETUP=s_callsetup
ESP_HF_UNAT_RESPONSE_EVT                 → ログ; unknown_at_send(addr, "")=OK; strstr(unat,"CKPD") かつ SLC && !SCO なら audio_connect
ESP_HF_DIAL_EVT                          → §6.4 通話偽装
ESP_HF_ATA_RESPONSE_EVT                 → ログ; s_call=1; s_callsetup=0; esp_hf_ag_answer_call(addr,1,0,ESP_HF_CALL_STATUS_CALL_IN_PROGRESS,ESP_HF_CALL_SETUP_STATUS_IDLE,"",ESP_HF_CALL_ADDR_TYPE_UNKNOWN)
ESP_HF_CHUP_RESPONSE_EVT                → ログ; s_call=0; s_callsetup=0; esp_hf_ag_end_call(addr,0,0,ESP_HF_CALL_STATUS_NO_CALLS,ESP_HF_CALL_SETUP_STATUS_IDLE,"",ESP_HF_CALL_ADDR_TYPE_UNKNOWN)
                                            // SCO は keepalive が再確立する
ESP_HF_VOLUME_CONTROL_EVT               → ログのみ(出力レベルはハードの半固定 VR)
ESP_HF_BCS_RESPONSE_EVT                 → ログ; if (param->bcs_rep.mode == ESP_HF_WBS_YES) LOGW("mSBC negotiated unexpectedly")
ESP_HF_WBS_RESPONSE_EVT / BVRA / NREC / VTS → ログのみ
ESP_HF_AUDIO_STATE_EVT:
  state==CONNECTING                      → SCO 点滅
  state==CONNECTED                       → s_sco_handle=sync_conn_handle; SCO 点灯;
                                            audio_i2s_reset(); audio_i2s_set_sco_active(true);
                                            bt_data_path_on_audio_connected();  // outgoing_data_ready 周期タイマ開始
  state==CONNECTED_MSBC                  → LOGW; 上と同様だが本来来ない
  state==DISCONNECTED                    → s_sco_handle=0; SCO 消灯;
                                            audio_i2s_set_sco_active(false);
                                            bt_data_path_on_audio_disconnected();
ESP_HF_BVRA_RESPONSE_EVT / NREC / VTS   → ログのみ
```

### 4.3 音声データフロー(定常)

```
[SRL-3] --SCO CVSD 8kHz--> [ESP32 BT controller] --vHCI--> Bluedroid (CVSD decode)
  → recv_cb(buf, len)  [BTC タスク文脈]
      RMS 累積; xRingbufferSend(s_jitter_rb, buf, len, 0)   // 満杯なら古い側を捨てて再試行 or drop+counter
                                   │
                s_jitter_rb (RINGBUF_TYPE_BYTEBUF, 4096B ≈ 256ms, 目標 60ms=960B)
                                   │
  i2s_tx タスク: ループ
      if (!sco_active || !prefilled)  → i2s_channel_write(無音 stereo フレーム)
      else:
        data = xRingbufferReceiveUpTo(s_jitter_rb, &got, pdMS_TO_TICKS(10), CHUNK_MONO_BYTES)
        mono→stereo 展開(L=R): out_stereo[2i]=out_stereo[2i+1]=mono[i]
        i2s_channel_write(s_tx_chan, out_stereo, got*2, &written, portMAX_DELAY(or 100ms))
        vRingbufferReturnItem(...)
        不足時は無音パディング + underrun カウンタ + prefilled=false
  → I2S0 → PCM5102A → 出力段(L/R 加算, VR, 絶縁トランス) → J1
```

上り(無音): `esp_timer`(7.5ms 周期、SCO 中のみ)→ `esp_hf_ag_outgoing_data_ready()` → Bluedroid が `send_cb(buf, sz)` を呼ぶ → `memset(buf,0,sz); return sz;`。

---

## 5. 詳細設計

### 5.1 `sdkconfig.defaults`(ISS-008 対策の中核)

**ベース = 公式 `hfp_ag/sdkconfig.ci.vohci`(§2.7)**。本機はそれに CVSD 固定・省機能・NVS/パーティションを追加。

```
# ==== ベース: 公式 vohci 構成(examples/.../hfp_ag/sdkconfig.ci.vohci)====
CONFIG_BT_ENABLED=y
CONFIG_BT_BLE_ENABLED=n
CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y
CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_HFP_ENABLE=y
CONFIG_BT_HFP_AG_ENABLE=y
CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y

# ==== 本機の追加/上書き ====
# rev 0.7: WBS 有効(AG が CVSD/mSBC 両提示、16kHz 経路は自動切替)。=n で CVSD 固定に戻る
CONFIG_BT_HFP_WBS_ENABLE=y
# controller SCO も HCI 明示(btc_hf_init が実行時上書きするので必須ではないが intent 明示)
CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y
# 省機能・RAM/Flash 節約
CONFIG_BT_HFP_CLIENT_ENABLE=n
CONFIG_BT_A2DP_ENABLE=n
CONFIG_BT_SPP_ENABLED=n
# ※ CONFIG_BT_SSP_ENABLED は ESP-IDF v5.4 に存在しない(ISS-009)。書かない。
#    SSP/レガシーは実行時 esp_bluedroid_config_t.ssp_en で切替(CR-010 / §13。現行は ssp_en=false + 固定 PIN 0000)。
#    ※ 旧記述「SSP 常時有効・IO capability は set_security_param で設定」は §13.2-4 で訂正済み。
# パーティション: カスタム partitions.csv(factory 0x1E0000)。4MB flash 明示。
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
# misc
CONFIG_BT_ACL_CONNECTIONS=2
CONFIG_ESP_MAIN_TASK_STACK_SIZE=4096
CONFIG_FREERTOS_HZ=1000
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
```

```
# partitions.csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x1E0000
```

> CODER 注 1: まず `cp examples/.../hfp_ag/sdkconfig.ci.vohci sdkconfig.defaults` をベースにし、上記「追加/上書き」分を足す運用でよい。
> CODER 注 2(rev 0.7 更新): `CONFIG_BT_HFP_WBS_ENABLE=y`。AG は CVSD と mSBC を提示し、
> SRL-3 が mSBC を選ぶと recv PCM が 16kHz になる。`bt_hf_ag.c` が `ESP_HF_AUDIO_STATE_CONNECTED_MSBC` で
> `audio_i2s_set_input_rate(16000)` を呼び音声経路を切替。CVSD 固定に戻すには `=n`(rev 0.6 の挙動)。
> CODER 注 3: **`CONFIG_BT_SSP_ENABLED` は ESP-IDF v5.4 で削除済み(ISS-009)。sdkconfig に書くと kconfig 警告**。SSP は常時有効で、IO cap のみ API で設定する。
> CODER 注 4: パーティション = カスタム `partitions.csv`(factory 0x1E0000 = 1.875MB)。app image 約 719KB(T05 ビルド実測)で factory に 63% 空き。NVS 0x6000(24KB)でピア BDA + Bluedroid ボンドに十分。
> CODER 注 5: Kconfig 名は IDF v5.4 実物で確認済み。ビルド後 `sdkconfig` の実効値を DEBUGGER が §9.5 で検証。
> CODER 注 6: `menuconfig` の場所 — Component config → Bluetooth → Bluedroid Options → Hands Free/Handset Profile →「audio(SCO) data path」= HCI /「Wide Band Speech」= OFF。Component config → Bluetooth controller →「BR/EDR Sync(SCO/eSCO) Max Connections」= 1 /「BR/EDR Sync default data path」= HCI。

### 5.2 `app_config.h`

```c
#pragma once
// ---- GPIO(§4.3)----
#define PIN_I2S_BCLK      26
#define PIN_I2S_WS        25
#define PIN_I2S_DOUT      27
#define PIN_LED_PWR       32
#define PIN_LED_LINK      33
#define PIN_LED_SCO       13
#define PIN_BTN_BOND      14
#define BTN_HOLD_MS       3000

// ---- Bluetooth(§5)----
#define BT_DEVICE_NAME    "DummyIntercom"
#define BT_COPS_NAME      "DummyNet"
#define SCO_KEEPALIVE_MS  3000
#define ACL_RECONNECT_MS  5000

// ---- Audio(§6)----
#define PCM_SAMPLE_RATE_HZ     8000
#define PCM_BYTES_PER_SAMPLE   2
#define PCM_TX_BLOCK_US        7500     // outgoing_data_ready 周期 = eSCO Tesco 整合
#define JITTER_RB_BYTES        4096     // ≈ 256ms @8kHz mono S16
#define JITTER_TARGET_BYTES    960      // 60ms(短縮時 640 = 40ms)
#define JITTER_PREFILL_BYTES   480      // 30ms たまってから再生開始
#define I2S_CHUNK_MONO_SAMPLES 64       // TX タスク 1 回の処理単位(8ms)
#define I2S_DMA_DESC_NUM       8
#define I2S_DMA_FRAME_NUM      128
#define RMS_LOG_PERIOD_US      1000000

// ---- NVS ----
#define NVS_NS            "dummyicom"
#define NVS_KEY_PEER_BDA  "peer_bda"    // blob, 6 bytes
```

### 5.3 `storage.c/.h`

```c
// storage.h
void storage_init(void);                       // nvs open (create if absent), peer 読出
bool storage_has_peer(void);
void storage_get_peer(esp_bd_addr_t out);       // 6 bytes
void storage_save_peer(const esp_bd_addr_t bda); // 既存と同一なら書込スキップ
void storage_factory_reset(void);               // peer キー消去 + Bluedroid ボンド全消去
```
実装:
- `storage_init`: `nvs_open(NVS_NS, NVS_READWRITE, &s_h)`。`nvs_get_blob(s_h, NVS_KEY_PEER_BDA, s_peer, &len)`(len=6 なら `s_has_peer=true`)。失敗時 `s_has_peer=false` + `ESP_LOGW`。
- `storage_save_peer`: `memcmp(s_peer, bda, 6)==0 && s_has_peer` なら return。else `nvs_set_blob` + `nvs_commit` + `s_peer=bda; s_has_peer=true`。ログ。
- `storage_factory_reset`:
  ```c
  int n = esp_bt_gap_get_bond_device_num();
  if (n > 0) { esp_bd_addr_t list[n]; esp_bt_gap_get_bond_device_list(&n, list);
               for (i) esp_bt_gap_remove_bond_device(list[i]); }
  nvs_erase_key(s_h, NVS_KEY_PEER_BDA); nvs_commit(s_h);
  ```
- 呼び出し文脈: `storage_init` は `app_main`。`storage_save_peer` は GAP `AUTH_CMPL` / HF `SLC_CONNECTED`(`BtAppT` 文脈)。`storage_factory_reset` は `leds` タスク → 直後 `esp_restart()`。

### 5.4 `leds.c/.h`

```c
// leds.h
typedef enum { LINK_OFF, LINK_BLINK, LINK_ON } link_led_t;
typedef enum { SCO_OFF, SCO_ON, SCO_ERR } sco_led_t;
void leds_init(void);                 // GPIO 設定, PWR=1, leds タスク起動
void leds_set_link(link_led_t s);     // 他タスクから呼べる(単純代入 + volatile)
void leds_set_sco(sco_led_t s);
```
- `leds` タスク(50ms 周期):
  - LINK: OFF→消灯 / BLINK→1Hz / ON→点灯
  - SCO: OFF→消灯 / ON→点灯 / ERR→10Hz(直近 1s の underrun+drop が閾値超で `audio_i2s` が要求)
  - ボタン: `gpio_get_level(PIN_BTN_BOND)==0` 連続時間を加算。`>= BTN_HOLD_MS` で
    `ESP_LOGW("BOND clear"); vTaskDelay(100ms); storage_factory_reset(); esp_restart();`
- PWR は起動時 `gpio_set_level(PIN_LED_PWR, 1)` 固定。

### 5.5 `audio_i2s.c/.h`

> **rev 0.7**: I2S は **48kHz 固定**。`i2s_tx` タスクは RB から取り出した SCO レート PCM を
> `audio_dsp_process()`(§6.1 = HPF + ゲート + 8k/16k→48k FIR)に通してから mono→stereo 展開・
> `i2s_channel_write`。大きいアップサンプル後バッファはタスクスタック肥大を避け file-scope static。
> 新 API `audio_i2s_set_input_rate(hz)` を SCO 接続時に `audio_i2s_reset()` の前に呼ぶ。
> 以下の擬似コードは rev 0.6 のもの(構造は不変、DSP 挿入と 48kHz 化のみ差分)。

```c
// audio_i2s.h
void audio_i2s_init(void);                          // I2S0 TX + ジッタ RB + i2s_tx タスク
void audio_i2s_set_input_rate(uint32_t hz);         // rev 0.7: SCO PCM レート 8000/16000
void audio_i2s_write(const uint8_t *pcm_s16_mono, uint32_t len);  // recv_cb から。非ブロッキング
void audio_i2s_set_sco_active(bool active);         // false: 無音出力 + RB フラッシュ
void audio_i2s_reset(void);                         // SCO 接続時: RB + DSP クリア, prefill リセット
uint32_t audio_i2s_underrun_count(void);            // leds の SCO_ERR 判定用
```

`audio_i2s_init`:
```c
s_jitter_rb = xRingbufferCreate(JITTER_RB_BYTES, RINGBUF_TYPE_BYTEBUF);
i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
cc.dma_desc_num = I2S_DMA_DESC_NUM; cc.dma_frame_num = I2S_DMA_FRAME_NUM;
i2s_new_channel(&cc, &s_tx_chan, NULL);
i2s_std_config_t sc = {
  .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(PCM_SAMPLE_RATE_HZ),
  .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
  .gpio_cfg = { .mclk=I2S_GPIO_UNUSED, .bclk=PIN_I2S_BCLK, .ws=PIN_I2S_WS,
                .dout=PIN_I2S_DOUT, .din=I2S_GPIO_UNUSED, .invert_flags={0,0,0} },
};
i2s_channel_init_std_mode(s_tx_chan, &sc);
i2s_channel_enable(s_tx_chan);
xTaskCreate(i2s_tx_task, "i2s_tx", 3072, NULL, configMAX_PRIORITIES-4, &s_i2s_task);
```

`audio_i2s_write`(recv_cb 文脈):
```c
if (!s_jitter_rb) return;
BaseType_t ok = xRingbufferSend(s_jitter_rb, pcm, len, 0);   // タイムアウト 0
if (ok != pdTRUE) {
    // 満杯: 古い側を 1 チャンク捨てて再試行(遅延を増やさない)
    size_t got; void *old = xRingbufferReceiveUpTo(s_jitter_rb, &got, 0, len);
    if (old) vRingbufferReturnItem(s_jitter_rb, old);
    xRingbufferSend(s_jitter_rb, pcm, len, 0);
    s_overrun++;
}
```

`i2s_tx_task`:
```c
int16_t mono[I2S_CHUNK_MONO_SAMPLES];
int16_t stereo[I2S_CHUNK_MONO_SAMPLES*2];
for (;;) {
    bool play = s_sco_active && s_prefilled;
    if (!s_prefilled && s_sco_active) {
        size_t avail; vRingbufferGetInfo(s_jitter_rb, NULL,NULL,NULL,NULL,&avail);
        if (avail >= JITTER_PREFILL_BYTES) s_prefilled = true;
    }
    size_t got = 0;
    if (play) {
        void *p = xRingbufferReceiveUpTo(s_jitter_rb, &got, pdMS_TO_TICKS(8),
                                         I2S_CHUNK_MONO_SAMPLES*2);
        if (p) { memcpy(mono, p, got); vRingbufferReturnItem(s_jitter_rb, p); }
    }
    uint32_t nmono = got / 2;
    if (nmono < I2S_CHUNK_MONO_SAMPLES) {
        // アンダーラン or SCO 断: 残りを無音で埋める
        memset(&mono[nmono], 0, (I2S_CHUNK_MONO_SAMPLES - nmono)*2);
        if (play && nmono == 0) { s_underrun++; s_prefilled = false; }
        nmono = I2S_CHUNK_MONO_SAMPLES;
    }
    for (uint32_t i=0;i<nmono;i++){ stereo[2*i]=mono[i]; stereo[2*i+1]=mono[i]; }
    size_t w; i2s_channel_write(s_tx_chan, stereo, nmono*4, &w, portMAX_DELAY);
}
```
- `i2s_channel_write` がブロックすることで自然にレートが 8kHz に律速される(DMA 消費待ち)。→ 追加のレート制御タイマ不要。
- `audio_i2s_set_sco_active(false)`: `s_sco_active=false`。TX タスクは無音出力継続。RB は次の `audio_i2s_reset` でクリア。
- `audio_i2s_reset()`: `s_sco_active` を触らず、RB を `xRingbufferReceiveUpTo` ループで空にする + `s_prefilled=false`。SCO connected 時に呼ぶ。

### 5.6 `bt_data_path.c/.h`

```c
// bt_data_path.h
void bt_data_path_init(void);                 // esp_hf_ag_register_data_callback(recv, send)
void bt_data_path_on_audio_connected(void);   // outgoing_data_ready 周期タイマ start
void bt_data_path_on_audio_disconnected(void);// timer stop
void bt_data_path_periodic_rms_log(void);     // 1s タイマから
```
```c
static void recv_cb(const uint8_t *buf, uint32_t len) {
    // RMS 累積(int64 sum of squares)
    const int16_t *s = (const int16_t*)buf;
    for (uint32_t i=0;i<len/2;i++) s_rms_acc += (int32_t)s[i]*s[i];
    s_rms_cnt += len/2;
    audio_i2s_write(buf, len);
}
static uint32_t send_cb(uint8_t *buf, uint32_t len) {
    memset(buf, 0, len);
    return len;                     // 常に len バイトの無音(#6218 回避: sz 厳守)
}
static void tx_ready_timer_cb(void *arg) { esp_hf_ag_outgoing_data_ready(); }
```
- `bt_data_path_init`: `esp_hf_ag_register_data_callback(recv_cb, send_cb)`。`esp_timer_create` で `s_tx_ready_timer`(周期 `PCM_TX_BLOCK_US`)を作成(start はしない)。
- `bt_data_path_on_audio_connected`: `esp_timer_start_periodic(s_tx_ready_timer, PCM_TX_BLOCK_US)`。
- `bt_data_path_on_audio_disconnected`: `esp_timer_stop(s_tx_ready_timer)`。
- `bt_data_path_periodic_rms_log`: `if (s_rms_cnt) { rms = isqrt(s_rms_acc/s_rms_cnt); ESP_LOGI("rms=%u cnt=%u under=%u over=%u", ...); reset; }`。1s の `esp_timer` を `app_main` or `bt_hf_ag` で作成。

> 設計判断: recv/send は **init 時 1 回登録**。ESP-IDF 例は AUDIO connected 時に登録するが、API doc は「bluedroid enable 後ならいつでも可」。1 回登録の方が state 管理が単純。**T3 でハング/無反応なら「connected 時登録・disconnected 時に登録解除相当」へ変更(U-02)**。

### 5.7 `bt_hf_ag.c/.h`

```c
// bt_hf_ag.h
void bt_hf_ag_start(void);                     // GAP/HF コールバック登録 + esp_hf_ag_init + dispatch(stack_up)
// 内部
static esp_bd_addr_t s_peer;                   // 現ピア(storage とミラー)
static bool s_slc = false;
static uint16_t s_sco_handle = 0;
static bool s_sco = false;
static int s_call = 0, s_callsetup = 0;        // 偽装 call state
static esp_timer_handle_t s_keepalive_timer, s_reconnect_timer, s_rms_timer;
```

**GAP コールバック `bt_gap_cb(event, param)`**(軽量 → 必要時 dispatch):
```
ESP_BT_GAP_AUTH_CMPL_EVT:
   if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
       memcpy(s_peer, param->auth_cmpl.bda, 6);
       dispatch → storage_save_peer(s_peer); bt_hf_ag_apply_scan_mode();
       ESP_LOGI("Bonded: %s", ...);
   } else ESP_LOGW("Auth failed: %d", stat);
ESP_BT_GAP_CFM_REQ_EVT:  esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);   // Just Works 自動許可
ESP_BT_GAP_PIN_REQ_EVT:  esp_bt_pin_code_t p={'0','0','0','0'}; esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, p);
ESP_BT_GAP_KEY_NOTIF_EVT / MODE_CHG_EVT: ログのみ
```

**HF コールバック `bt_hf_cb(event, param)`**: §4.2 の表のとおり。実処理は
`esp_hf_ag_cind_response` 等の即応答系はコールバック内で直接、`audio_connect`/`slc_connect` は
`bt_app_work_dispatch` 経由で `BtAppT` から。

**`bt_hf_ag_apply_scan_mode()`**:
```c
if (storage_has_peer())
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
else
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
```

**タイマ**:
```c
// keepalive(3s 周期、SLC 中のみ有効)
static void keepalive_cb(void *a) {
    if (s_slc && !s_sco) dispatch → esp_hf_ag_audio_connect(s_peer);
}
// reconnect(5s 周期、ACL 未接続 && peer あり)
static void reconnect_cb(void *a) {
    if (!s_slc && storage_has_peer()) {
        leds_set_link(LINK_BLINK);
        dispatch → esp_hf_ag_slc_connect(s_peer);
    }
}
```
- keepalive は `SLC_CONNECTED` で `esp_timer_start_periodic(s_keepalive_timer, 3s)`、`DISCONNECTED` で stop。
- reconnect は `DISCONNECTED` で start、`SLC_CONNECTED` で stop。
- **dispatch 経由必須**: `esp_timer` コールバックから `esp_hf_ag_*` を直接呼ぶと BTC タスクとの競合の恐れ。`bt_app_work_dispatch` で `BtAppT` へ。

### 6. 主要アルゴリズム / シーケンス設計

#### 6.1 リサンプル / DSP(rev 0.7)
I2S は **48000 Hz 固定**。recv PCM(CVSD=8kHz / mSBC=16kHz S16 mono)を `audio_dsp` で
48kHz に変換してから I2S へ。処理は **i2s_tx タスク文脈**(recv_cb ではない → ISS-013 不変)。
入力サンプルごとに:

1. **1 次ハイパス**(`AUDIO_HPF_CUTOFF_HZ` 既定 120Hz): `y = a·(y1 + x − x1)`、`a = exp(−2π·fc/fs)`。
   DC・ゴロ・ハムの裾を除去。`AUDIO_HPF_CUTOFF_HZ 0` で無効。
2. **ノイズゲート(下方エキスパンダ)**: **既定は無効(`AUDIO_GATE_ENABLE 0`)**。有効時は
   ピークホールド包絡 → 閾値 `AUDIO_GATE_OPEN_LEVEL` 超でゲイン目標 1.0、以下は `AUDIO_GATE_FLOOR`
   (0.10 = −20dB)。アタック速(発話頭を潰さない)・リリース緩(ポンピング防止)、`AUDIO_GATE_HOLD_MS` ホールド。
3. **ポリフェーズ FIR アップサンプル**: L=6(8k)/ L=3(16k)。Kaiser 窓 sinc、32 taps/phase、
   各 phase を DC ゲイン 1 に正規化。`y[n·L+p] = Σ_k h_p[k]·x[n−k]`。CVSD 帯域(〜3.4kHz)は
   平坦(−0.5dB 以内)、イメージ帯 70dB+ 減衰。係数は `tools/gen_fir.py` で生成し `audio_dsp.c` に埋め込み。

`bt_hf_ag.c` の `ESP_HF_AUDIO_STATE_EVT` が `CONNECTED` / `CONNECTED_MSBC` を見て
`audio_i2s_set_input_rate(8000/16000)` を呼び、DSP の係数と HPF/ゲート係数を切替(状態リセット)。
無音(SCO 断・アンダーラン)も DSP を通すのでフィルタ状態は連続、I2S は常時稼働。

#### 6.2 mono → stereo
I2S スロットは STEREO(PCM5102A に mono 専用モードなし)。`stereo[2i]=stereo[2i+1]=mono[i]`。
出力段(§4.4)で L/R を 470Ω で加算するため電気的にも mono。

#### 6.3 ジッタバッファ
- `RINGBUF_TYPE_BYTEBUF`、容量 4096B(≈256ms)。目標充足 `JITTER_TARGET_BYTES`(60ms、短縮 40ms)。
- **プリフィル**: SCO connected → `audio_i2s_reset()` → `s_prefilled=false`。`JITTER_PREFILL_BYTES`(30ms)たまるまで無音出力、以降再生。
- **オーバーラン**(recv 過多 or I2S 消費不足): `audio_i2s_write` が古い側を捨てて最新を優先(遅延を増やさない)。`s_overrun++`。
- **アンダーラン**: TX タスクが無音パディング、`s_underrun++`、`s_prefilled=false`(再プリフィル)。
- **遅延見積り**: SCO ~15ms + プリフィル 30ms + ジッタ 60ms + I2S DMA(8 desc × 128 frame / 8kHz ≈ 128ms は最大、実効は 2〜3 desc ≈ 30〜48ms)。→ **合計 ~90–110ms**。目標 <100ms 前後。<60ms を狙うなら `JITTER_TARGET=40ms` + `DMA_DESC_NUM=4`/`FRAME_NUM=64`。
- 遅延は固定(負帰還レート制御なし)。SCO クロックと I2S クロックは独立だが、`i2s_channel_write` のブロックで I2S 側が律速され、長時間ドリフトはオーバー/アンダーランとして吸収(音は途切れるがピッチ不変)。**30 分連続で累積が問題になる場合は Phase 2 で ±1 sample のレート整合を追加(§13)**。

#### 6.4 通話偽装シーケンス(RQ-07 / §5.3-2)

| SRL-3 の AT | イベント | 本機の応答(`BtAppT` 文脈) |
|---|---|---|
| `ATD<num>;` | `ESP_HF_DIAL_EVT`(type=`ESP_HF_DIAL_NUM`) | `esp_hf_ag_cmee_send(addr, ESP_HF_AT_RESPONSE_CODE_OK, ESP_HF_CME_AG_FAILURE)` → `s_callsetup=2`; `esp_hf_ag_ciev_report(addr, ESP_HF_IND_TYPE_CALLSETUP, 2)` → 200ms 後 `s_call=1; s_callsetup=0`; `ciev CALL=1`, `ciev CALLSETUP=0`; `esp_hf_ag_out_call(addr,1,0,1,0,num,129)` → SCO 未確立なら `esp_hf_ag_audio_connect(s_peer)` |
| `AT+BLDN`(last number redial) | `ESP_HF_DIAL_EVT`(`num_or_loc==NULL`) | 同上(number は `""`) |
| `AT+CKPD=200` | `ESP_HF_UNAT_RESPONSE_EVT`(`unat` に `"+CKPD=200"`) | ログ; `esp_hf_ag_unknown_at_send(addr, "")`(OK); SCO 未確立なら `audio_connect` |
| 発呼系なしで直接 SCO 要求 | `ESP_HF_AUDIO_STATE_EVT`=CONNECTED | そのまま受理 |

- 200ms の遅延は `esp_timer` one-shot(`s_dial_timer`)で実装。
- `esp_hf_ag_out_call` は「HF 駆動」ケース。doc 上 OK/ERROR 応答が必要 → 先に `cmee_send(OK)`。
- **設計判断(BOSS U-03=(a))**: 一度 `s_call=1` にしたら SRL-3 が `AT+CHUP` を送るまで維持。`CHUP`(`ESP_HF_CHUP_RESPONSE_EVT`)で `esp_hf_ag_end_call` + `s_call=0`。その後 keepalive が SCO を張り直す(call なしの `audio_connect` は demo `cona` で実績あり)。
- **簡略版への切替(実装コメント必須)**: `fake_call_accept()` 冒頭に
  `// [簡略版] 通話中偽装が SRL-3 のインカム動作に副作用を与える場合、cmee_send(OK) の直後に`
  `// esp_hf_ag_audio_connect(s_peer) だけ行い、s_callsetup/s_call/out_call/ciev は一切触らず return する。`
  と明記。実機で R-NEW-06 が顕在化したらこの 1 箇所で切替。

#### 6.5 上り無音送出(RQ-05)
- `send_cb`: `memset(buf,0,len); return len;`(#6218 対策として `len` 厳守)。
- SCO connected 中のみ `s_tx_ready_timer`(7.5ms 周期)が `esp_hf_ag_outgoing_data_ready()` を呼ぶ。
- ESP-IDF 例の「ringbuffer 充足チェック + セマフォ + 専用タスク」は不要(無音は常に用意できる)。
- `bta_ag_sco_read_cback: status(2)` warning が出ても下り受信に影響しないことを T3 で確認。多発するなら `outgoing_data_ready` 周期を SCO の実 Tesco に合わせて調整。

#### 6.6 RMS ログ(§5.4 / R1 検証)
`recv_cb` で `sum += s*s`。1s の `esp_timer` で `rms = isqrt(sum/cnt)` をログ。`isqrt` は整数ニュートン法。
発話有無・グループ音声の判定材料。

---

## 7. データ構造

| 変数 | 型 | 所有 | 保護 | 説明 |
|---|---|---|---|---|
| `s_jitter_rb` | `RingbufHandle_t`(BYTEBUF, 4096) | audio_i2s | FreeRTOS Ringbuffer 内部ロック | ジッタバッファ。producer=recv_cb(BTC), consumer=i2s_tx |
| `s_tx_chan` | `i2s_chan_handle_t` | audio_i2s | i2s_tx タスクのみ write | I2S0 TX |
| `s_sco_active` / `s_prefilled` | `volatile bool` | audio_i2s | volatile | 再生ゲート |
| `s_overrun` / `s_underrun` | `uint32_t`(加算のみ) | audio_i2s | ゆるいアトミック | 統計。leds/rms が読む |
| `s_rms_acc` / `s_rms_cnt` | `int64_t` / `uint32_t` | bt_data_path | recv_cb のみ書込 | RMS 累積 |
| `s_tx_ready_timer` | `esp_timer_handle_t` | bt_data_path | - | 7.5ms 周期(SCO 中) |
| `s_peer` / `s_has_peer` | `esp_bd_addr_t` / `bool` | bt_hf_ag / storage | `BtAppT` + GAP コールバック(短時間) | ピア BDA |
| `s_slc` / `s_sco` / `s_sco_handle` | `bool` / `bool` / `uint16_t` | bt_hf_ag | BTC/BtAppT | 接続状態 |
| `s_call` / `s_callsetup` | `int` | bt_hf_ag | BtAppT | 偽装 call state。CIND/CIEV/CLCC 応答で参照 |
| `s_keepalive_timer` / `s_reconnect_timer` / `s_dial_timer` / `s_rms_timer` | `esp_timer_handle_t` | bt_hf_ag | - | タイマ群 |
| `led 状態` | `volatile enum` | leds | volatile | LINK/SCO |

- 共有リング(`s_jitter_rb`)は FreeRTOS Ringbuffer が内部で排他 → 追加ロック不要。
- `s_call`/`s_callsetup` は基本 `BtAppT` タスク内で読み書き。CIND/CLCC 応答がコールバック内直接実行なら BTC からも読む → `int` 単純読みで可(整合性の worst case でも古い値を返すだけ、実害小)。厳密にしたいなら CIND/CLCC 応答も dispatch(推奨)。**→ CIND/CLCC/CIEV 応答も dispatch で統一する**(§9.3)。

---

## 8. 影響範囲・リスク・注意事項

### 8.1 新規リスク

**issue_list.md へ登録済み(BOSS): R-NEW-01〜07 ≒ ISS-012〜018**。以下は設計側の記録。ISS-009〜011(DEBUGGER ビルド検証)は §5.1 / §2.4 に反映済み。

| ID | 内容 | 重大度 | 対応 |
|---|---|---|---|
| R-NEW-01 | ISS-008: HCI SCO の codec ネゴ / データパス不具合の履歴。SRL-3 実機で SCO が張れない可能性 | High | sdkconfig(§5.1: `AUDIO_DATA_PATH_HCI=y` + `MAX_SYNC_CONN=1` + `WBS=n`)。T3 最優先。keepalive/reconnect で復旧。詰まったら recv/send を「接続時登録」へ(U-02)、`outgoing_data_ready` 周期を調整 |
| R-NEW-02 | recv_cb で `audio_i2s_write` が重い/ブロックすると BTC タスクが詰まり SCO 全体が破綻 | High | `xRingbufferSend(...,0)` のみ。RMS は加算だけ。ログ・`i2s_channel_write` を絶対に呼ばない |
| R-NEW-03 | `esp_timer` コールバックから `esp_hf_ag_*` を直呼びすると Bluedroid 内部状態と競合 | Medium | 全て `bt_app_work_dispatch` 経由で `BtAppT` から呼ぶ |
| R-NEW-04 | I2S クロック(8kHz)と SCO クロックの独立ドリフトで 30 分連続時に累積オーバー/アンダーラン | Medium | ジッタバッファで吸収(ピッチ不変、稀に途切れ)。T6/T7 で評価。問題なら Phase 2 でレート整合 |
| R-NEW-05 | PCM5102A が 8kHz LRCK を受け付けるか(モジュール個体差) | Low | データシート上 8k 対応。不安なら Phase 2 で 8k→32k/48k アップサンプルして I2S 出力(PCM5102A 確実動作域) |
| R-NEW-06 | `s_call` 偽装で SRL-3 が「通話中」表示になりインカム動作に副作用 | Medium | T3/T4 実機確認。副作用大なら「DIAL に OK だけ返し call state を変えず audio_connect のみ」に変更(§6.4 の簡略版) |
| R-NEW-07 | Bluedroid ボンド情報と自前 NVS キーの二重管理で不整合(片方だけ消える) | Low | `storage_factory_reset` で両方消す。`AUTH_CMPL` 成功時のみ自前保存 |
| R-NEW-08 | CoD 値の相違(§0-D) | Low | intent 優先で設定。SLC 不成立時に `0x20040C` 固定へ |
| R-NEW-09 | 実機なし(ISS-002) | Medium | ビルド + 静的検証まで。T1〜T13 手順書化・未実施明示 |

### 8.2 既存機能への副作用
新規開発。ESP-IDF 例からの主な変更 = console REPL 削除 / PCM(gpio_pcm)削除 / recv・send コールバックの実装差し替え / I2S 追加。BT スタック設定は例の sdkconfig.defaults を継承。

### 8.3 性能・リソース
| 項目 | 見積り |
|---|---|
| CPU | recv_cb(RMS+RB write)< 2% / i2s_tx(memcpy+expand+write)< 3% / BT スタック 既定 |
| RAM | ジッタ RB 4KB + I2S DMA(8×128×4 = 4KB)+ stereo バッファ + Bluedroid(~40–60KB)。WROOM 320KB DRAM に余裕 |
| Flash | Bluedroid classic+HFP ~1.3–1.5MB。factory 1.875MB で収まる。4MB フラッシュ前提 |
| 消費電流 | §9: 100–160mA @5V |
| E2E 遅延 | ~90–110ms(§6.3。40ms ジッタ設定で ~70ms) |

### 8.4 セキュリティ / 保守性
- SSP Just Works(IO_CAP_NONE)= MITM 保護なし。用途上許容(仕様どおり)。
- ピア 1 台。2 台目の SLC は受理するが自前 NVS は**上書きしない**(最初のペアを維持。discoverable も初回以降 off なので通常起きない)。
- ログに BDA が出る(シリアル)。物理接続前提で許容。
- モジュール分割・`app_config.h` 集約・タスク文脈を §3.1 に明文化。

### 8.5 タスク間排他(R8 相当)
| 資源 | アクセス元 | 排他 |
|---|---|---|
| `s_jitter_rb` | recv_cb(BTC)書込 / i2s_tx 読出 | FreeRTOS Ringbuffer 内蔵 |
| `s_tx_chan` | i2s_tx のみ | 不要 |
| `esp_hf_ag_*` API | `BtAppT` のみ(dispatch 経由に統一) | Bluedroid 内部 |
| `esp_bt_gap_*` API | `BtAppT` / GAP コールバック即応答 | Bluedroid 内部 |
| NVS(`s_h`) | `BtAppT`(save) / `leds` タスク(reset 直前) | reset 後即 `esp_restart` なので NVS ハンドル自体の競合窓は無い。**ただし `esp_bt_gap_remove_bond_device()` は非同期(BTC へキュー)なので、その完了を待たずに `esp_restart()` すると Bluedroid 側 bond が消えない**(M-1 / ISS-019)。→ 修正: bond 削除 → 500ms 待機 → `esp_restart()`(または `HFW_FACTORY_RESET` を BtAppT へ dispatch)。同期 NVS 操作(`nvs_erase_key`/`nvs_commit`)は短時間で完了する。 |
| 統計カウンタ | i2s_tx 加算 / leds・rms 読出 | `uint32_t` 単純アクセス(取りこぼし許容) |
| LED 状態 enum | 各タスク書込 / leds 読出 | `volatile` 単純代入 |

**CODER 必須チェック**:
1. recv_cb / send_cb 内に `ESP_LOG*` / `i2s_channel_write` / ブロッキング呼び出しが無いこと。
2. `esp_timer` コールバック内から `esp_hf_ag_*` / `esp_bt_gap_*` を直呼びしていないこと(全て dispatch)。
   **例外: `esp_hf_ag_outgoing_data_ready()` は直呼び可**(データパスのキックであり状態機械の遷移 API ではない。内部は `osi_malloc`+`bta_sys_sendmsg` でタスク文脈から安全。7.5ms 周期を dispatch すると BtAppT キュー深さ10 を圧迫するため。T05 §10.3-1 で承認)。
3. `esp_hf_ag_register_data_callback` は `esp_bluedroid_enable()` 後・`esp_hf_ag_init()` 後に呼んでいること。
4. `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` を controller init 前に呼んでいること(BLE 無効構成)。

---

## 9. CODER への実装指示

### 9.1 実装順序
0. **環境有効化**: `export IDF_TOOLS_PATH=/home/subcon/.espressif && . /home/subcon/esp/esp-idf/export.sh`(毎シェル)。
1. **プロジェクト骨格 + ビルド通し**: `CMakeLists.txt` / `main/CMakeLists.txt` / `sdkconfig.defaults`(`cp` vohci + 上書き。カスタム `partitions.csv`、SSP 行なし)/ `partitions.csv` / `app_config.h` / 空実装の各 `.c/.h`。`app_main` は NVS init + `ESP_LOGI("boot")` のみ。**`idf.py set-target esp32 && idf.py build` が通ること**を確認(DEBUGGER 並行)。
2. **BT 起動 + GAP + HF init**: `app_main.c` §4.1、`bt_hf_ag.c` の GAP/HF コールバック(ログのみ)、`bt_app_core` 流用。→ ビルド + ログで `HFP-AG ready` / discoverable を確認。
3. **I2S 出力**: `audio_i2s.c`。まず正弦波 or ゼロを 8kHz stereo で出し続ける TX タスク。→ ビルド。
4. **データパス**: `bt_data_path.c`。recv_cb → `audio_i2s_write`、send_cb → 無音、outgoing_data_ready タイマ。HF `AUDIO_STATE_EVT` と接続。
5. **通話偽装 + タイマ**: `bt_hf_ag.c` の DIAL/keepalive/reconnect、CIND/CLCC/CIEV 応答。
6. **storage / leds**: NVS ピア保存、BOND-CLR、LED 状態機械。
7. **仕上げ**: ログレベル整理、警告ゼロ化、デバッグコード削除、README 記述。

### 9.2 ファイル別・関数別(シグネチャ確定)

| ファイル | 公開関数(前提 / エラー時) |
|---|---|
| `app_main.c` | `void app_main(void)` — §4.1。各 init の戻り値を `ESP_ERROR_CHECK` or ログして早期 return。BT 起動失敗時は PWR LED 5Hz + `vTaskDelay` ループ |
| `bt_hf_ag.c` | `void bt_hf_ag_start(void)` — `esp_bluedroid_enable()` 後に呼ぶ。GAP/HF コールバック登録 + `esp_hf_ag_init()` + タイマ create(start しない)+ `dispatch(stack_up)` |
| | `static void bt_gap_cb(esp_bt_gap_cb_event_t, esp_bt_gap_cb_param_t*)` — §5.7 |
| | `static void bt_hf_cb(esp_hf_cb_event_t, esp_hf_cb_param_t*)` — §4.2 |
| | `void bt_hf_ag_apply_scan_mode(void)` — §5.7。`BtAppT` から |
| `bt_data_path.c` | `void bt_data_path_init(void)` — `esp_hf_ag_register_data_callback(recv_cb, send_cb)` + `s_tx_ready_timer` create |
| | `void bt_data_path_on_audio_connected(void)` / `..._disconnected(void)` — timer start/stop |
| | `void bt_data_path_rms_log(void)` — 1s タイマから。`isqrt` 内蔵 |
| `audio_i2s.c` | `void audio_i2s_init(void)` — §5.5。失敗は `ESP_ERROR_CHECK` |
| | `void audio_i2s_write(const uint8_t*, uint32_t)` — recv_cb 文脈、非ブロッキング。`len` が奇数なら切り捨て |
| | `void audio_i2s_set_sco_active(bool)` / `void audio_i2s_reset(void)` |
| | `uint32_t audio_i2s_underrun_count(void)` / `uint32_t audio_i2s_overrun_count(void)` |
| `storage.c` | §5.3 のシグネチャ。`nvs` エラーは `ESP_LOGW` + `s_has_peer=false` で継続 |
| `leds.c` | `void leds_init(void)` / `void leds_set_link(link_led_t)` / `void leds_set_sco(sco_led_t)` — §5.4 |

### 9.3 エラー処理・境界条件(必須)

| 状況 | 期待動作 |
|---|---|
| SCO 未確立で電源投入直後 | I2S は無音 stereo を出し続ける。ノイズ・ポップを出さない |
| プリフィル未完 | 無音出力。`JITTER_PREFILL_BYTES` 到達で再生開始 |
| ジッタ アンダーラン | 無音パディング、`s_underrun++`、再プリフィル、SCO LED を `SCO_ERR`(直近1sで閾値超時) |
| ジッタ オーバーラン | 古いデータ破棄で最新優先、`s_overrun++` |
| recv_cb の `len` が想定外(0 / 過大) | 0 は無視。過大でも `xRingbufferSend` の失敗を握って drop + counter。クラッシュしない |
| SRL-3 が mSBC を提示(BCS=USE_MSBC) | `ESP_LOGW`。`AUDIO_STATE_EVT`=CONNECTED_MSBC でも受信経路は同じ(ただし 16kHz になるので **本 Phase では非対応**、音が壊れる旨ログ)。§13 R3 |
| `esp_hf_ag_audio_connect` が非 ESP_OK | ログ。次の keepalive で再試行 |
| `esp_hf_ag_slc_connect` が非 ESP_OK | ログ。次の reconnect で再試行 |
| ペア未登録 | discoverable=GENERAL、reconnect タイマ停止。SLC 待ち |
| ペア登録済み | discoverable=NON、起動直後から reconnect タイマ |
| BOND-CLR 押下中に SLC 進行 | 3s 到達で問答無用に `storage_factory_reset()` → `esp_restart()` |
| NVS 初期化失敗(ISS-022) | 1 回目失敗(`NO_FREE_PAGES`/`NEW_VERSION_FOUND`)→ `nvs_flash_erase()` + 再 `nvs_flash_init()`。**2 回目も失敗したら `ESP_ERROR_CHECK`(panic)ではなく縮退**: `ESP_LOGE` + `leds_init()` は実行(PWR を遅い点滅 = 縮退表示)+ BT を起動しない + `vTaskDelay` ループで待機。ボンド不能でも「起動しないより起動して LED で異常を示す」。`storage_init()` は既に NVS 失敗時 `s_has_peer=false` で継続する実装済み(§5.3)。 |
| SRL-3 が想定外 AT | `UNAT` を全ログ。基本 `esp_hf_ag_unknown_at_send(addr, "")`(OK)。詰まる AT が判明したら個別対応 |
| CIND/CLCC/CIEV 応答 | **全て `bt_app_work_dispatch` 経由**で `BtAppT` から実行(§7 の s_call 整合性)。`s_call`/`s_callsetup` の現在値で応答 |

### 9.4 コーディング規約 / 制約
- ESP-IDF スタイル。関数 `module_verb_noun`、静的変数 `s_`。`ESP_LOGx(TAG, ...)`、TAG はファイルごと。
- **recv/send データコールバック内: `malloc`/`ESP_LOG`/ブロッキング/長い演算 禁止**。
- `esp_hf_ag_*` / `esp_bt_gap_*` は原則 `BtAppT` から(dispatch)。
- `esp_timer` コールバックは短時間・非ブロッキング。実処理は dispatch。
- タスクスタックは実測前は少し多め(i2s_tx 3072、BtAppT は例の 2048 → 3072 に増やす)。
- 警告 `-Werror` 相当でゼロを目標。未使用引数は `(void)`。
- デバッグ用一時コード・`#if 0`・TODO を最終成果物に残さない。
- 設計からの逸脱は**実装前に DESIGNER/BOSS へ報告**。

### 9.5 テスト観点(DEBUGGER 向け、§11 と対応)
| 観点 | 内容 |
|---|---|
| ビルド | `idf.py set-target esp32 && idf.py build`。警告ゼロ。`.bin` 生成。`idf.py size` |
| sdkconfig 検証 | ビルド後 `sdkconfig` に `BT_HFP_AUDIO_DATA_PATH_HCI=y` / `BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y`(+`_EFF=0`)/ `BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1` / `BT_HFP_WBS_ENABLE` not set / `BTDM_CTRL_MODE_BR_EDR_ONLY=y` / `BT_A2DP_ENABLE` not set / `PARTITION_TABLE_CUSTOM=y` + `ESPTOOLPY_FLASHSIZE="4MB"` が反映。**`CONFIG_BT_SSP_ENABLED` は存在しない**(kconfig 警告が出ないこと)。→ **T05 で全項目確認済み** |
| 静的 | GPIO 重複なし(26/25/27/32/33/13/14)、ストラッピング/入力専用ピン不使用、`idf.py size` が factory(~3MB)に収まる(app ~760KB 目安) |
| コールバック制約 | recv_cb/send_cb に禁止 API が無いこと(grep)。timer cb に `esp_hf_ag_*` 直呼びが無いこと |
| I2S | `i2s_std` 設定値(8kHz/16bit/stereo/Philips)、GPIO、DMA サイズ |
| HFP ロジック | CIND/CLCC/CIEV/COPS/UNAT 応答、DIAL→通話偽装→audio_connect、keepalive/reconnect の状態遷移(コードレビュー + 可能なら QEMU) |
| RMS / ジッタ | `isqrt` 単体、プリフィル/オーバー/アンダー分岐のロジック |
| 遅延見積り | 定数から E2E < 100ms |
| 実機(ブロック) | T1〜T13 は ESP32/SRL-3 実機必須。**T3(SCO 確立)を最優先**。手順書化し「未実施」 |

---

## 10. 実装後レビュー結果(T05 / DESIGNER / 2026-09-02)

### 10.0 レビュー範囲・方法
- 対象: `main/*.c/.h`(app_main / bt_hf_ag / bt_data_path / audio_i2s / storage / leds / bt_app_core)、`CMakeLists.txt` ×2、`sdkconfig.defaults`、`partitions.csv`、`README.md`。
- 方法: 設計書 §4〜§9 との突合、仕様 rev 0.6 の RQ 充足確認、**実ビルド実施**(`idf.py set-target esp32 && idf.py build` = 成功、`main/` 由来の警告 0)、`idf.py size` 確認、Bluedroid 内部(`btc_hf_ci_sco_data` / `BTA_AgCiData` / `esp_hf_ag_register_data_callback`)のソース追跡でスレッド文脈の妥当性検証。

### 10.1 総合判定: **条件付き合格(Medium 1 件の修正を推奨)**
- **Critical: 0 / High: 0 / Medium: 1 / Low: 4 / Info: 3**
- 設計構造(コア/タスク文脈分離、dispatch 方針、ジッタバッファ、通話偽装、keepalive/reconnect)は設計書どおり実装され、可読性・コメント・命名良好。不要変更・デバッグ残骸なし。`bt_app_core.c/h` は ESP-IDF 例からライセンス表記付きで流用。
- ビルド成功・警告ゼロ・サイズ余裕(app 719KB / factory 0x1E0000 に 63% 空き、静的 DRAM 34.9%)。

### 10.2 指摘事項

| # | 重大度 | 箇所 | 内容 | 修正方針 |
|---|---|---|---|---|
| **M-1** | **Medium** | `leds.c` `leds_task()` + `storage.c` `storage_factory_reset()` | BOND-CLR 長押し時、`storage_factory_reset()` は `esp_bt_gap_remove_bond_device()`(**非同期**: BTC へキュー)を発行した直後に `esp_restart()` を**即座に**呼ぶ。BTC タスクが remove-bond を処理する前に再起動するため **Bluedroid 側のリンクキー(NVS)が消えない可能性**が高い。結果: 自前の `peer_bda` キーは消えて discoverable にはなるが、同じ SRL-3 は古いリンクキーで無言再接続してしまい、T12「消去→初回ペアリング可能」を完全には満たさない。加えて `esp_bt_gap_remove_bond_device()` を `leds` タスクから呼ぶのは §8.5(GAP API は BtAppT/GAP-cb のみ)からの逸脱。 | (推奨) `storage_factory_reset()` の後に `vTaskDelay(pdMS_TO_TICKS(500))` を入れてから `esp_restart()`。より堅牢には `HFW_FACTORY_RESET` work を BtAppT へ dispatch し、そこで bond 削除 + 500ms 待機 + `esp_restart()`。`nvs_commit` 後の待機で BTC の bond 削除完了を担保。 |
| L-1 | Low | `bt_data_path.c` `s_rms_acc`(int64) | `recv_cb`(BTC 文脈)が加算、`rms_timer_cb`(esp_timer 文脈)がスナップショット+ゼロクリア。Xtensa で 64bit は非アトミック → 稀に torn read で 1 行だけ RMS ログ値が異常。**診断ログのみで機能影響なし**。 | 許容可。厳密化するなら `portMUX_TYPE` スピンロックで 3 行を囲む、または `s_rms_acc` を 32bit 平均に変更。 |
| L-2 | Low | `bt_hf_ag.c` `handle_dial` / `handle_dial_finish` | 200ms 以内に `ESP_HF_DIAL_EVT` が再来すると `HFW_DIAL_FINISH` が二重にキューされ得る(`s_call=1` 再設定・CIEV 重複送出)。冪等で実害なし。 | 許容可。気になるなら `handle_dial_finish` 冒頭で `if (s_call) return;`。 |
| L-3 | Low | `bt_hf_ag.c` `handle_stack_up` | `esp_bt_gap_set_cod` / `set_device_name` / `set_security_param` の戻り値未チェック。 | 許容可(通常失敗しない)。ログ追加は任意。 |
| L-4 | Low | `bt_data_path.c` `tx_ready_timer_cb` | esp_timer コールバックから `esp_hf_ag_outgoing_data_ready()` を直呼び(§8.5 チェック2 からの逸脱)。→ **10.3 で「妥当な例外」と判定。指摘は記録のみ**。 | 対応不要。将来 esp_timer タスク飢餓が観測されたら ESP-IDF 例同様の専用タスク(セマフォ駆動)へ。 |
| I-1 | Info | `partitions.csv` / `sdkconfig.defaults` | CODER はカスタム `partitions.csv`(factory 0x1E0000)+ `CONFIG_PARTITION_TABLE_CUSTOM` を採用(設計 rev.3 準拠、BOSS が仕様を「カスタム」に再統一)。**設計書 §3/§5.1 の rev.4「SINGLE_APP_LARGE」記述を rev.5 でカスタムに戻して整合**(本 rev.5 で対応)。CODER 実装は正。 |
| I-2 | Info | `bt_hf_ag.c` | 実ヘッダのシンボルを使用(`ESP_HF_CALL_ADDR_TYPE_UNKNOWN` / `param->bcs_rep.mode == ESP_HF_WBS_YES` / `param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS` / `ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING` 等)。設計書の `129` リテラル・`USE_MSBC` 記述より正確。**設計書 §4.2 を実ヘッダに合わせて更新済み(本 rev.5)**。CODER 実装は正。 |
| I-3 | Info | `bt_hf_ag.c` `bt_data_path.c` | U-02(recv/send 登録タイミング)は「init 時 1 回」で実装、切替コメント(`bt_data_path.c:96-99`)あり。U-03(通話偽装の深さ)は「call=1 まで」で実装、簡略版切替コメント(`bt_hf_ag.c:246-249`)あり。いずれも BOSS T02 決定どおり。 |

### 10.3 CODER 設計逸脱の可否判定(BOSS 依頼分)

| # | 逸脱内容 | 判定 | 根拠 |
|---|---|---|---|
| 1 | `tx_ready_timer_cb` が esp_timer 文脈から `esp_hf_ag_outgoing_data_ready()` を直呼び(BtAppT へ dispatch しない) | **可(妥当な例外)** | (a) esp_timer 既定 dispatch は `ESP_TIMER_TASK` = 通常の FreeRTOS タスク文脈(ISR ではない)。`esp_hf_ag_outgoing_data_ready()` → `btc_hf_ci_sco_data()` → `BTA_AgCiData()` = `osi_malloc` + `bta_sys_sendmsg`(キュー post)で、**タスク文脈から安全**。(b) これは「データを取りに来い」という**データパスのキックのみ**で、Bluedroid の状態機械を遷移させる制御 API ではない。(c) 133Hz を深さ 10 の BtAppT キューへ dispatch すると `xQueueSend`(10ms timeout)の失敗連鎖・キュー圧迫で悪化する。(d) ESP-IDF 例も同等レートでタスクから駆動している。→ §8.5 チェック2 に「`esp_hf_ag_outgoing_data_ready()` は例外(データパスキック、タスク文脈可)」と追記(本 rev.5)。 |
| 2 | CoD = `TELEPHONY \| AUDIO` → 実効 ≈ `0x60020C`(設計 §0-D プローズは `0x40020C`) | **CODER 実装が正。設計 §0-D プローズを修正**(本 rev.5) | 設計 §4.1/§5.2 のコード片自体が `ESP_BT_COD_SRVC_TELEPHONY \| ESP_BT_COD_SRVC_AUDIO` を書いており、CODER はそれに忠実。§0-D の「≈0x40020C」は DESIGNER のプローズ誤り(Telephony のみで計算していた)。HFP-AG は Audio + Telephony の両サービスを提供するので両ビット立てが妥当。機能影響なし(SRL-3 は major=Phone を見る)。SLC 不成立時に literal `0x20040C` へ切替(U-05)。 |
| 3 | カスタム `partitions.csv` 採用(設計 rev.4 は SINGLE_APP_LARGE) | **CODER 実装が正**(BOSS が仕様を「カスタム」に再統一) | I-1 参照。設計書を rev.5 でカスタムに戻す。 |
| 4 | 全 HF イベントを BtAppT へ dispatch(純ログ系除く) | **可**。設計 §7/§9.3 どおり | 純ログ系(VOLUME/BCS/WBS/BVRA/NREC/VTS/PROF_STATE)は `ESP_LOGI` のみでインライン。`esp_hf_ag_*` を呼ぶものは全て dispatch。CIND/CLCC/CNUM/IND_UPDATE も dispatch(§9.3 の s_call 整合性要件を満たす)。 |
| 5 | 実ヘッダ優先のシンボル修正 | **可(推奨)**。設計書を実ヘッダに整合(本 rev.5) | I-2 参照。 |

### 10.4 要求充足(仕様 rev 0.6)
RQ-01〜18 すべて実装確認。SCO keepalive(3s)/ACL reconnect(5s)/通話偽装/上り無音/NVS ピア保存/GAP(名前・CoD・IO_CAP_NONE)/discoverable トグル/CIND 応答(service=1,signal=5,batt=5)/UNAT→OK/LED 3 状態/SCO 断時無音出力/BLE 無効 — いずれも設計どおり。**BOND-CLR のみ M-1 の修正で完全充足**。

### 10.5 DEBUGGER T04 との整合
- `test_report.md` は「CODER 実装前(v0.2)」。本レビューで S1(ビルド)= 成功、S2(警告)= `main/` 由来 0、S3(sdkconfig)= §5.1 の全キー一致(`BT_HFP_AUDIO_DATA_PATH_HCI=y` / `WBS` not set / `BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y` / `MAX_SYNC_CONN=1` / `BR_EDR_ONLY=y` / `A2DP` not set / `SSP_ENABLED` 不在)を確認済み。
- **DEBUGGER へ依頼**: S4(`idf.py size` 詳細 / 実行時ヒープ余裕)、S6(GPIO 静的検証: 26/25/27/32/33/13/14 とストラッピング GPIO12/15、入力専用 34-39 の非重複)、S7(静的解析)、S10(WBS Phase 2 ビルド)、A2(LED 状態遷移レビュー)を実施。
- **追加テスト提起**: M-1 修正後、T12(BOND-CLR)は「自前キー消去 + Bluedroid bond 消去の両方」を確認する手順に更新。A3(上り無音送出)は `tx_ready_timer` 7.5ms 周期と send_cb ゼロ埋めを btmon で確認。

### 10.6 CODER への修正依頼(M-1)
`storage_factory_reset()` 呼び出し後・`esp_restart()` 前に **500ms の待機**を入れる(推奨は BtAppT への dispatch 化)。他の L-1〜L-4 は「許容(任意対応)」。

---

## 10.7 T05 二次レビュー結果(DESIGNER / 2026-09-02、差分のみ)

対象差分: `bt_hf_ag.c/.h`(`HFW_FACTORY_RESET` work + `bt_hf_ag_request_factory_reset()` 新設)、`leds.c`(GAP/storage 直呼び削除)、`app_main.c`(NVS 二重失敗の縮退)。**ビルドは DEBUGGER 使用中のため未実行、コード + include の静的確認のみ。**

### 判定: **合格。Critical/High/Medium 0。新規 Low/Info のみ。M-1(ISS-019)/ ISS-022 とも解消。**

| 項目 | 判定 | 根拠 |
|---|---|---|
| **ISS-019 修正 vs 設計 §8.5 rev.5** | **一致(かつ改善)** | `bt_hf_ag_request_factory_reset()`(bt_hf_ag.c)→ `bt_app_work_dispatch(HFW_FACTORY_RESET)`。ハンドラ(BtAppT 文脈)で `storage_factory_reset()` → **`while(esp_bt_gap_get_bond_device_num()>0 && 経過<1000ms) vTaskDelay(50)`**(= bond 削除の実完了をポーリング確認)→ `vTaskDelay(300ms)`(NVS flush 余裕)→ `esp_restart()`。DESIGNER 推奨の「盲目的 500ms 待機」より、実際の bond 数 0 を確認する方式で確実。`esp_bt_gap_get_bond_device_num()` は同期・スレッドセーフ。 |
| **`request_factory_reset()` の文脈安全性** | **OK** | `leds` タスクは `bt_hf_ag_request_factory_reset()` を呼ぶだけ(戻り値 false = キュー未起動 → 次 tick で再試行)。GAP/NVS API は一切呼ばない。成功時は `reset_sent=true` で LED 駆動停止し idle。§8.5 チェック 3/4 を満たす。 |
| **ISS-022 修正 vs 設計 §9.3 rev.5** | **一致(かつ改善)** | `app_main.c`: 1回目 `NO_FREE_PAGES`/`NEW_VERSION_FOUND` → `nvs_flash_erase()`(**戻り値も判定**)→ 再 init。それでも `err!=ESP_OK` なら `ESP_ERROR_CHECK`(panic/再起動ループ)ではなく `ESP_LOGE` + **`pwr_blink_forever(900)`**(0.55Hz 遅い点滅、`fatal_blink` の 5Hz と視覚的に区別)。設計は「`leds_init()` 実行」としたが、CODER は依存のない自己完結 `pwr_blink_forever`(PWR ピンのみ `gpio_config`)で同等の可視結果 → **依存削減で改善。設計意図に合致**。 |
| **回帰: work キュー種別追加** | **影響なし** | `HFW_FACTORY_RESET` は enum 末尾に追加(既存値の再割当なし)。`hf_work_handler` の `switch` は名前ラベル + `default` 有り。`hf_work_t` 構造体不変。 |
| **ポーリング 最大 1s + 300ms** | **許容(過剰でない)** | BOND-CLR は稀な wipe→reboot 操作。ポーリングは bond 0 到達で早期脱出(通常 <200ms)。300ms は NVS commit マージン。この間 BtAppT がブロックするが直後に再起動するため実害なし。 |
| L-5(Low、新規) | 許容 | `dispatch` 成功後に(ほぼあり得ないが)BtAppT が停止し `esp_restart` 未到達だと LED 消灯のまま。旧実装(leds タスクで直 `esp_restart`)には無かった経路。BtAppT は有界処理のみでリスク無視可。 |
| Info(新規) | — | 縮退モード(ISS-022)では `leds_init()` 前に `pwr_blink_forever` へ入るため BOND-CLR は使用不可。NVS 破損時に消すものは無いので許容。 |

### 未対応(許容)
L-1(`s_rms_acc` 非アトミック / 診断ログのみ)、L-2(200ms 内 DIAL 二重 FINISH / 冪等)、L-3(`handle_stack_up` の GAP 戻り値未チェック)、L-4(逸脱1 = 承認済み)。いずれも機能影響なし、任意対応。

### DEBUGGER へ
- 再ビルド + `main/` 警告 0、`idf.py size`(縮退パス追加後のサイズ影響は軽微の見込み)を確認依頼。
- T12(BOND-CLR)手順を「(a) 自前 `peer_bda` キー消去 (b) `esp_bt_gap_get_bond_device_num()==0` (c) 再起動後に discoverable かつ SRL-3 が旧リンクキーで無言再接続しない」の 3 点確認に更新。

---

## 11. 未解決事項(BOSS 判断 T02 済み)

**BOSS レビュー(T02)で下記のとおり決定。issue_list.md「BOSS 判断(T02)」節と一致。**

| # | 事項 | **BOSS 決定** | 実装への落とし込み |
|---|---|---|---|
| U-01 | 実機なしで T3(SCO 確立)が検証不能。ISS-008 の残存リスク | **(a) ビルド + 静的検証で完了扱い**。T3 を「本開発の最大の未検証点」としてユーザーへ明示報告 | 実装は仕様どおり継続。`test_report.md` に T3 手順を記載し「未実施」 |
| U-02 | recv/send データコールバックの登録タイミング | **(a) init 時 1 回登録**。切替を容易にしておく | `bt_data_path_init()` で登録。接続時登録版へは `bt_data_path_on_audio_connected/disconnected` に登録/解除を移すだけで切替可能な構造にする |
| U-03 | 通話偽装の深さ(§6.4) | **(a) `out_call` で call=1 まで進める**。簡略版への切替点をコメントで明示 | §6.4 の実装。`fake_call_accept()` 冒頭に「// 簡略版: 以降をスキップし audio_connect のみにする場合はここで return」コメント |
| U-04 | ジッタバッファ既定値 | **60ms** | `JITTER_TARGET_BYTES = 960`(`app_config.h`)。変更は 1 定数 |
| U-05 | CoD 値(§0-D) | **(a) intent 優先**(major=Phone / minor=Smartphone / service=Telephony\|Audio → 実効 ≈ `0x60020C`) | §4.1 step 10 のとおり。実装は `ESP_BT_COD_SRVC_TELEPHONY \| ESP_BT_COD_SRVC_AUDIO`。SLC 不成立時に literal `0x20040C` へ切替 |
| U-06 | WBS(mSBC)必須だった場合(R3) | **Phase 2**。**B8 で esp32 でも WBS(mSBC ソフトデコード)ビルド可を確認済み** | Phase 1 は CVSD 固定。Phase 2 は `CONFIG_BT_HFP_WBS_ENABLE=y` + 16kHz 経路 + I2S 16kHz |
| U-07 | 内蔵 8bit DAC 代替(§4.2) | **Phase 2**(`dac_continuous` ドライバ経路) | Phase 1 は PCM5102A のみ |
| U-08 | 絶縁トランス無し構成でのハム(R6) | **ハード側**。設計変更なし | ユーザー実機 T9 で評価 |

---

## 12. 参照実装ファイル一覧(解析済み)

| ファイル | 用途 |
|---|---|
| `~/esp/esp-idf/examples/bluetooth/bluedroid/classic_bt/hfp_ag/main/main.c` | 起動シーケンス、GAP 設定、`bt_hf_hdl_stack_evt` |
| `.../hfp_ag/main/bt_app_hf.c` | HF コールバック全 `ESP_HF_*_EVT` 処理、HCI データパス(recv/send cb, データタスク) |
| `.../hfp_ag/main/bt_app_core.c` | `bt_app_work_dispatch` ワークキュー(BtAppT) |
| `.../hfp_ag/main/app_hf_msg_set.c` | console コマンドでの `audio_connect`/`answer_call`/`out_call`/`ciev_report` 呼び方(削除対象だが API 用法の参考) |
| `.../hfp_ag/sdkconfig.defaults` | BT classic + HFP-AG + MAX_SYNC_CONN=1 の基本設定 |
| `.../hfp_ag/sdkconfig.ci.vohci` | **公式 Voice-over-HCI リファレンス**。§5.1 のベース。`BT_HFP_AUDIO_DATA_PATH_HCI=y` + `MAX_SYNC_CONN=1`(controller SCO path・WBS は未設定) |
| `~/esp/esp-idf/components/bt/host/bluedroid/btc/profile/std/hf_ag/btc_hf_ag.c:355-361` | `btc_hf_init` が `BTM_SCO_HCI_INCLUDED` 時に `esp_bredr_sco_datapath_set(ESP_SCO_DATA_PATH_HCI)` を実行時呼び出し |
| `build_probe_espidf/`(DEBUGGER 作成) | リンク性検証済み(app ~760KB / 空き 51%)。ISS-009〜011 の根拠。**正式構成は本設計書 §3/§5.1 を正とする**(build_probe はそのまま流用せず参考のみ) |
| `~/esp/esp-idf/components/bt/host/bluedroid/api/include/api/esp_hf_defs.h` | rec_cb/send_cb 型は v5.4 系(`void(*)(const uint8_t*,uint32_t)` / `uint32_t(*)(uint8_t*,uint32_t)`)。v6 系と異なる(N5) |
| `~/esp/esp-idf/components/bt/host/bluedroid/api/include/api/esp_hf_ag_api.h` | AG API 全シグネチャ、recv/send cb 型、`AUDIO_STATE_EVT` param(preferred_frame_size 不在を確認) |
| `.../api/esp_gap_bt_api.h` | `esp_bt_cod_t`、scan mode、IO cap、ボンド管理、GAP イベント |
| `.../api/esp_hf_defs.h` | `ESP_HF_IND_TYPE_*`、call/callsetup status enum |
| `~/esp/esp-idf/components/esp_driver_i2s/include/driver/i2s_std.h` / `i2s_common.h` | 新 I2S ドライバ API、`i2s_channel_write` シグネチャ |
| `~/esp/esp-idf/components/bt/controller/esp32/Kconfig.in` | `BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI` / `BTDM_CTRL_BR_EDR_MAX_SYNC_CONN` |
| `~/esp/esp-idf/components/bt/host/bluedroid/Kconfig.in` | `BT_HFP_AUDIO_DATA_PATH_HCI` / `BT_HFP_WBS_ENABLE` |
| espressif/esp-idf#4967(BT-776)+ #6218 / #6503 / #28057 | HCI SCO データパスの不具合履歴と回避策(§2.7) |

---

## 13. CR-010 SENA SRL3 ペアリング対応

- 作成: DESIGNER / 2026-09-30 / 状態: **設計(BOSS 確認待ち、CODER 未着手)**
- 対象: `main/app_main.c` / `main/bt_hf_ag.c` / `main/app_config.h` / `sdkconfig.defaults`(コメントのみ)/ `dummy_intercom_spec.md`

### 13.1 変更要求
SENA SRL3.0 のユニバーサルインターコム・ペアリングから dummycom が検出・ペアリングできない。PC(Windows 等)からはペアリングできるが、ペア番号(SSP 数値確認/パスキー)を求められる。インカムで一般的な方式(レガシー PIN 0000)にしたい。

### 13.2 現行解析(コード・IDF v5.4 での確認結果)

| # | 現行 | 根拠・所見 |
|---|---|---|
| 1 | `app_main.c:82` `BT_BLUEDROID_INIT_CONFIG_DEFAULT()` → `.ssp_en = true`(`esp_bt_main.h:35-38`) | **SSP は常時有効の設定で起動している** |
| 2 | `bt_hf_ag.c:143-144` `esp_bt_gap_set_security_param(IOCAP, NONE)` → SSP Just Works | 相手が DisplayYesNo の PC だと Windows は確認ダイアログ(場合によりコード表示)を出す。SRL3 側が SSP 対応なら同様に数値/確認を伴う経路に入りうる |
| 3 | `bt_hf_ag.c:146-147` `esp_bt_gap_set_pin(VARIABLE,"0000")` + `PIN_REQ_EVT` で "0000" 返答(437-441) | レガシー経路は既に用意済みだが、**SSP 有効のため相手も SSP 対応なら一度も使われない**(btm_sec.c:5017 `btm_sec_pin_code_request` はコントローラが PIN_CODE_REQ を出したときのみ)|
| 4 | `sdkconfig.defaults:34-36` / 設計書 §5.1 / spec §203 の「`CONFIG_BT_SSP_ENABLED` は v5.4 で廃止、SSP 常時有効」 | **説明が不正確**: Kconfig は確かに存在しない(`grep` で `components/bt` に定義なし、`sdkconfig` にも無し)が、**SSP 有無は実行時の `esp_bluedroid_config_t.ssp_en` で切替可能**。IDF 公式例 `bt_spp_vfs_initiator/main/main.c:323`、`bt_l2cap_server/main/main.c:309` が `bluedroid_cfg.ssp_en = false` を使用。したがって **sdkconfig/sdkconfig.defaults に変更すべき CONFIG は無い**(コメントの訂正のみ) |
| 5 | `controller.c:177-183` | `simple_pairing_supported = コントローラ対応 && get_ssp_enabled()`。`ssp_en=false` なら `HCI Write_Simple_Pairing_Mode(ENABLED)` を**発行しない** → ホストが SSP 非対応として相手に見え、相手は必ずレガシー PIN 認証にフォールバックする(BT Core: 双方が SSP host support のときのみ SSP)|
| 6 | `esp_gap_bt_api.c:325` ほか(set_security_param / ssp_confirm_reply / ssp_passkey_reply) | `ssp_en=false` 時は `ESP_ERR_NOT_SUPPORTED` + ERROR ログ。**`handle_stack_up` の `set_security_param` 呼び出しは削除が必須**(残すと毎起動 ERROR ログ) |
| 7 | `btm_sec.c:5078` | `pin_type == HCI_PIN_TYPE_FIXED` かつ `pairing_disabled` でないとき、**PIN_REQ をアプリに上げずスタックが固定 PIN で即返答**。可変(現行 VARIABLE)だと `ESP_BT_GAP_PIN_REQ_EVT` 経由で BtAppT/BTC の遅延・取りこぼしリスクが残る |
| 8 | `bt_hf_ag.c:87-93` `bt_hf_ag_apply_scan_mode()`:**ピア保存済みなら NON_DISCOVERABLE** | **検出できない有力な原因**。`ESP_BT_GAP_AUTH_CMPL_EVT` 成功で `HFW_SAVE_PEER` が**任意の相手(PC を含む)**を `storage_save_peer` する(`bt_hf_ag.c:423-431, 366-371`)ため、PC で一度でもペアリング/試行が成功すると以降は不可視になり、SRL3 の探索に出てこない。BOND-CLR 3 秒長押し(`leds.c:70`)で復帰する |
| 9 | CoD = Phone/Smartphone/Telephony\|Audio(≈0x5A020C 相当、`app_config.h:33`、`bt_hf_ag.c:137-141`)| 仕様 U-05 で BOSS 決定済み。HFP-AG ロールと整合。SENA の探索フィルタは**不明(推測)**、§13.4 参照 |
| 10 | デバイス名 `DummyIntercom`、SDP は `esp_hf_ag_init()` が自動登録(HFP-AG サービスクラス 0x111F、CoD と独立) | 変更不要 |

### 13.3 関連ファイル
`main/app_main.c:82`、`main/bt_hf_ag.c`(handle_stack_up / bt_gap_cb / apply_scan_mode / handle_conn_state / HFW_SAVE_PEER)、`main/app_config.h`、`main/storage.c`(変更なし。`storage_has_peer` の意味は維持)、`sdkconfig.defaults`(コメント訂正)、`sdkconfig`(変更なし)、`dummy_intercom_spec.md` §ペアリング行・SSP 注記・`app_main.c` 行。

### 13.4 現行フローと問題点 / 変更方針
現行: 起動 → SSP 有効 + IOCAP=NONE + PIN "0000"(VARIABLE)→ ピア無し時のみ discoverable → 任意相手の AUTH_CMPL 成功でピア保存 → 以降 non-discoverable。

**判断 (1) レガシー PIN 化 → 採用**:
- `ssp_en=false`(実行時)+ `esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, "0000")`。SENA を含む多くのインカム/ヘッドセットの既定 PIN は 0000(SENA 実機の挙動は**推測**。SENA が別の PIN を送る場合は認証失敗になる。ログ `auth failed` で確認)。
- 効果: 番号比較・パスキー表示・確認ダイアログが消え、PC からは PIN 入力(0000)のみに変わる。**トレードオフ**: レガシーペアリングは SSP より弱く(固定 PIN・MITM 保護なし)、暗号鍵強度も低い。本機は音声受信専用ダミー機なので許容と判断(BOSS 承認事項、§13.11 U-1)。
- 代替案(不採用): SSP 維持 + `ESP_BT_GAP_CFM_REQ_EVT` 自動承認(現行)。SRL3 側が SSP のユーザー確認を要求する場合に対処できない。

**判断 (2) CoD → 現行(Phone)維持 + 切替マクロを追加(最小変更)**:
- SRL3 は HF として AG に接続する(仕様 §HFP 表)ため AG に相応しい Phone 系 CoD が整合的。AV/Headset 系にすると SRL3 が「ヘッドセット同士」と見なして AG 接続を試みない/誤ったロール選択の懸念(推測)。
- ただし SENA の探索フィルタが CoD 依存かは**確証なし**。実機で「discoverable + PIN 化」でも出ない場合に備え `app_config.h` に `BT_COD_PROFILE`(0=Phone 既定 / 1=AV Handsfree 案)を用意し再ビルドのみで切替できるようにする。Profile 1 = major `ESP_BT_COD_MAJOR_DEV_AV`(4)、minor 0x02(Hands-free Device、IDF 列挙子なしのため数値)、service = `ESP_BT_COD_SRVC_AUDIO | ESP_BT_COD_SRVC_TELEPHONY`。IDF に AV minor 列挙子が無いため `cod.minor` は数値直書き(`BT_COD_MINOR_AV_HANDSFREE 0x01` は Bluetooth Assigned Numbers で minor 6bit フィールド値 0x01=Wearable Headset、0x02=Hands-free。**CODER は Assigned Numbers を確認して値を確定**)。既定は 0 で既存 HFP-AG 動作を変えない。

**判断 (3) discoverable 方針の変更(検出不能の有力原因対策)→ 採用**:
- `bt_hf_ag_apply_scan_mode()` を「**SLC 未接続の間は常に CONNECTABLE + GENERAL_DISCOVERABLE、SLC 接続中は CONNECTABLE + NON_DISCOVERABLE**」に変更。再接続タイマ(ピア保存時)は従来どおり動作。
- 保存ピアが PC など別機器でも SRL3 から探索・接続できる。副作用: 保存ピア切断中は常時見える(spec の「ボンド済みなら不可視」の緩和)。

### 13.5 詳細設計
1. **`app_main.c`**: `bd_cfg.ssp_en = false;` を `esp_bluedroid_init_with_cfg` 前に追加。`app_config.h` に `#define BT_LEGACY_PAIRING 1`(1=`ssp_en=false`、0=従来 SSP)を置き `bd_cfg.ssp_en = !BT_LEGACY_PAIRING;` とすることで巻き戻し容易にする。
2. **`handle_stack_up`**: `BT_LEGACY_PAIRING` 時は `set_security_param` を呼ばない(`#if !BT_LEGACY_PAIRING` で囲む)。`esp_bt_gap_set_pin(BT_PIN_TYPE, 4, pin)`(`BT_PIN_TYPE = ESP_BT_PIN_TYPE_FIXED`)。戻り値を `ESP_LOGW` で確認。CoD 設定は `BT_COD_PROFILE` で分岐。
3. **`bt_gap_cb`**: `PIN_REQ_EVT`(FIXED では通常来ない保険)・`CFM_REQ`/`KEY_NOTIF` は残す(SSP 巻き戻し用、`BT_LEGACY_PAIRING` 時は到達しないだけ)。`AUTH_CMPL` 成功時に相手名 `param->auth_cmpl.device_name` と BDA を INFO ログ出力、失敗時は `stat` に加え BDA もログ(PIN 不一致調査用)。
4. **`bt_hf_ag_apply_scan_mode()`**: 上記判断(3)。SLC 状態は `s_slc`(BtAppT でのみ更新される既存変数)を参照。**呼び出し元は既に BtAppT 内(`handle_stack_up`/`SLC_CONNECTED`/`SAVE_PEER`)**なので新規スレッド問題なし。`SLC_DISCONNECTED` 処理(`s_slc=false` 後)でも `bt_hf_ag_apply_scan_mode()` を呼び、切断後に再び discoverable に戻す。
5. **データ構造**: 追加なし(マクロ 4 個: `BT_LEGACY_PAIRING`, `BT_PIN_CODE`(文字列 "0000"), `BT_COD_PROFILE`, 必要なら `BT_COD_MINOR_AV_HANDSFREE`)。NVS 変更なし。
6. **IF 変更**: 公開関数シグネチャ変更なし(`bt_hf_ag_apply_scan_mode` の意味のみ変更、`bt_hf_ag.h` のコメント更新)。
7. **sdkconfig / sdkconfig.defaults**: **設定行の追加・変更なし**(Kconfig 不在のため)。`sdkconfig.defaults:34-36` のコメントを次のとおり置換(両ファイルはコミット済みだが sdkconfig 本体にはコメント行なしのため実質 `sdkconfig.defaults` のみ編集):
   ```
   # NOTE: There is no CONFIG_BT_SSP_ENABLED in ESP-IDF v5.4. SSP vs legacy pairing is a
   # runtime choice: esp_bluedroid_config_t.ssp_en (app_main.c). CR-010: ssp_en=false,
   # fixed PIN "0000" (SENA SRL3 universal intercom pairing).
   ```
   `sdkconfig` は変更不要(再生成で差分が出ないことを確認)。
8. **ドキュメント同期**(メモリ規約): `dummy_intercom_spec.md` のペアリング行(L171)を「レガシー PIN(固定 0000)。SSP 無効(`ssp_en=false`)」に、L203 の SSP 注記を訂正、L293 の `app_main.c` 行の「SSP=IO_CAP_NONE」を訂正、discoverable 規則、CoD 注記。**spec の rev 更新が必要**(rev 0.7 → 次 rev、改訂表に CR-010 行追加)。本書 §5.1 コメント(L407-408)・§5.7 も同様に訂正。README.md に該当記述があれば同期。

### 13.6 影響範囲
- HFP-AG の SLC/SCO/音声経路(`bt_data_path.c`、`audio_*`)は**無変更**。ペアリング完了後の動作は従来と同じ。
- **既存ボンド(SSP 鍵)**: 鍵種別が異なる場合、旧ボンド相手は再ペアリングが必要になりうる。対策: 初回 CR-010 ファーム書込み後は BOND-CLR 長押しを推奨(§13.10 テスト 0)。
- discoverable 化により、SLC 未接続中は周囲の機器から見える(セキュリティ・利便トレードオフ)。
- HF ユニット(rev 0.8 の 2 台目 ESP32)が別プロジェクトで、AG ユニットに SSP 前提で接続する構成なら、**HF ユニット側 IOCAP/PIN 設定の整合確認が必要**(レガシー PIN 0000 相手にはレガシー PIN で応答すれば通る想定だが未確認。`ssp_en` はローカル設定のため相手が SSP 対応でも本機が非対応ならレガシーにフォールバック)。

### 13.7 リスク
| # | リスク | 対策 |
|---|---|---|
| R-1 | SENA の探索・接続・ロールが**推測**(HF として AG に接続する想定 / ユニバーサルインターコムで CoD フィルタを使うか不明) | `BT_COD_PROFILE` 切替、ログで inquiry/ACL 着信を確認、`auth failed` ログ |
| R-2 | SENA が 0000 以外の PIN(例 1234)を送る | `BT_PIN_CODE` 1 定数で変更可能。将来: VARIABLE + `PIN_REQ_EVT` で複数候補 |
| R-3 | 固定 PIN は「相手が PIN を送る」経路。本機起点の接続(再接続タイマ)時に PIN 要求が来ても FIXED なら自動応答 | 問題なし(btm_sec.c:5078) |
| R-4 | `ssp_en=false` で HFP 動作が変わる懸念 | HFP/SCO はペアリング方式と独立(IDF 公式 SPP/A2DP 例も同設定)。実機 T3 を再確認 |
| R-5 | レガシーペアリングのセキュリティ低下 | 音声受信専用機として許容(U-1)。`BT_LEGACY_PAIRING=0` で戻せる |
| R-6 | 常時 discoverable で他機器が誤って接続 | HFP-AG のため実害小。必要なら後続 CR でボタン長押し(短押し)でペアリング窓を導入 |

### 13.8 CODER への実装指示
| 項目 | 内容 |
|---|---|
| **A. `main/app_config.h`** | `/* ---- Bluetooth ---- */` 節へ追加: `#define BT_LEGACY_PAIRING 1`、`#define BT_PIN_CODE "0000"`、`#define BT_COD_PROFILE 0`(コメントで 0=Phone/1=AV handsfree)。CoD 既存 `BT_COD_MINOR_SMARTPHONE` は維持。 |
| **B. `main/app_main.c:82`** | `esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();` の直後に `bd_cfg.ssp_en = !BT_LEGACY_PAIRING;`。`app_config.h` を include 済みか確認。 |
| **C. `bt_hf_ag.c handle_stack_up`** | 現行 `esp_bt_io_cap_t io...set_security_param` 3 行 → `#if !BT_LEGACY_PAIRING` で囲む。現行 `esp_bt_pin_code_t pin = {'0','0','0','0'}; set_pin(VARIABLE,4,pin)` → `esp_bt_pin_code_t pin; memcpy(pin, BT_PIN_CODE, 4);`(`sizeof(BT_PIN_CODE)-1 == 4` を `_Static_assert`)`esp_err_t e = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin); if (e != ESP_OK) ESP_LOGW(...)`。CoD: `BT_COD_PROFILE == 1` のとき major=`ESP_BT_COD_MAJOR_DEV_AV`, minor=Handsfree 値(Assigned Numbers で確認), service は現行と同じ。 |
| **D. `bt_hf_ag_apply_scan_mode`** | 現行 `storage_has_peer()` 分岐 → `s_slc ? NON_DISCOVERABLE : GENERAL_DISCOVERABLE`(常に CONNECTABLE)。`s_slc` の宣言位置が関数より後なら前方へ移動/前方宣言。`bt_hf_ag.h` コメント更新。 |
| **E. `handle_conn_state`** | `SLC_DISCONNECTED`(または `DISCONNECTED`)で `s_slc=false` 設定後に `bt_hf_ag_apply_scan_mode()` 呼出しを追加(未呼出の場合)。 |
| **F. `bt_gap_cb`** | `AUTH_CMPL_EVT` の成功/失敗ログに BDA と `device_name` を追加。`PIN_REQ_EVT` は残置(保険)。`CFM_REQ`/`KEY_NOTIF` は残置。 |
| **G. `sdkconfig.defaults`** | L34-36 のコメントを §13.5-7 の文面に置換。**CONFIG 行は追加しない**。`sdkconfig` 本体は編集しない(`idf.py build` で差分が出た場合のみ確認しコミット対象を報告)。 |
| **H. docs** | `dummy_intercom_spec.md` を §13.5-8 のとおり更新(rev 更新+改訂表)、README.md の該当記述、`software_design.md` §5.1 の L407-408 を訂正。 |
| 境界条件 | `ssp_en=false` 時に `esp_bt_gap_set_security_param`/`ssp_confirm_reply` を呼ばない(`ESP_ERR_NOT_SUPPORTED`)。`esp_bt_gap_set_pin` は `esp_bluedroid_enable` 後、`handle_stack_up`(STACK_UP 後)内で呼ぶ現行位置を維持。PIN 長 4。 |
| エラー処理 | `set_pin`/`set_cod`/`set_scan_mode` の戻り値を WARN ログ(致命扱いにしない)。 |
| ビルド | `idf.py build` 警告 0 を確認(`_Static_assert`・未使用変数 `io` 警告に注意)。 |

### 13.9 テスト観点(実機。開発環境に SRL3 無し = 未実施として報告)
0. BOND-CLR 長押しで旧ボンド消去後に書込み。
1. 起動ログ: `ssp_en` 無効、`set_pin` OK、`set_security_param` の ERROR ログが**出ない**こと。
2. PC(Windows/Android)から検索→ペアリング: 番号比較/パスキーが出ず、PIN 入力(0000)のみ。
3. SENA SRL3 ユニバーサルインターコム・ペアリングで検出(名前 `DummyIntercom` が出る)→ PIN 自動→ SLC 確立 → SCO 受信(既存 T3 手順)。
4. ピア保存済み(PC 等)・SLC 未接続の状態でも SRL3 から検出できること(判断 3 の検証)。SLC 接続中は不可視。
5. 電源再投入後の自動再接続(`ACL_RECONNECT_MS`)が従来どおり動くこと。
6. 検出不能が続く場合: `BT_COD_PROFILE=1` で再試験し結果を記録。`auth failed` ステータス確認。
7. リグレッション: mSBC/CVSD 切替、キープアライブ、BOND-CLR、LED。

### 13.10 実装後レビュー結果(DESIGNER / 2026-09-30、BOSS 実装の git diff 対象)
対象: `main/app_config.h` / `app_main.c` / `bt_hf_ag.c` / `bt_hf_ag.h`、`sdkconfig.defaults`、`dummy_intercom_spec.md`(rev 0.10)、`README.md`。ビルド・実機は未確認(レビューは静的)。

| 重大度 | 件数 | 内容 |
|---|---|---|
| Critical | 0 | — |
| High | 0 | — |
| Medium | 2 | **M-1** `dummy_intercom_spec.md:318`(§8.1 手順 4)が「PIN 不要、Just Works」のまま(L171・T2 は更新済みで矛盾)。**M-2** spec §5 注記(L203 直下)に「IO capability は `esp_bt_gap_set_security_param` で `ESP_BT_IO_CAP_NONE` に設定」の行が残存(`ssp_en=false` では呼ばれず ERR となる旨と矛盾)。→ 「`BT_LEGACY_PAIRING 0` のときのみ」へ修正 |
| Low | 4 | **L-1** README「初回ペアリング」手順 4 が「以降は非検出状態」のまま(CR-010 で SLC 未接続中は discoverable)。README の参照 spec も `rev 0.6` のまま(現 0.10)。**L-2** README が全面日本語化+コマンド節追加され、CR-010 のスコープ外の大差分(意図的なら可、コミットは分離推奨)。**L-3** AV Handsfree の minor `0x02`(Assigned Numbers: Audio/Video minor 0x01=Wearable Headset / 0x02=Hands-free)は妥当。`esp_bt_cod_t.minor` は 6bit フィールドで実効 CoD 下位は 0x08 になるため、Profile 1 試験時にスキャナで確認。既定 0 のため影響なし。README の DSP 記述「120 Hz HPF」も現行 `AUDIO_HPF_CUTOFF_HZ 30` と不一致(CR-010 以前からの陳腐化)。 **L-4** 設計 §5.1 L407-408 の SSP 記述(本レビューで訂正済み)|

設計との整合確認(問題なし):
- `bd_cfg.ssp_en = !BT_LEGACY_PAIRING`、`set_security_param` が `#if !BT_LEGACY_PAIRING` で囲まれ(`ESP_ERR_NOT_SUPPORTED` ログ回避)、`set_pin(FIXED,4,"0000")` は `_Static_assert` + 戻り値 WARN ログ。IDF 側根拠(controller.c:177-183、btm_sec.c:5078)と一致。
- `apply_scan_mode` は `s_slc`(定義 L61、関数 L87 より前)を参照。呼び出しは BtAppT 内のみ(stack_up / SLC_CONNECTED[`s_slc=true` 後]/ SAVE_PEER / DISCONNECTED[`s_slc=false` 後、L196→追加行])で順序正しい。
- 設計との差分(SLC_DISCONNECTED 側への `apply_scan_mode` 追加、AUTH_CMPL ログへ BDA/名前追加)は設計 §13.8-E/F の範囲内。`ESP_BD_ADDR_STR/HEX` と `auth_cmpl.device_name` は v5.4 ヘッダに存在を確認。
- `sdkconfig.defaults` はコメントのみ変更、設定行の変更なし(設計どおり)。`sdkconfig` は変更なし。`BT_LEGACY_PAIRING 0` で旧動作へ戻る。
- 既知の残存挙動(仕様どおり、指摘ではない): `HFW_SAVE_PEER` は PC 等の任意相手も再接続対象ピアとして保存する。SLC 未接続中は常時 discoverable のため PC で保存されても SRL3 から検出可能。実機試験は未実施。

判定: **合格**(再確認 2026-09-30: M-1 / M-2 / L-1 は spec L318・L204・README L15,73 で修正を確認。残 Low: README L70「最初のボンドが完了するまで検出可能」が軽微に陳腐化、README DSP 120Hz 記述、README 大差分のコミット分離。ファーム側に修正必須項目なし)。

### 13.11 未解決事項
| # | 事項 | 提案 |
|---|---|---|
| U-1 | レガシー PIN 化によるセキュリティ低下の承認 | 承認推奨(`BT_LEGACY_PAIRING` で戻せる) |
| U-2 | SENA SRL3 の実探索条件(CoD フィルタ有無)・接続ロール・PIN 値は**未確認(推測)** | 実機ログで確定。当面 Phone CoD + 常時 discoverable |
| U-3 | 常時 discoverable は spec「ボンド済みは不可視」の緩和 — spec 変更承認 | 承認推奨。不可なら「BOND-CLR 後のみ」に戻す代わりに PC ペア実績があると SRL3 不可視、と説明 |
| U-4 | HF ユニット(rev 0.8)側のペアリング設定整合 | BOSS がユニット側コードを確認(本 CR の変更範囲外) |
| U-5 | `dummy_intercom_spec.md` の rev 更新(§13.5-8)が必要 | CODER 実装時に同期 |
| U-6 | ISS-009 記述訂正(過去 rev の記録は保持し §13.2-4 に訂正を追記済み) | — |


---

## 14. CR-011 HF 役追加(SRL3 ユニバーサルインターコム対応)

- 作成: DESIGNER / 2026-09-30 / 状態: **設計(BOSS 確認待ち、CODER 未着手)** / ソース変更なし
- **前提の注意**: 「SRL3 が SDP で HF/HSP を探して切断している」は**仮説であり未検証**。本設計はその仮説が正しい場合に備えるもので、§14.7 の検証を実装より先に行うことを推奨する。

### 14.1 変更要求と実機ログの事実
- `BT_COD_PROFILE=1`(AV Handsfree)にすると、SRL3 ユニバーサルインターコムが dummycom へ ACL 接続(`hcif conn complete st 0`)するようになるが、**約 0.4 秒後に SRL3 側から切断(disc reason 0x13 = Remote User Terminated)**。認証・PIN・SLC は一切発生せず、数秒おきに繰り返す。PC からのレガシー PIN ペアリングは成功済み(CR-010 は有効)。
- **事実から言えること**: (a) CoD が探索・接続の**フィルタ**になっている(CR-010 §13.4 判断(2) の推測が実機で裏付けられた)。(b) 接続後の 0.4 秒で SRL3 が「自分の期待と合わない」と判断して自発的に切っている(0x13)。認証開始前なので、この 0.4 秒で行われる処理は SDP 問い合わせ(または単なる ACL 上の能力確認)の可能性が高い(**推測**)。
- **仮説 H1**: SRL3 はユニバーサルインターコムで AG(電話側)として振る舞い、相手に HFP-HF(0x111E)または HSP-HS(0x1108)の SDP レコードを期待する。dummycom は AG(0x111F)のみ公開しているので不一致で切断。
- 代替仮説(未排除): **H2** SRL3 が期待するのは HF/HS だが、探すのは SDP ではなく CoD/名前・EIR の UUID リスト(EIR に 0x111F しか無い)。**H3** SRL3 が AG として SLC を張る前に固定の別ロール(HSP 専用)を要求する。**H4** 認証要求(PIN/SSP)を出す前に切っており、SRL3 側が dummycom の LMP/SSP 機能に不満(可能性は低いが、`ssp_en=false` 側の事象)。H1〜H3 は §14.7 の実験で切り分ける。
- 目的: メッシュ会話音声を録音するため、**dummycom は HF 役で SCO を受信**し既存のアナログ出力経路へ流す。従来の電話ペアリング(SRL3=HF, dummycom=AG)も維持する。

### 14.2 調査結果(ESP-IDF v5.4、`~/esp/esp-idf/components/bt/host/bluedroid`)

**(1) AG と HF client の共存**
| 事項 | 根拠 |
|---|---|
| Kconfig 上は `BT_HFP_CLIENT_ENABLE` と `BT_HFP_AG_ENABLE` を**両方 y にできる**(独立 config、`depends on BT_HFP_ENABLE` のみ) | `Kconfig.in:142-150` |
| **同時動作は不可**(公式明記) | `Kconfig.in:134-140` `BT_HFP_ENABLE` の help: "Hands Free Unit and Audio Gateway can be included simultaneously but they cannot run simultaneously due to internal limitations." |
| 具体的な内部制約の実体は BTA/BTC ソース内に明記されていない(SCO 制御・SDP・RFCOMM が AG/HF で別 CB を持つが SCO を共有 = `bta_hf_client_sco.c` / `bta_ag_sco.c` が同じ `btm_cb` の SCO を操作)。**推測**: SCO 共有・`BTA_HFP` サービス ID の排他が理由 | 実体未確認と明記 |
| API は独立: `esp_hf_ag_init/deinit` と `esp_hf_client_init/deinit`、コールバック/データコールバックも別(`esp_hf_client_register_data_callback`, `esp_hf_client_incoming_data_cb_t(const uint8_t*, uint32_t)`、`outgoing_data_cb_t(uint8_t*, uint32_t)` は AG と**同一シグネチャ**) | `esp_hf_client_api.h:287,305,680,707` |
| `btc_hf_client_init` は `esp_bredr_sco_datapath_set(HCI)` を実行、SDP レコードは `BTA_HfClientEnable` 系で登録 | `btc_hf_client.c:168-190`, `bta_hf_client_sdp.c:75` |
| **結論**: 同時 init は不可(公式)。**1 起動につき 1 役だけ init する**。実行中の deinit→別役 init(動的切替)は IDF 上でサポート明記も検証例もなく**不採用**(リスク大)。役切替は再起動を伴う。 |

**(2) HF 役の SCO 受信**: HF client も同じ HCI データパス(`BT_HFP_AUDIO_DATA_PATH_HCI=y`, `BT_HFP_WBS_ENABLE=y` は AG と共通 Kconfig)。`ESP_HF_CLIENT_AUDIO_STATE_CONNECTED` = CVSD 8k、`..._CONNECTED_MSBC` = mSBC 16k(`esp_hf_client_api.h:33-36`)。`recv_cb` は AG の既存 `recv_cb`(`bt_data_path.c`)とシグネチャ・意味が同一 → **`audio_i2s_write` へ流す既存コードをそのまま再利用できる**。アップリンク(`send_cb`)は無音(0 埋め)で同じ。`outgoing_data_ready` の呼び先のみ `esp_hf_client_outgoing_data_ready()` に変わる。

**(3) 既存資産(流用可否)**: リポジトリ本体(`/home/subcon/SOURCE_CODE/dummycom`)に `hf_unit/` は**無い**。実体は兄弟ディレクトリ **`/home/subcon/SOURCE_CODE/dummycom.botu/hf_unit/`**(独立 ESP-IDF プロジェクト、rev 0.8/0.9 の HF ユニット、`bt_hf_client.c` 370 行 / `bt_data_path.c` / `adc_capture.c` ほか)。`sdkconfig.defaults` は `BT_HFP_CLIENT_ENABLE=y` + `BT_HFP_AG_ENABLE=n`。**流用可**: `bt_hf_client.c`(GAP・HF イベント・keepalive/reconnect・factory reset パターンが AG 側と同構造)を移植元にでき、HF client API の実使用例として信頼できる(ただし CR-010 以前の SSP/VARIABLE PIN/旧 scan mode のため CR-010 の 3 点を再適用要)。`bt_data_path.c` は HF ユニットでは ADC 送信用で recv が no-op のため**そのままは不可**(dummycom の recv→I2S 版をベースにする)。`adc_capture.c` は不要。**副次的発見**: `hf_unit` ビルドを dummycom 基板にそのまま書き込むだけで H1 を実機検証できる(§14.7 実験 A)。

**(4) SRL3 の SDP 問い合わせ確認方法**: §14.7。SDP トレース(`CONFIG_BT_LOG_SDP_TRACE_LEVEL_*`)は接続イベントのみで**UUID は出ない**(`sdp_server.c` のトレースは EVENT/DEBUG で connection・config のみ)。HCI ログ(`CONFIG_BT_HCI_LOG_DEBUG_EN=y`、`bt_hci_log_hci_data_show()`)は controller→host の ACL(=SRL3 からの SDP 要求 PDU)を 16 進で保持するため、**UUID(0x111E/0x1108/0x111F 等)を目視で判読可能**。

### 14.3 案の比較
| 案 | 内容 | 長所 | 短所 |
|---|---|---|---|
| **A ビルド時マクロ** `BT_ROLE`(AG/HF) | 両役コンパイル(Kconfig 両 y)、`app_main` が起動時に 1 役のみ init | 最小変更・安全(1 起動 1 役)・実機検証に最短 | 役変更に再書込が必要 |
| **B NVS + ボタン切替** | 役を NVS 保存、GPIO14 短押しでトグルして再起動 | 現場で切替可能(従来ペアリングと共存) | 誤押下対策/LED 表示/既存 BOND-CLR 長押しとの整合が要る |
| C 動的 deinit/init | 実行中に役を入替 | 再起動不要 | IDF 未保証・SCO/SDP 残留リスク。**不採用** |
| D 両役同時 init | — | — | 公式に不可。**不採用** |
| E SDP レコード切替のみ | AG のまま HF レコードを追加登録 | — | `esp_hf_client` を使わないと SLC/SCO が処理できない。生 SDP 追加では意味がない。**不採用** |

**推奨: A → B の段階導入。Phase 1 = 案 A(本設計の実装指示は Phase 1 を詳述)で H1 を実機検証。H1 が支持されたら Phase 2 = 案 B(NVS+短押し)を追加。** 実験 A(§14.7、hf_unit を書込むだけ)を実装前に実施すれば、Phase 1 の前に H1 の一次確認ができる。

### 14.4 変更方針と詳細設計(Phase 1 = 案 A)
1. **役の選択**: `app_config.h` に `#define BT_ROLE_AG 0`, `#define BT_ROLE_HF 1`, `#define BT_ROLE BT_ROLE_AG`(既定=従来動作。SRL3 ユニバーサル試験時のみ `BT_ROLE_HF` へ変更してビルド)。`app_main` は `#if BT_ROLE == BT_ROLE_HF bt_hf_client_start(); #else bt_hf_ag_start();` の**どちらか一方のみ**呼ぶ(同時 init 禁止)。
2. **`main/bt_hf_client.c/.h`(新規、`dummycom.botu/hf_unit/main/bt_hf_client.c` を移植)**:
   - 移植元からの変更: `adc_capture` 依存を削除。`AUDIO_STATE_CONNECTED[_MSBC]` で `audio_i2s_set_input_rate(msbc ? 16000 : 8000)`(AG 側 `bt_hf_ag.c:230` 相当)+ `bt_data_path_on_audio_connected()`、`DISCONNECTED` で `bt_data_path_on_audio_disconnected()`。
   - CR-010 の再適用: GAP は `ssp_en=false` 前提(`set_security_param` 呼ばない)、`esp_bt_gap_set_pin(FIXED,"0000")`、scan mode = SLC 未接続中は常時 discoverable(`bt_hf_ag_apply_scan_mode` と同ロジックの `bt_hf_client_apply_scan_mode`)、AUTH_CMPL ログに BDA/名前。
   - CoD(HF 役): major=AV、minor=Hands-free(0x02)、service=`AUDIO | RENDERING`(実効 ≈ 0x240408、一般的なヘッドセット/ハンズフリーの CoD)。`BT_COD_PROFILE` は AG 役専用のまま。AG 役のマクロ定義は変更しない。
   - 接続動作: SLC_CONNECTED → `esp_hf_client_connect_audio(s_peer)` を即時 1 回 + keepalive(3 s)、切断時 reconnect タイマ(5 s)で `esp_hf_client_connect(peer)`。**SRL3(AG)がユニバーサルインターコム中のみ SCO を張る場合、`connect_audio`(AT+BCC)は拒否/失敗ログが繰り返される可能性がある(未検証)**。keepalive 失敗は WARN 止まりで致命扱いにしない。
   - HF client イベントは hf_unit と同じく制御系のみ処理、指示系(CIND 等)はログのみ。
3. **`bt_data_path.c/.h`**: `bt_data_path_init()` にロール分岐を追加(`#if BT_ROLE == BT_ROLE_HF` で `esp_hf_client_register_data_callback(recv_cb, send_cb)`、それ以外は現行 `esp_hf_ag_register_data_callback`)。`tx_ready_timer_cb` も `esp_hf_client_outgoing_data_ready()` / `esp_hf_ag_outgoing_data_ready()` を分岐。`recv_cb`(I2S 書込み・RMS)・`send_cb`(無音、`len` 厳守)は**共通**。`#include` も分岐。
4. **`leds.c`**: `bt_hf_ag_request_factory_reset()` を role 非依存のラッパへ(`bt_role_request_factory_reset()` を `app_main.c` か小さな `bt_role.h` に置き、内部で `#if BT_ROLE` により `bt_hf_ag_request_factory_reset`/`bt_hf_client_request_factory_reset` を呼ぶ)。
5. **`app_main.c`**: BT 初期化(Bluedroid、`ssp_en`)は共通。`esp_hf_ag_*`/`esp_hf_client_*` を role 別に呼ぶ以外は変更なし。
6. **sdkconfig / sdkconfig.defaults(両ファイル編集、コミット済み)**:
   - `sdkconfig.defaults:30` `CONFIG_BT_HFP_CLIENT_ENABLE=n` → `CONFIG_BT_HFP_CLIENT_ENABLE=y`。`CONFIG_BT_HFP_AG_ENABLE=y` は維持(`sdkconfig.defaults:16`)。コメント「Both compiled in; only one is init'd at runtime (IDF: cannot run simultaneously)」を追記。
   - `sdkconfig:486` `# CONFIG_BT_HFP_CLIENT_ENABLE is not set` → `CONFIG_BT_HFP_CLIENT_ENABLE=y`。`sdkconfig` は `idf.py reconfigure`/build で派生行(`HFP_CLIENT` 関連の自動生成行、`sdkconfig:2199` の `# CONFIG_HFP_CLIENT_ENABLE is not set` 等 rename 関連)が更新されるので**手編集ではなく `idf.py reconfigure` 後の差分をコミット**し、`git diff sdkconfig` に無関係な変更が無いことを確認。
   - それ以外の CONFIG は不変(`BT_HFP_AUDIO_DATA_PATH_HCI=y`, `BT_HFP_WBS_ENABLE=y`, `BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1`)。
   - 診断ビルド時のみ(常設しない): §14.7 の `CONFIG_BT_HCI_LOG_DEBUG_EN=y` / `CONFIG_BT_HCI_LOG_DATA_BUFFER_SIZE=20` / `CONFIG_BT_LOG_SDP_TRACE_LEVEL_EVENT=y`(sdkconfig.defaults へは入れず `sdkconfig.hcilog` 等の別ファイル、または一時編集)。
7. **データ構造/IF 変更**: `BT_ROLE` マクロ、`bt_hf_client_start/apply_scan_mode/request_factory_reset`(新規公開)、`bt_data_path` の内部分岐のみ。NVS フォーマット変更なし(Phase 1)。既存 `bt_hf_ag.c` は無変更(`bt_hf_ag_request_factory_reset` は残す)。
8. **Phase 2(案 B、H1 支持後)**: NVS キー `role`(u8)、`storage_get_role/set_role`、`app_main` が `BT_ROLE` マクロ→NVS 値へ置換(未設定なら従来 AG)。GPIO14 の短押し(離した時に 50 ms〜1.5 s)で `storage_set_role(!role)`+ LED 2 回点滅 + `esp_restart()`。3 s 長押し(BOND-CLR)は現行どおり。役ごとに保存ピア(NVS `peer_bda`)を別キー(`peer_bda_ag` / `peer_bda_hf`)にするか、共有するかは BOSS 判断(§14.10 U-2)。

### 14.5 影響範囲
- **AG 役(既定)の動作は不変**: `BT_ROLE=BT_ROLE_AG` のとき init されるのは AG のみ。HF client のコードは Kconfig で「コンパイルされる」が init されない。app サイズは増加(HF client 分、現状 ~723KB / factory 0x1E0000 = 約 1.9MB で余裕)。
- HF 役では SCO 受信 → 既存の `bt_data_path.c recv_cb` → `audio_i2s_write` → `audio_dsp` → PCM5102A の**全経路が共通**。`audio_i2s.c`/`audio_dsp.c` は無変更。
- 保存ピア/ボンド: 役ごとに相手・役割が異なるため、役切替時は **BOND-CLR 推奨**(リンクキー自体は相手 BDA 単位で有効だが、SRL3 がユニバーサルインターコム用に新規ペアリングを要求する可能性あり)。
- ドキュメント: spec 更新が必要(rev 0.11、§HFP 表に「HF 役(オプション、SRL3 ユニバーサルインターコム用、ビルド時 `BT_ROLE`)」、CoD 表に HF 役 CoD、§8.1 に HF 役手順、T 系に HF 役試験、改訂表)。README 更新(役切替手順)。memory の `build-and-config` 規約どおり `sdkconfig`/`sdkconfig.defaults` 同期。

### 14.6 リスク
| # | リスク | 対策 |
|---|---|---|
| R-1 | **H1 が誤り**(SRL3 の切断理由は別: H2/H3/H4) → HF 役を実装しても解決しない | 実装前に §14.7 の実験 A/B で切り分ける |
| R-2 | SRL3(AG 役)が HSP のみ要求する場合、IDF Bluedroid は HSP-HS(0x1108)のサーバ役を `esp_hf_client` で提供しない(HSP は AG 側 `BTA_HSP_SERVICE_ID` のみ確認)→ 解決不可 | 実験 B の SDP UUID で判定。HSP のみと判明したら Bluedroid 改造(パッチ)か別スタックが必要と BOSS 報告 |
| R-3 | HF 役の `connect_audio`(AT+BCC)を SRL3 が拒否/無視(インターコム音声時のみ SCO を張る) | keepalive は WARN 止まり。SCO が来るのを待つ受動動作でも可(要実機) |
| R-4 | HF 役の WBS(mSBC)ネゴ: SRL3 が LC3-SWB を提示した場合の HF client パーサ挙動(AG 側で `AT+BAC` パーサバグがあった、`patches/`)。HF client は `+BCS` を受ける側で影響は小さい想定だが**未確認** | 実機ログの codec を確認、必要なら `WBS_ENABLE=n` で CVSD 固定 |
| R-5 | 両役コンパイルによるフラッシュ/RAM 増(ヒープ)。同時 init しないため RAM 影響は HF client の静的 CB 分のみ | ビルドサイズ確認、`esp_get_free_heap_size` ログ |
| R-6 | Phase 2 の短押しトグル誤操作 | LED 確認表示、3 s 長押しと区別、`BT_ROLE_TOGGLE_ENABLE` マクロで無効化可 |
| R-7 | 「同時動作不可」の実体未確認のため、将来の IDF 更新で挙動が変わる | 1 起動 1 役の設計で回避 |

### 14.7 SRL3 の SDP 問い合わせ確認方法(仮説 H1 の検証手順)
- **実験 A(最短、コード変更なし)**: `~/SOURCE_CODE/dummycom.botu/hf_unit` をビルドして dummycom 基板へ書込み(ADC 入力は未接続で可、SCO 送信は無音/欠落だが SLC 確立の有無は見える)。SRL3 のユニバーサルインターコム・ペアリングで、① ACL が 0.4 秒で切れなくなるか、② `connection state: SLC_CONNECTED` まで進むか、③ SCO(`audio state: connected`)が来るか、を確認。**進めば H1(SRL3=AG 期待)がほぼ確定**。進まない場合は H2〜H4。注意: hf_unit は旧 SSP/VARIABLE PIN/旧 scan mode なので、ペアリング関連で別要因が出うる点を割り引く(SRL3 は SSP Just Works に IO_CAP_NONE で応じるかもしれない)。
- **実験 B(SDP の UUID を直接見る、AG ビルドのまま)**: 診断ビルドで `CONFIG_BT_HCI_LOG_DEBUG_EN=y`、`CONFIG_BT_HCI_LOG_DATA_BUFFER_SIZE=20`(20 KB)、`CONFIG_BT_LOG_SDP_TRACE_LEVEL_EVENT=y` を追加(`sdkconfig.defaults` へは入れず別ファイル/一時編集)。`bt_hf_ag.c` の `DISCONNECTED` 処理の中(**診断ビルドのみ、`#ifdef CONFIG_BT_HCI_LOG_DEBUG_EN`**)で `bt_hci_log_hci_data_show()`(`hci_log/bt_hci_log.h`)を 1 回呼ぶ。出力された C2H ACL の中から L2CAP CID → SDP PDU を探し、**UUID パターン `19 11 1E`(HFP-HF) / `19 11 08`(HSP-HS) / `19 11 1F`(HFP-AG) / `19 11 12`(HSP-AG)** を確認する(SDP Service Search/Attribute Request の UUID は 16bit なら `0x19` + UUID の 3 バイト形式)。SDP トレースは「SDP チャネルが開いたか(`Rcvd L2CAP conn ind`)」の確認用。
- **実験 C(補助)**: ペアリング時のログに `ESP_BT_GAP_DISC_RES_EVT` は出ないため、SRL3 の CoD/名前/EIR は本機からは見えない。SRL3 のペアリング画面上で表示される名称の変化(CoD が AV になると探索に出る)を記録。
- どの実験でも結果(SRL3 が探した UUID)を §14.11 に追記して仮説を確定/棄却する。

### 14.8 CODER への実装指示(Phase 1、BOSS 承認後)
| 項目 | 内容 |
|---|---|
| A. `main/app_config.h` | `BT_ROLE_AG 0`/`BT_ROLE_HF 1`/`BT_ROLE BT_ROLE_AG` を追加(コメント: HF 役は SRL3 ユニバーサルインターコム用。同時 init 不可のため 1 起動 1 役)。HF 役 CoD 用に `BT_COD_HF_SERVICE (ESP_BT_COD_SRVC_AUDIO | ESP_BT_COD_SRVC_RENDERING)` は `bt_hf_client.c` 内で `esp_gap_bt_api.h` のシンボルから組む。 |
| B. `main/bt_hf_client.c/.h`(新規) | `~/SOURCE_CODE/dummycom.botu/hf_unit/main/bt_hf_client.c` を移植。§14.4-2 の変更(adc 依存削除、`audio_i2s_set_input_rate`、CR-010 の 3 点再適用、CoD、apply_scan_mode、AUTH_CMPL ログ)。公開: `bt_hf_client_start()`, `bt_hf_client_apply_scan_mode()`, `bt_hf_client_request_factory_reset()`。`SLC_DISCONNECTED` 時にも `apply_scan_mode` を呼ぶ。`esp_bt_gap_register_callback` は `bt_hf_client_start` 内(AG と排他なので二重登録なし)。 |
| C. `main/bt_data_path.c/.h` | §14.4-3。`#if BT_ROLE == BT_ROLE_HF` で登録 API と `outgoing_data_ready` 先を切替。`recv_cb`/`send_cb`/RMS は共通。 |
| D. `main/leds.c` + 新規小ヘッダ | `bt_hf_ag_request_factory_reset()` → 役非依存ラッパ。 |
| E. `main/app_main.c` | 役に応じて `bt_hf_client_start()` または `bt_hf_ag_start()` のどちらか一方のみ。 |
| F. `main/CMakeLists.txt` | `bt_hf_client.c` を SRCS に追加(既存 SRCS 記述方式に合わせる)。 |
| G. `sdkconfig` / `sdkconfig.defaults` | §14.4-6。`idf.py reconfigure` で `sdkconfig` を更新し差分を確認。 |
| H. docs | spec rev 0.11(§14.5)、README(役切替の手順)、設計書 §14.11。 |
| 境界条件 | HF 役でも `esp_hf_ag_*` を呼ばない(未 init で `INVALID_STATE`)。逆も同様。HF 役では `BT_COD_PROFILE` を使わない(HF 役 CoD 固定)。 |
| エラー処理 | `connect`/`connect_audio` 失敗は WARN。`register_data_callback` 失敗は ERROR ログ+`fatal_blink` 相当(既存 `bt_data_path_init` の扱いに合わせる)。 |
| ビルド | `idf.py build` 警告 0。app サイズを記録。AG 役ビルド(既定)で従来ログ(`HFP-AG ready`)が変わらないこと。 |

### 14.9 テスト観点(実機。SRL3 実機は開発環境に無く**未検証**)
0. (実装前)§14.7 実験 A/B で H1 を確認。
1. `BT_ROLE_AG`(既定)ビルド: 従来の電話ペアリング・SLC・SCO(CVSD/mSBC)が CR-010 直後と同じ。回帰なし。
2. `BT_ROLE_HF` ビルド: 起動ログ `HFP-HF ready`、HFP-AG 系ログが出ない。PC/スマホから HF として見える(PIN 0000)。
3. SRL3 ユニバーサルインターコム: ACL が 0.4 秒で切れず SLC_CONNECTED → SCO → 音声が PCM5102A 出力(既存 T3〜T5 の HF 役版)。CVSD/mSBC どちらか、`SCO PCM … Hz` ログと `audio_i2s_set_input_rate` の整合。
4. SRL3 の電源断/復帰での自動再接続、keepalive/`connect_audio` の失敗ログの頻度。
5. 役を変えて書込み直した後の BOND-CLR、既存ボンド残存時の挙動。
6. 長時間(30 分)で SCO 切断・ジッタ underrun の確認(T6 相当)。
7. Phase 2: 短押しトグル、誤押下、再起動後の役維持、BOND-CLR との区別。

### 14.10 未解決事項
| # | 事項 | 提案 |
|---|---|---|
| U-1 | H1(SDP/役割)は**未検証**。H2〜H4 の可能性 | 実験 A(hf_unit 書込み)→ B(HCI ログ)の順で確認してから実装 |
| U-2 | Phase 2 でピア保存を役ごとに分けるか | 分ける(`peer_bda_ag`/`peer_bda_hf`)を推奨。BOSS 判断 |
| U-3 | SRL3(AG)が HSP のみ要求する場合(R-2)は Bluedroid で対応不能 | 実験 B で UUID を確認、該当時は BOSS へ別途相談 |
| U-4 | HF 役の SCO 開始主体(SRL3 が張るのか、本機の `connect_audio` が必要か) | 実機ログで確認、keepalive の要否を確定 |
| U-5 | `dummycom.botu` と本リポの関係(hf_unit の正規の置き場所) | BOSS が確認。必要なら hf_unit を本リポへ取り込むか、参照方式を確定 |
| U-6 | 本ドキュメントの spec rev(0.11)採番 | 実装時に確定 |

### 14.11 実装後レビュー結果(DESIGNER / 2026-09-30、BOSS 実装の git diff 対象)
対象: `main/app_config.h` / `bt_hf_client.c/.h`(新規)/ `bt_role.h`(新規)/ `bt_data_path.c` / `leds.c` / `app_main.c` / `CMakeLists.txt`、`sdkconfig`・`sdkconfig.defaults`、spec rev 0.11、README、`hf_unit/`(コピー)。静的レビューのみ(BOSS 報告: AG/HF 両ビルド成功。実機未検証)。

| 重大度 | 件数 | 内容 |
|---|---|---|
| Critical | 0 | — |
| High | 0 | — |
| Medium | 2 | **M-1** `app_config.h` の `BT_ROLE` が **`BT_ROLE_HF`** で入っているが、README(「`BT_ROLE_AG`(既定)」)・設計 §14.4-1(既定=AG、従来動作維持)と不一致。検証用に HF にしているなら、コミット前に AG へ戻すか、README/設計/spec に「現在 HF ビルドで検証中」と明記して整合させる(このままコミットすると既定挙動が「電話ペアリング=AG」から変わる)。**M-2** spec rev 0.11 は改訂表の行のみ追加で、本文が未更新(§14.5 要求: HFP 表の HF 役、CoD 表の HF 役 CoD ≈0x240408、§8.1 の HF 役手順、T 系の HF 役試験、`app_main.c` 行の `esp_hf_ag_init`(役により `esp_hf_client_init`))。改訂表だけ 0.11 で本文が AG のみだと rev の意味が食い違う |
| Low | 4 | **L-1** `hf_unit/`(268KB、untracked)は `dummycom.botu/hf_unit` のコピー(build 除く)。旧 SSP/VARIABLE PIN のまま(CR-010 未適用)で、本リポに取り込む意図(参照用/アーカイブ/実験 A 用)を README か設計に 1 行明記、`.gitignore` か明示 add のいずれかを決める。**L-2** `sdkconfig.old` が変更されている(reconfigure 副産物)。コミット対象から除外推奨。**L-3** README「初回ペアリング」節は AG(phone pairing)前提の文面のまま。HF 役(ユニバーサルインターコム)の手順(SRL3 側のペアリング操作、期待ログ `connection state: SLC_CONNECTED` / `audio state`)を役切替節に 1〜2 行追記すると実験者が迷わない。**L-4** `bt_hf_client.c` 冒頭コメントの参照 `software_design.md 13.5.7 / 13.4` は hf_unit 由来の節番号(本設計書の該当節ではない)。「14 / CR-011」のみに直すと混乱がない |

設計との整合確認(問題なし):
- 1 起動 1 役: `bt_role_start()` が `BT_ROLE` に応じ `bt_hf_client_start()` か `bt_hf_ag_start()` のどちらか一方のみ(`bt_role.h`)。HF 役で `esp_hf_ag_*` は呼ばれない(`bt_hf_ag.c` は未使用コードとしてリンクのみ)。
- `bt_data_path.c`: 登録 API(`esp_hf_client_register_data_callback`)と `esp_hf_client_outgoing_data_ready` が `#if BT_ROLE == BT_ROLE_HF` で分岐、`recv_cb`/`send_cb`/RMS は共通(`send_cb` は `len` 厳守・無音)。
- `bt_hf_client.c`: CR-010 の 3 点(`set_security_param` を `#if !BT_LEGACY_PAIRING` でガード、`FIXED` PIN "0000"+`_Static_assert`、SLC 未接続中は常時 discoverable、DISCONNECTED でも `apply_scan_mode`)、AUTH_CMPL に BDA/名前ログ、CoD=AV/Hands-free(0x02)+Audio|Rendering、音声状態で `audio_i2s_set_input_rate(8000/16000)`+`audio_i2s_reset`+`set_sco_active`(AG 側 `bt_hf_ag.c` の `handle_audio_state` と同等)。イベント名・`c_conn_state`/`c_audio_state` 配列の添字は `esp_hf_client_api.h` の enum(CONNECTION 0..4、AUDIO 0..3)と整合。設計 §14.4-2 のとおり。
- `sdkconfig`: `CONFIG_BT_HFP_CLIENT_ENABLE=y` と `CONFIG_HFP_CLIENT_ENABLE=y`(rename 由来)の 2 行のみ、`sdkconfig.defaults` と一致。`BT_HFP_AG_ENABLE=y` 維持。
- `leds.c` は `bt_role_request_factory_reset()`(マクロ)経由。設計の「ラッパ」をマクロで実現した点は許容(関数ではないため `bt_role.h` を include するファイルのみ利用可)。
- 未検証事項(設計 §14.6/14.10 のまま): 仮説 H1(SDP で HF を期待)、HF 役の SCO 開始主体(`connect_audio`/AT+BCC の受理)、HSP のみ要求の可能性(R-2)、HF 役の mSBC ネゴ。実験 A/B は未実施(§14.7)。

判定: **条件付き合格**(Medium 2 件: BT_ROLE 既定値の整合、spec 本文の更新。ファーム側に修正必須項目なし)。

| 項目 | 結果 |
|---|---|
| 仮説 H1 の検証結果(実験 A/B) | (空欄・実機後に記入) |


---

## 15. CR-012 2×ESP32 I2S デジタル直結(AUX / PCM5102A / ADC 廃止)

(DESIGNER / 2026-09-30。ソース未変更。既存章は変更していない。実機なし = ビルドと静的検証まで。)

### 15.1 変更要求
- ESP32 #1(`main/`, `BT_ROLE_HF`)= SRL3 メッシュ会話を SCO 受信 → **16 kHz モノ 16bit の I2S マスタ出力**(BCLK=GPIO26, WS=GPIO25, DOUT=GPIO27)。48 kHz アップサンプルと PCM5102A 依存を除去。CVSD 8 kHz 入力は 16 kHz へ 2 倍アップサンプル、mSBC 16 kHz はそのまま通す。
- ESP32 #2(`hf_unit/`, HFP-HF でスマホ / Insta360 X4 へ無線マイク)= `adc_capture.c`(ADC1 GPIO34, 32 kHz オーバーサンプル)を **I2S スレーブ RX**(BCLK=26, WS=25, DIN=27)に置換。SCO 送信は既存経路(`bt_data_path_write_pcm` → TX リング → `send_cb` / `esp_hf_client_outgoing_data_ready`)へ供給。クロックドリフト吸収(ジッタバッファ / 無音補填)を設計。
- 配線・電源は `docs/wiring_dual_esp32.md`(#1 の 5V を #2 へ、GND 共通、I2S 3 線)。
- 既存の AG 役・HF 役動作、`patches/`(SENA 対応 IDF パッチ)、CR-010/011 の挙動を壊さない。

### 15.2 現行解析(実ソース確認結果)
**#1 (`main/`)**
- 入力: `bt_data_path.c` `recv_cb`(BTC 文脈)が RMS 集計後 `audio_i2s_write()` でジッタ RB(`JITTER_RB_BYTES`=4096, BYTEBUF, SCO レート S16 モノ)へ。満杯時は最古を捨てる。
- `bt_hf_client.c`(`handle_audio_state`)/`bt_hf_ag.c` が SCO 接続時に `audio_i2s_set_input_rate(msbc?16000:8000)` → `audio_i2s_reset()` → `audio_i2s_set_sco_active(true)`。
- `audio_i2s.c` `i2s_tx_task`: プリフィル(`JITTER_PREFILL_BYTES`=480)後、RB から `I2S_CHUNK_MONO_SAMPLES`(64)取り出し → `audio_dsp_process`(1 次 HPF 30 Hz → ゲート(既定 OFF)→ ポリフェーズ FIR ×6/×3 で 48 kHz)→ モノ→ステレオ複製 → `i2s_channel_write(portMAX_DELAY)`。DMA 空き待ちが全体のペーシングになる。アンダーラン時は無音を流し続け(I2S は止めない)、`s_underrun++` して再プリフィル。
- I2S 設定: I2S0 マスタ TX、`I2S_STD_CLK_DEFAULT_CONFIG(48000)`, Philips 16bit STEREO, `dma_desc_num=8`, `dma_frame_num=128`, DOUT のみ(`din=UNUSED`)。ピン(26/25/27)は既に配線図と一致。
- 定数 `I2S_OUTPUT_RATE_HZ=48000`, `I2S_UP_MAX=6`(`s_up_mono`/`s_stereo` 静的バッファのサイズ根拠)。`audio_dsp.c` の係数は `tools/gen_fir.py`(`FS_OUT=48000` 固定)で生成した ×6 / ×3 のみ。
- **HF 役では送信タイマーを起動しない**(`bt_data_path_on_audio_connected` が `#if BT_ROLE==BT_ROLE_HF` で return。過去に controller の vhci ISR ウォッチドッグ panic)。#1 は受信専用のままとする(本 CR で変更しない)。
- `leds.c` は `audio_i2s_underrun_count()` のみ参照。
**#2 (`hf_unit/`)**
- `adc_capture.c`: ADC1 continuous 32 kHz → ボックスカー間引き(÷4=8 k / ÷2=16 k)→ 1 次 DC ブロッカ(ADC バイアス除去)→ 64 サンプル毎に `bt_data_path_write_pcm()`。レート切替は `bt_hf_client.c` `handle_audio_state` が `adc_capture_set_decimation()` で通知。
- `bt_data_path.c`: TX RB(`TX_RB_BYTES`=4096)。`send_cb` は `TX_PREFILL_BYTES`(480 B)未満なら 0 を返す(`esp_hf_client_outgoing_data_cb_t` 契約、RQ-H05)。不足時 `s_prefilled=false` で再プリフィル。`outgoing_data_ready` は 7.5 ms 周期 esp_timer(SCO 接続中のみ)。RB 満杯時は最古を捨てる。`recv_cb` は no-op。
- `app_config.h` の ADC 定数群(`PIN_ADC_AUDIO_IN` 等)、`CMakeLists.txt` は `esp_adc` 依存。GPIO 25/26/27 は hf_unit では未使用(LED=32/33/13, ボタン=14)= I2S に流用可能(競合なし)。
- SCO ブロック長: 7.5 ms → CVSD 8 kHz = 60 sample = 120 B、mSBC 16 kHz = 120 sample = 240 B(`len` は `send_cb` 引数で厳守)。

### 15.3 関連ファイル
| 区分 | ファイル |
|---|---|
| #1 変更 | `main/app_config.h`, `main/audio_i2s.c`, `main/audio_dsp.c`, `main/audio_dsp.h`(コメントのみ), `tools/gen_fir.py`(×2 係数生成) |
| #1 変更なし(確認のみ) | `bt_data_path.c`, `bt_hf_client.c`, `bt_hf_ag.c`, `leds.c`, `app_main.c`, `CMakeLists.txt`(`esp_driver_i2s` 既存) |
| #2 新規 | `hf_unit/main/i2s_capture.c`, `hf_unit/main/i2s_capture.h` |
| #2 変更 | `hf_unit/main/app_config.h`, `app_main.c`, `bt_hf_client.c`, `bt_data_path.c/.h`, `CMakeLists.txt` |
| #2 維持(ロールバック用) | `adc_capture.c/.h`(削除しない。`AUDIO_IN_MODE` で切替) |
| ドキュメント | `README.md`, `hf_unit/README.md`, `docs/wiring_dual_esp32.md`(配線変更なし)、spec 改訂は BOSS |

### 15.4 現行フロー
```
#1: SRL3 -SCO-> recv_cb -> jitter RB(8k/16k) -> i2s_tx_task: HPF -> FIR x6/x3 -> stereo(48k) -> I2S0 TX -> PCM5102A -> AUX
#2: ADC1(GPIO34,32k) -> adc_task: 間引き -> DC-block -> bt_data_path_write_pcm -> TX RB -> send_cb <- outgoing_data_ready(7.5ms timer) -> SCO -> スマホ
```
目標フロー:
```
#1: SRL3 -SCO-> recv_cb -> jitter RB(8k/16k) -> i2s_tx_task: HPF -> (8k:FIR x2 / 16k:素通し) -> mono 16k -> I2S0 TX master(BCLK/WS/DOUT)
                                                                                     |  3線 (16k, 16bit, mono, Philips, 左スロット)
#2: I2S0 RX slave <-----------------------------------------------------------------+
    i2s_capture_task: 16k -> (mSBC:素通し / CVSD:半帯域 FIR ÷2) -> ドリフト補正(±1 sample) -> bt_data_path_write_pcm -> TX RB -> send_cb -> SCO -> スマホ
```

### 15.5 変更方針(最小変更・決定事項)
1. **DAC 出力は残す**: `main/app_config.h` に `AUDIO_OUT_MODE`(`AUDIO_OUT_DAC48K=0` / `AUDIO_OUT_I2S16K=1`)を新設、**既定 = 1(I2S16K)**。0 で現行 48 kHz/ステレオ/PCM5102A 動作を完全に維持(コンパイル時スイッチ。旧経路のコードは削除しない)。
2. **#2 も同様に `AUDIO_IN_MODE`**(`AUDIO_IN_ADC=0` / `AUDIO_IN_I2S=1`)、**既定 = 1(I2S)**。`adc_capture.*` は残す(0 でビルド可)。
3. I2S フォーマットは両ユニット同一固定: **16000 Hz / 16bit / モノ / Philips / 左スロット(WS=Low)/ BCLK = 16000×2×16 = 512 kHz**、MCLK なし。**#1=マスタ、#2=スレーブ。#2 の SCO レートが 8k(CVSD)でも I2S は常に 16 kHz**(レートを SCO に追従させない = 配線に制御線が無いため。#2 側で間引く)。
4. #1 は SCO レートを I2S に伝える必要が無い(常に 16 k 化して出す)ため、#1↔#2 間に制御情報は無い。
5. クロックドリフト対策は **#2 の受信側で 1 サンプル単位の挿入 / 削除**(§15.6.3)。#1 は現行どおり(DMA 自動ペーシング、アンダーランは無音+再プリフィル)。
6. `patches/`・BT 関連コード(`bt_hf_client.c` のペアリング / SDP / CoD 等)・`bt_data_path.c`(#1)は触らない。`bt_role` / AG 役の経路(`AUDIO_OUT_MODE` は AG 役でも効く)は同じ `audio_i2s.c` を通るが、入力レート API は不変。

### 15.6 詳細設計

#### 15.6.1 `main/`(#1)
`app_config.h`:
```c
#define AUDIO_OUT_DAC48K  0
#define AUDIO_OUT_I2S16K  1
#define AUDIO_OUT_MODE    AUDIO_OUT_I2S16K
#if AUDIO_OUT_MODE == AUDIO_OUT_I2S16K
#define I2S_OUTPUT_RATE_HZ   16000
#define I2S_UP_MAX           2     /* 8k->16k = 2, 16k = 1 */
#define I2S_OUT_CHANNELS     1
#define I2S_DMA_DESC_NUM     6
#define I2S_DMA_FRAME_NUM    64    /* 64 frame @16k = 4 ms/desc, x6 = 24 ms */
#else  /* 既存値をそのまま: 48000 / 6 / 2ch / 8 / 128 */
#endif
```
(`PIN_I2S_*`、`JITTER_*`、`I2S_CHUNK_MONO_SAMPLES`=64 は不変。`I2S_OUT_CHANNELS` は `audio_i2s.c` のスロットモードと出力変換に使用。)

`audio_dsp.c` / `.h`:
- I2S16K モードでは `audio_dsp_set_input_rate(16000)` → `s_L=1`(FIR 素通し: HPF/ゲート後の `y` を直接 `out[]` へクリップ丸めして出力)、`8000` → `s_L=2` の新係数 `s_coef_x2[2][DSP_TAPS_PER_PHASE]`。`#if AUDIO_OUT_MODE == AUDIO_OUT_DAC48K` で既存 ×6/×3 表、`#else` で ×2 表(未使用表は `#if` で除外しフラッシュ節約)。
- ×2 係数: Kaiser 窓 sinc(β=7)、出力レート 16 kHz、fc=3.8 kHz(CVSD 帯域 3.4 kHz を平坦、像 4.6 kHz 以上を抑圧)、L=2、32 taps/phase(= 64 タップ)、各フェーズ DC ゲイン 1。`tools/gen_fir.py` は `FS_OUT` が定数 48000 固定なので `--fs-out`(または `FS_OUT` 変数)を引数化し、`design(2, 3800, 7.0, "s_coef_x2")` を出力できるようにする(既存 ×6/×3 の出力は変えない)。
- 16 k 素通し時の計算量は HPF のみ。8 k → ×2 は 1 入力あたり 2×32 MAC(48k 版より軽い)。
- `audio_dsp_upsample_factor()` は s_L を返す(1 を返し得る。呼び元は `audio_i2s.c` のみ)。

`audio_i2s.c`:
- `audio_i2s_init()`: `#if AUDIO_OUT_MODE == AUDIO_OUT_I2S16K` → `I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER)`、`cc.dma_desc_num=6`, `cc.dma_frame_num=64`, **`cc.auto_clear = true`**(v5.4 では `auto_clear_after_cb`/`auto_clear_before_cb` の別があるがデフォルトの `auto_clear` 相当を有効化:DMA データ枯渇時に 0 を出し続け、DOUT がゴミを出さない)。`i2s_std_config_t`: `clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000)`, `slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)`(`slot_mask = I2S_STD_SLOT_LEFT`)、gpio は現行と同一(mclk/din = UNUSED, invert なし)。`ESP_ERROR_CHECK(i2s_channel_init_std_mode)` + `i2s_channel_enable` は現行どおり(起動直後から BCLK/WS を出し続け、#2 がいつ起動しても同期できる)。
- `i2s_tx_task`: ステレオ複製ループを `#if` で分岐。I2S16K では `s_up_mono` を **そのまま** `i2s_channel_write(s_tx_chan, s_up_mono, nup*2, &written, portMAX_DELAY)`(`s_stereo` 配列は DAC48K のときだけ定義)。`s_up_mono` は `I2S_CHUNK_MONO_SAMPLES * I2S_UP_MAX` = 128 sample(=8 ms @16k)。
- `audio_i2s_set_input_rate` のログ文言は `"%d Hz I2S"`(`I2S_OUTPUT_RATE_HZ`)のまま動く(修正不要)。
- SCO 非アクティブ / アンダーラン時は既存どおり 0 サンプルを DSP 経由で流す → 無音 16 kHz が連続出力される(#2 側は常時同期可)。
- 出力レートが SCO の 16 kHz と一致する mSBC では、#1 内で BT クロック(SCO)と I2S クロック(ESP32 PLL)が別のため微小ドリフトがある。**現行(48 kHz)と同一の性質**なので変更せず、既存の「満杯時最古破棄 / 枯渇時無音+再プリフィル」で吸収(ppm オーダーで数十秒〜分に 1 回の 1 チャンク欠落。R-3 参照)。
- エラー処理: `i2s_channel_write` の戻り値が `ESP_OK` 以外(`ESP_ERR_TIMEOUT` は portMAX_DELAY のため出ない)なら 1 秒 1 回までレート制限で `ESP_LOGW`、ループ継続。

#### 15.6.2 `hf_unit/`(#2)I2S スレーブ RX
`app_config.h`(`AUDIO_IN_MODE` 新設。既定 I2S):
```c
#define AUDIO_IN_ADC 0
#define AUDIO_IN_I2S 1
#define AUDIO_IN_MODE AUDIO_IN_I2S
#define PIN_I2S_BCLK 26
#define PIN_I2S_WS   25
#define PIN_I2S_DIN  27
#define I2S_IN_RATE_HZ        16000
#define I2S_IN_DMA_DESC_NUM   6
#define I2S_IN_DMA_FRAME_NUM  128        /* 8 ms/desc, 6 desc = 48 ms */
#define I2S_IN_CHUNK_SAMPLES  128        /* 1 read = 128 sample = 256 B = 8 ms */
#define I2S_IN_READ_TIMEOUT_MS 50        /* BCLK 断検出 */
#define I2S_IN_LOST_MS        200        /* 連続タイムアウトでクロック喪失扱い */
/* ジッタバッファ(SCO レート換算 ms で規定。サンプル数 = rate*ms/1000) */
#define TX_PREFILL_MS   30
#define TX_TARGET_MS    40
#define TX_HYST_MS      20               /* target ± 20ms でドリフト補正 */
#define DRIFT_MIN_INTERVAL_MS 100        /* 補正の最短間隔(=補正は最大 10 sample/s) */
#define SILENCE_FILL_MAX_MS 200          /* send_cb 無音補填の上限 */
```
`#if AUDIO_IN_MODE == AUDIO_IN_ADC` の間だけ既存 ADC 定義を有効にする(`#if` で囲む。削除はしない)。`TX_RB_BYTES`(4096 = 16 kHz で 128 ms)は不変(TARGET+HYST=60 ms < 128 ms を満たす)。既存 `TX_PREFILL_BYTES` は `#if ADC` 内に残し、I2S 時は `bt_data_path.c` がレートから算出(§15.6.3)。

新規 `i2s_capture.h`:
```c
void i2s_capture_init(void);                  /* I2S0 RX slave 開始 + タスク生成。起動時 1 回 */
void i2s_capture_set_rate(uint32_t sco_hz);   /* 8000 or 16000。BtAppT から呼ぶ(volatile 代入のみ) */
void i2s_capture_set_active(bool on);         /* SCO 接続中のみ true。false 中は DMA を読み捨て */
uint32_t i2s_capture_clock_lost_count(void);  /* BCLK 喪失回数(診断) */
uint32_t i2s_capture_drift_ins_count(void);   /* 挿入補正回数 */
uint32_t i2s_capture_drift_del_count(void);   /* 削除補正回数 */
```
新規 `i2s_capture.c`(要点):
- `i2s_new_channel(&cc, NULL, &s_rx)`, `cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE)`、`dma_desc_num/frame_num` は上記。`i2s_std_config_t`: `clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000)`(スレーブでは DMA 計算用のみ)、`slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)`(**#1 とビット幅・Philips・LEFT 完全一致**)、`gpio_cfg = {mclk=UNUSED, bclk=26, ws=25, dout=UNUSED, din=27, invert 0}`。`i2s_channel_init_std_mode` → `i2s_channel_enable`(BCLK が無くても enable 可。失敗は `ESP_ERROR_CHECK`ではなく致命扱いにせず、ログして `i2s_capture` 無効のまま BT だけは起動 = 縮退)。
- タスク `i2s_capture`(優先度 `configMAX_PRIORITIES - 5`、スタック 3072、コア指定なし):ループで `i2s_channel_read(s_rx, raw, 256, &got, pdMS_TO_TICKS(50))`。
  - `ESP_ERR_TIMEOUT`: 連続 `I2S_IN_LOST_MS` 以上で `clock_lost++`、状態を「LOST」にして 1 秒に 1 回 `ESP_LOGW("I2S clock lost")`。LOST 中は何も `write_pcm` しない(→ send_cb が §15.6.3 の無音補填 200 ms を超えると 0 返却)。データが再び来たら `hist` リセットして復帰ログ。
  - `s_active == false`: 読んだデータを破棄(DMA を詰まらせない)。`false→true` 遷移時に `hist`, ドリフトタイマをリセット。
  - 正常: `n=got/2` sample。`rate=s_rate`(volatile を 1 回読み、チャンク内固定)。
    - `rate==16000`: 素通し。
    - `rate==8000`: 半帯域 7 tap FIR `y = (-x[n-6] + 9x[n-4] + 16x[n-3] + 9x[n-2] - x[n]) >> 5` を **2 サンプル毎に 1 出力**(間引き ÷2、整数演算、履歴 6 サンプルをチャンク跨ぎで保持)。DC ゲイン 1、CVSD 帯域端 3.4 kHz で −2 dB 程度・4 kHz 以上を抑圧(ADC 版のボックスカー ÷4 より高品質)。DC ブロッカは**不要**(デジタル入力。ただし #1 側 HPF 30 Hz が既に効く)なので削除。
    - 出力 `out[≤128]`(チャンク 128 in → 16k:128 / 8k:64)。
  - **ドリフト補正**(§15.6.3)を出力ブロックに適用し `bt_data_path_write_pcm(out, m)`。
- `i2s_capture_set_rate()` は `bt_hf_client.c` から `handle_audio_state`(CONNECTED / CONNECTED_MSBC)で呼ぶ。切替時は次チャンク境界で `hist` と補正状態をリセット。

#### 15.6.3 ジッタバッファ・ドリフト・無音補填(`bt_data_path.c/.h` 変更)
前提: #1 の I2S(ESP32 PLL 由来)と #2 の SCO 消費クロック(BT コントローラのマスタクロック由来)は独立。各 ±20 ppm 想定で相対 最大 ±40 ppm ⇒ 16 kHz で ±0.64 sample/s(約 1.5 s に 1 サンプル)。補正しないと 4096 B の RB は数分で満杯 / 空になる。
- 新規 API: `uint32_t bt_data_path_fill_bytes(void);`(`vRingbufferGetInfo` の free 領域から算出: `TX_RB_BYTES` 内の使用量。`xRingbufferGetCurFreeSize()` を用い `used = TX_RB_BYTES - free` とする。BYTEBUF は管理領域があるので `used` は近似で良く、閾値判定に ±数十 B の誤差は許容)。`void bt_data_path_set_rate(uint32_t hz);`(プリフィル・目標をバイト換算するため。`bt_data_path_reset()` と同じ呼び出し箇所)。
- バイト換算: `bytes = rate * ms / 1000 * 2`。8 kHz: PREFILL 30 ms=480 B, TARGET 40 ms=640 B, HYST ±20 ms=±320 B。16 kHz: 960 / 1280 / ±640 B。ADC 版の固定 `TX_PREFILL_BYTES`(480)は `AUDIO_IN_MODE==AUDIO_IN_ADC` のときのみ従来値を使用(`#if` 分岐、挙動不変)。
- ドリフト補正アルゴリズム(`i2s_capture` タスク内、`write_pcm` 直前、1 ブロックにつき最大 1 サンプル):
  - `fill = bt_data_path_fill_bytes()`。前回補正から `DRIFT_MIN_INTERVAL_MS` 未満なら何もしない。
  - `fill > target + hyst`(供給過多): ブロックの**最後のサンプルを 1 個削除**(`m-1` 個を書く)。`drift_del++`。
  - `fill < target - hyst`(供給不足): ブロック末尾サンプルを**複製して 1 個挿入**(`m+1`)。`drift_ins++`。
  - 端点 1 サンプルの挿入 / 削除は 8/16 kHz 音声で可聴歪みをほぼ生じない(最大 10 回/s)。可聴なら未解決事項 U-2(線形補間ストレッチ)。
  - 補正は SCO 非アクティブ時は行わない。
- `send_cb` 変更(I2S モード時のみ):
  - プリフィル未達 → 従来どおり 0 返却(接続直後の準備中)。
  - プリフィル達成後の不足(`got < len`): **無音補填**。取得できた `got` バイトをコピー、残りを `0` で埋め、`len` を返す(短い欠落を SCO 側で連続化)。連続補填時間が `SILENCE_FILL_MAX_MS`(200 ms)を超えたら `s_prefilled=false`、以後 0 返却+再プリフィル(I2S クロック喪失 / #1 停止)。補填ごとに `s_underrun++`。
  - 根拠: 契約上 0 返却は許容(現行 ADC 版)だが、I2S 版は短時間の不足(ドリフト初期・BT 再送)を欠落無音として滑らかに扱う方が X4 側の音切れが少ない。0 返却が必要なら `SILENCE_FILL_MAX_MS=0` で従来動作に戻る。
- `bt_data_path_reset()`(SCO 接続時)は従来どおり RB を空にし `s_prefilled=false`。加えて補填継続カウンタをクリア。`bt_data_path_write_pcm` は不変(満杯時に最古破棄 = `overrun++`)。
- `s_underrun/s_overrun` を LED 判定へ流用する既存仕様は不変(補填 5 回/秒超で SCO_ERR)。
- TX タイマー(`outgoing_data_ready` 7.5 ms)は hf_unit では従来どおり SCO 接続中のみ起動。

#### 15.6.4 `hf_unit` 接続点
- `app_main.c`: `adc_capture_init()` を `#if AUDIO_IN_MODE==AUDIO_IN_ADC ... #else i2s_capture_init(); #endif`。呼び出し順は現行の位置(BT 起動前)を維持。
- `bt_hf_client.c` `handle_audio_state`:
  - `adc_capture_set_decimation(...)` を `#if` で分岐し、I2S 時は `bt_data_path_set_rate(msbc?16000:8000); i2s_capture_set_rate(msbc?16000:8000);` を `bt_data_path_reset()` の**前**に、`i2s_capture_set_active(true)` を `bt_data_path_on_audio_connected()` と同じ箇所(CONNECTED 分岐)で呼ぶ。DISCONNECTED 分岐および ACL 切断分岐で `i2s_capture_set_active(false)`。
- `CMakeLists.txt`: SRCS に `i2s_capture.c` 追加、REQUIRES に `esp_driver_i2s` 追加(`esp_adc` と `adc_capture.c` は残す)。
- sdkconfig 変更なし(`CONFIG_ESP_DRIVER_I2S` は IDF 既定有効)。

### 15.7 データ構造 / IF 変更
| 対象 | 変更 |
|---|---|
| `main/app_config.h` | `AUDIO_OUT_MODE` 追加、I2S16K 時に `I2S_OUTPUT_RATE_HZ/I2S_UP_MAX/DMA` を上書き。`I2S_OUT_CHANNELS` 追加 |
| `audio_dsp` | 内部のみ(`s_L=1/2` 追加、`s_coef_x2`)。公開 API 不変 |
| `audio_i2s` | 公開 API 不変 |
| `hf_unit/app_config.h` | `AUDIO_IN_MODE`, `PIN_I2S_*`, `I2S_IN_*`, `TX_*_MS` 追加 |
| `hf_unit i2s_capture.h` | 新規 IF(§15.6.2) |
| `hf_unit bt_data_path.h` | `bt_data_path_fill_bytes()`, `bt_data_path_set_rate()` 追加。既存 API 不変 |
| BT / NVS / GAP | 変更なし |

### 15.8 影響範囲
- #1: `AUDIO_OUT_MODE=1` では AG 役・HF 役とも出力が 16 kHz モノ(AG 役の音声出力も同様)になる。PCM5102A を繋ぐ従来構成は `AUDIO_OUT_MODE=0` に戻す。ペアリング、SDP、CoD、`patches/` 依存部分に変更なし。
- #2: マイク入力が ADC → I2S。ADC 用 GPIO34 は未使用に。`esp_adc` コンポーネントは残るがリンク時に `AUDIO_IN_MODE==I2S` では `adc_capture.c` を `#if` で空にするか未参照のため最終バイナリから外れる(要ビルドで確認)。
- スペック(`dummy_intercom_spec.md`)の音声・ピン章は BOSS が rev 更新(CR-012)。

### 15.9 リスク
| ID | リスク | 対策 |
|---|---|---|
| R-1 | モノ I2S のスロット解釈不一致(#1 が左 / 右のどちらに出すか、#2 がどちらを取るか)で無音になる | 両側 `slot_mask=LEFT`+ Philips 固定。実機で無音なら `I2S_STD_SLOT_RIGHT` 両側変更(1 行)。テスト T-2 |
| R-2 | #1 起動前 / リセット中の DOUT・BCLK 浮遊で #2 がノイズを取り込む | SCO 非アクティブ中は #2 が読み捨て。DATA に 100kΩ プルダウン推奨(配線図追記は BOSS) |
| R-3 | #1 内で SCO(BT)クロックと I2S クロックのドリフト → まれなチャンク欠落(現行同等) | 現行同等として許容。気になる場合は #1 側にも±1 サンプル補正(§U-3) |
| R-4 | スレーブの BCLK 位相 / エッジ(マスタは立下がりで出力、スレーブは立上がりで取得)ずれ、配線長 | 標準 Philips で整合。実機で化ける場合は `invert_flags.bclk_inv`。配線 10 cm 以内、直列抵抗 33〜100Ω |
| R-5 | ドリフト補正が音質を劣化 | 最大 10 回/s、端点 1 サンプルのみ。可聴時は U-2 |
| R-6 | ESP32 I2S0 のマスタ MCLK なし 16 kHz で内部分周が粗い | `I2S_STD_CLK_DEFAULT_CONFIG(16000)`(mclk_multiple=256, PLL_F160M)で誤差は許容範囲。ビルド後 `i2s_channel_init_std_mode` が ESP_OK を返すか実機起動ログで確認 |
| R-7 | 電源共有(#1 の 5V から #2 へ)で BT 送信ピークの電圧降下 | 配線書のコンデンサ推奨。ファーム対応不可 |
| R-8 | HF 役で `outgoing_data_ready` を #1 で起動すると controller panic(既知) | #1 は受信専用のまま(本 CR で触らない) |
| R-9 | `AUDIO_OUT_MODE` 切替で `s_stereo` 等のバッファが `#if` 崩れ、AG/HF どちらかがビルド不能 | CODER は §15.11 のビルド 4 通りを必須実施 |

### 15.10 CODER への実装指示(ファイル別。ソースは CODER のみ変更)
**共通**: ESP-IDF v5.4(`export IDF_TOOLS_PATH=/home/subcon/.espressif && . ~/esp/esp-idf/export.sh`)。`sdkconfig`/`sdkconfig.defaults`/`patches/`/`build/` を手で編集しない。既存の関数シグネチャを変えない。

1. `main/app_config.h`: §15.6.1 のとおり `AUDIO_OUT_MODE` を追加。`AUDIO_OUT_MODE==0` で既存値(48000/6/2ch/8/128)を保持。`I2S_OUT_CHANNELS`(0→2, 1→1)。
2. `tools/gen_fir.py`: `FS_OUT` を `design()` の引数化(既定 48000)し、×2 表(L=2, fc=3800, β=7, 32 taps/phase, FS_OUT=16000)を出力できるようにする。既存出力(×6/×3)を再生成して差分が無いことを確認。
3. `main/audio_dsp.c`: `#if AUDIO_OUT_MODE == AUDIO_OUT_DAC48K` で既存 ×6/×3 表と切替、`#else` で `s_coef_x2` を定義。`audio_dsp_set_input_rate` を I2S16K では 16000→L=1、8000→L=2。L==1 のとき FIR を省略して `y` を丸めクリップして出力(履歴シフトはスキップ可、reset は維持)。`audio_dsp_process` の `n_in > out_cap / L` ガードはそのまま。コメント内の「48 kHz」記述は `I2S_OUTPUT_RATE_HZ` に更新。`audio_dsp.h` はコメントのみ(factor 6/3 → 6/3 or 2/1)。
4. `main/audio_i2s.c`: `audio_i2s_init` と `i2s_tx_task` を §15.6.1 のとおり `#if` 分岐(モノ直書き込み、`s_stereo` は DAC48K のみ)。`auto_clear=true`(I2S16K)。`i2s_channel_write` 戻り値チェック(レート制限ログ)。ログ文言に "mono"/"stereo" を含める。
5. `hf_unit/main/app_config.h`: §15.6.2 の定数追加、ADC 定数を `#if AUDIO_IN_MODE == AUDIO_IN_ADC` で囲む(`TX_PREFILL_BYTES` は ADC 時のみ)。
6. `hf_unit/main/i2s_capture.h/.c` 新規: §15.6.2。必須事項:(a) `i2s_new_channel(&cc, NULL, &s_rx)` ロール SLAVE、(b) ピン 26/25/27、(c) 静的バッファのみ・タスク内で malloc 禁止、(d) `i2s_channel_read` は 50 ms タイムアウト、(e) 半帯域 FIR は整数 `(-x0 + 9x2 + 16x3 + 9x4 - x6) >> 5` を 2 サンプル毎(履歴はチャンク跨ぎ保持、ゲイン飽和は int16 クリップ)、(f) ドリフト補正は §15.6.3、(g) init 失敗時は `ESP_LOGE` して return(BT は継続)。
7. `hf_unit/main/bt_data_path.c/.h`: `bt_data_path_fill_bytes()`, `bt_data_path_set_rate()` 追加。I2S モード時のプリフィル / 無音補填(§15.6.3)。ADC モードでは従来コードパスが完全に残ること(`#if`)。`recv_cb` 不変。
8. `hf_unit/main/app_main.c`, `bt_hf_client.c`, `CMakeLists.txt`: §15.6.4。
9. `README.md`, `hf_unit/README.md`: 配線(I2S 3 線 + 5V + GND)、`AUDIO_OUT_MODE`/`AUDIO_IN_MODE` の意味、PCM5102A 構成に戻す方法を追記(仕様 rev は BOSS)。

### 15.11 テスト観点
**ビルド(CODER が必須実施、実機なし)**:
- B-1 `idf.py build`(`AUDIO_OUT_MODE=1, BT_ROLE_HF`)、B-2 同 `BT_ROLE_AG`、B-3 `AUDIO_OUT_MODE=0`(旧構成)、B-4 `idf.py -C hf_unit build`(`AUDIO_IN_MODE=1`)、B-5 同 `AUDIO_IN_MODE=0`。すべてワーニング増加なし。build 成果物のコミット要否は BOSS。
**静的検証**: (a) `I2S_OUTPUT_RATE_HZ`/`I2S_UP_MAX` からのバッファ計算がオーバーラン無し(`s_up_mono` = 64×2)、(b) hf_unit の ISR/BTC 文脈で新規のブロッキング呼び出し無し(`send_cb` は現行同様 `xRingbufferReceiveUpTo(...,0,...)` のみ)、(c) `#1` と `#2` の I2S 設定(ビット幅・スロットモード・マスク・規格)が一致、(d) `gen_fir.py` 再生成の既存表差分なし、(e) ×2 係数の各フェーズ DC ゲイン ≈ 1。
**実機(未実施 = 報告時に明記)**:
- T-1 配線導通(26/25/27/GND/5V)。 T-2 #1 に 1 kHz を SCO 経由で入力 → #2 の RX で 16 kHz サンプルに 1 kHz が出る(左スロット)。 T-3 CVSD/mSBC 双方で X4 に音声が届く(#2 のレート切替追従)。 T-4 30 分連続でドリフト補正が動作し `overrun/underrun` が増え続けない、`drift_ins/del` の平均が ±0.64/s 以内。 T-5 #1 リセット中 / 未接続 → #2 はクロック喪失ログ、200 ms 後に 0 返却、復帰後に再プリフィルで自動復旧。 T-6 #2 単独起動(#1 なし)でも BT ペアリング可能。 T-7 `AUDIO_OUT_MODE=0` で PCM5102A 従来動作。 T-8 SENA SRL3 との HF 役接続・SCO 確立が CR-011 と同一。

### 15.12 未解決事項(ユーザー判断が必要)
| # | 事項 | 現在の仮定 / 推奨 |
|---|---|---|
| U-1 | `AUDIO_OUT_MODE`/`AUDIO_IN_MODE` の**既定値**を I2S(新構成)にしてよいか。AG 役での従来 PCM5102A 利用者に影響 | 既定 I2S、0 で旧構成。**要承認** |
| U-2 | ドリフト補正方式:端点 1 サンプル挿入 / 削除(推奨・最小実装)でよいか、より高品質な線形補間ストレッチにするか | 前者。実機で可聴なら後者 |
| U-3 | #1 側にもドリフト補正が必要か(現行 48 kHz 経路と同等の性質で未対処) | 実機 T-4 の結果で判断 |
| U-4 | #2 の SCO 不足時に**無音を埋めて送信**(推奨)か、従来どおり**0 返却(送信スキップ)**か | 無音補填 200 ms まで。X4 側の挙動で調整 |
| U-5 | `dummy_intercom_spec.md` の rev 採番(0.12?)と、ピン章 / 音声章の改訂(BOSS) | BOSS 判断 |
| U-6 | スマホ側マイク接続先は Insta360 X4 のままか(wiring 書 §5-1)。CoD(Microphone)が受け入れられるかは CR-010/011 同様 実機未検証 | 現状維持 |
| U-7 | 基板は devkit か裸モジュールか(wiring 書 §5-3)。DATA のプルダウン抵抗(R-2)や直列抵抗の実装可否 | devkit 想定 |
| U-8 | `M-1`(§14.11)`BT_ROLE` の既定値が HF のまま(AG に戻すか)。本 CR とは独立だが `AUDIO_OUT_MODE` の効き方に影響 | 既定 HF のまま検証継続なら明記 |

### 15.13 CODER 質問への確定回答(DESIGNER / §15.5〜15.6 の補足。矛盾時は本節が優先)
1. **コーデック(8k/16k)**: #1 の I2S 出力は **常に 16 kHz 固定**。SCO が CVSD(8k)なら `audio_dsp` で ×2 アップサンプル、mSBC(16k)なら素通し。#2 の SCO レートは #2 自身が X4 と決めたもので、#1 側と**一致する必要はない**(I2S は PCM であり、コーデック非依存。#1=CVSD/#2=mSBC でも可、その逆も可)。#2 は SCO が CVSD なら 16k→8k を半帯域 FIR ÷2、mSBC なら素通し。#1↔#2 間にレート情報のやり取りは無い。ただし #1=CVSD のとき音声帯域は実質 3.4 kHz までで、#2=mSBC でもそれ以上には広がらない(仕様どおり)。
2. **`audio_dsp`**: 残す(HPF 30 Hz、ゲートは既定 OFF のまま)。FIR のみ切替(16k 入力 = 素通し L=1、8k 入力 = ×2 の新係数)。`I2S_OUTPUT_RATE_HZ` は `AUDIO_OUT_MODE=1` で 16000、`I2S_UP_MAX` は 2(`s_up_mono` = 64×2 = 128 sample)。`=0` では従来値 48000 / 6。
3. **ピン**: 配線図どおり **BCLK=26, WS=25, #1 DOUT=27 → #2 DIN=27** で確定。GPIO34 は使わない(入力専用ピンで #1 の出力側と非対称、devkit で露出しない可能性もあり、配線図・PDF と不一致になるため)。#2 では 25/26/27 は未使用ピンで競合なし。
4. **スロット**: 両側 **モノ 16bit Philips、`slot_mask = LEFT`(WS=Low のスロット)**。#1 は `I2S_SLOT_MODE_MONO` で mono データを直接書き込み(ステレオ複製はしない)、#2 も `I2S_SLOT_MODE_MONO` の左スロット受信。BCLK = 16000×2×16 = 512 kHz(フレームは L/R 2 スロットぶん出るが有効データは左のみ)。実機で右スロットに出る/取れる場合は両側 `RIGHT` に変更(R-1)。
5. **ドリフト吸収・バッファ値**(16kHz 基準で再定義済み)。
   - **#1**: 追加補正なし(U-3)。`JITTER_RB_BYTES`=4096、`JITTER_PREFILL_BYTES`=480、`JITTER_TARGET_BYTES`=960 は **SCO 入力レート基準のため不変**(8k で 30 / 60 ms、16k で 15 / 30 ms。mSBC で 15 ms が短いと感じる場合は `JITTER_PREFILL_BYTES` を 960 に上げる余地あり = 実機調整)。
   - **#2**: `TX_RB_BYTES`=4096 不変(16k で 128 ms)。`TX_PREFILL_BYTES` は固定値を廃止し `TX_PREFILL_MS=30`(8k=480 B / 16k=960 B)、目標 `TX_TARGET_MS=40`(640 / 1280 B)、ヒステリシス ±20 ms(±320 / ±640 B)。§15.6.3 のとおり受信側で最大 10 回/秒の 1 サンプル挿入/削除。
6. **SCO 未確立時**: #1 は I2S を**常時出力**(SCO 非アクティブ / アンダーラン / プリフィル待ちは無音 0 を DSP 経由で流し続ける = 現行踏襲。マスタのクロックを止めない)。#2 は I2S RX を**常時読み取り**、`i2s_capture_set_active(false)` の間は**破棄**(RB に入れない)。SCO 接続時に `true` + `bt_data_path_reset()`(RB 空・プリフィル再武装)。
7. **削除範囲**: **何も削除しない**(ロールバック用に `AUDIO_IN_MODE=0`/`AUDIO_OUT_MODE=0` を維持)。`adc_capture.*` 残置、`esp_adc` は CMake REQUIRES に残す、ADC 定義は `#if AUDIO_IN_MODE==AUDIO_IN_ADC` で囲むのみ。`bt_data_path.c`(#2)のコメント「adc_capture タスクから」は「adc_capture / i2s_capture タスクから」に更新。#1 の `bt_data_path.c` は変更不要。廃止(完全削除)を望む場合は **ユーザー判断(U-9)**。
- **U-9 (新規)**: ADC / PCM5102A 関連コードを完全削除して最終形にするか(推奨=当面残置、実機検証完了後に削除 CR を別途)。

### 15.14 実装後レビュー結果(DESIGNER / 2026-09-30、CODER 実装の git diff 対象・静的のみ)
対象: `main/{app_config.h,audio_dsp.c/.h,audio_i2s.c}`, `tools/gen_fir.py`, `hf_unit/main/{app_config.h,app_main.c,bt_hf_client.c,bt_data_path.c/.h,adc_capture.c,CMakeLists.txt,i2s_capture.c/.h}`, 両 README。CODER 報告: B-1〜B-5 ビルド成功。DEBUGGER の `test_report.md §12` は**ベースライン + 計画のみ**で B/S/R の実施結果は未記載(本レビュー時点)。

**判定: 合格(Critical 0 / High 0 / Medium 0 / Low 5)。** §15・§15.13 と整合。

確認済み(問題なし):
- #1 `AUDIO_OUT_MODE`: 16k/mono/`I2S_UP_MAX=2`/DMA 6×64/`auto_clear`、`s_stereo` は 2ch 時のみ、`s_up_mono` = 64×2 で `audio_dsp_process` の `out_cap/L` ガードと整合。`AUDIO_OUT_MODE=0` は従来値(48000/6/2/8/128)と ×6/×3 表を保持。×2 係数は各フェーズ DC ゲイン 1.000(数値確認)。L=1 素通しは HPF 後にクリップ丸めのみ、`set_input_rate` が `audio_dsp_reset` を呼ぶため 1↔2 切替で履歴の残りなし。`bt_data_path.c`/`bt_hf_*.c`/`leds.c`/`app_main.c`(main)、`patches/` に差分なし = AG/HF・SENA 対応への副作用なし。HF 役で送信タイマー非起動も維持。
- #2 `i2s_capture.c`: SLAVE/26/25/27、mono/Philips/16bit で #1 と一致(左スロット既定)。静的バッファのみ、50 ms タイムアウト、200 ms で LOST、非アクティブ時読み捨て、init 失敗は縮退(BT 継続)。半帯域 FIR: `x[-6..0]` 添字・履歴 6 サンプルのチャンク跨ぎ保持・2 サンプル毎出力・クリップとも正しい(`s_out` は最大 n+1 で足りる)。ドリフト補正: 1 ブロック最大 1 サンプル・≥100 ms 間隔・閾値バイト換算が §15.13-5 の値と一致、`TX_RB_BYTES`(4096)> TARGET+HYST。
- BTC 文脈(`send_cb`)は `xRingbufferReceiveUpTo(...,0,...)`/`vRingbufferGetInfo` のみで非ブロッキング。ISR 関連の新規呼び出しなし。`bt_data_path_fill_bytes` は producer タスクからの非ブロッキング参照。
- `handle_audio_state` の順序(`set_rate` → `reset` → `set_active(true)` → タイマー)と DISCONNECTED/ACL 切断の `set_active(false)` は §15.6.4 どおり。

CODER 提示の設計差異の判定:
| 差異 | 判定 |
|---|---|
| `#if` 囲み(ADC 定義・`adc_capture.c` 本体を I2S モードで空に) | **妥当**。§15.8 の「要ビルド確認」に沿い、B-4 の map で `adc_capture` 不在を DEBUGGER が確認すること。ロールバック性も維持 |
| `send_cb` の最大 2 回取得 | **妥当・必要**。BYTEBUF は折返し境界で `ReceiveUpTo` が短く返るため、1 回のみだと mSBC の 240 B 要求で誤った不足→無音補填が周期的に発生し得る。非ブロッキングのまま。(既存 ADC 版の 1 回取得も同じ潜在問題だが、本 CR の範囲外) |
| プリフィル期間もドリフト補正が働く | **許容**(Low L-2)。プリフィル 30 ms が補正下限 20 ms(8k)を上回るのは開始後数十 ms で、補正間隔 100 ms 未満なら発動しない。再プリフィル直後に最大 1 サンプル余分に挿入され得るが可聴影響なし |

| 重大度 | ID | 内容 / 推奨 |
|---|---|---|
| Low | L-1 | `i2s_capture_set_rate()` を SCO 中(CVSD↔mSBC 再ネゴ)に呼ぶと、`bt_data_path_reset()` 直後にタスクが旧レートのチャンクを 1 つ書き得る(`set_active` を落とさないため)。1 チャンク(8 ms)の音の乱れのみで実害小。気になれば `reset` 前後で `set_active(false→true)` を挟む |
| Low | L-2 | 上記プリフィル中補正。厳密にするなら補正を `s_prefilled` 真の間だけに(`bt_data_path` に getter 追加)。現状のままで可 |
| Low | L-3 | `s_silence_bytes` は BTC のみ更新だが `bt_data_path_reset()`(BtAppT)からもクリアされる非原子的 32bit 書込み。実害なし(ESP32 は 32bit 書込みがアトミック)。コメントで明記推奨 |
| Low | L-4 | git インデックスに `build/*`、`sdkconfig.old`、`hf_unit/sdkconfig.old` が staged。CR-012 の成果物ではないため、コミット時は `git restore --staged` で除外推奨(BOSS 判断)。`docs/wiring_dual_esp32.md`・`esp32-esp32-wiring.pdf` は意図的な未追跡(add 要) |
| Low | L-5 | `test_report.md §12.1` に「#1 が HFP-AG の SCO 受信」とあるが、#1 の既定は `BT_ROLE_HF`(AG は切替時)。DEBUGGER に文言訂正を依頼。また §12 の B/S/R 実施結果は未記載のため、記載後に本レビューの静的確認と突合すること |

実機でのみ確認できる項目(T-1〜T-8)は変更なし。特に **R-1(左スロットの解釈)と R-4(BCLK エッジ)** は最初の通電で確認し、無音なら両側 `slot_mask` を RIGHT、化けるなら `bclk_inv` を試すこと。§15.12 の U-1〜U-9(既定値承認等)は依然ユーザー判断待ち。

---

## 16. CR-014 録音レベル低下対策 A(AT+VGS 送信)

### 16.1 変更要求
SRL3 メッシュ会話を PC で録音すると音量が小さい。BOSS 事前調査により #1(main/)・#2(hf_unit/)の DSP / I2S 経路に減衰は無い(unity)ことを確認済み。#1(HF 役)は `esp_hf_client_volume_update` を一度も呼んでおらず、AG である SRL3 の SCO 出力レベルが未制御。
**ユーザー決定 = 対策 A**: #1 から SRL3 へ HFP スピーカー音量 `AT+VGS=15`(最大)を送信する。**スコープ外**: 対策 B(DSP ゲイン)、hf_unit(#2)の変更、マイク音量(AT+VGM)。

### 16.2 現行ソフト解析結果
- `main/bt_hf_client.c` は `esp_hf_client_volume_update` を呼ばない。`bt_hf_cb`(BTC 文脈)の `ESP_HF_CLIENT_VOLUME_CONTROL_EVT` は専用 case 無しで `default:` の `"HF event %d (log only)"` に落ち、音量値・種別が読めない(コメント上は「ログのみ、応答不要」)。
- `handle_conn_state(SLC_CONNECTED)` は `s_slc=true`、peer 保存、scan mode、LED、reconnect タイマ停止のみ。`connect_audio` は意図的に呼ばない(SRL3 が自ら SCO を開く。AT+BCC は拒否され AT timeout になる)。
- `handle_audio_state(CONNECTED / CONNECTED_MSBC)` は `s_sco=true`、`audio_i2s_set_input_rate` → `audio_i2s_reset` → `set_sco_active(true)` → `bt_data_path_on_audio_connected` → LED の順。いずれも BtAppT(`hf_work_handler`)上で実行される。
- API 仕様(`esp_hf_client_api.h`): `esp_err_t esp_hf_client_volume_update(esp_hf_volume_control_target_t type, int volume)`。type は `ESP_HF_VOLUME_CONTROL_TARGET_SPK` / `_MIC`、volume は **0〜15**。**前提条件: SLC 確立済み**。戻り値は `ESP_OK`(下位層へ送出)、`ESP_ERR_INVALID_STATE`(Bluedroid 未有効)、`ESP_FAIL`(その他)。実装(`esp_hf_client_api.c`)は `btc_transfer_context` で BTC へ投げるだけで、**ESP_OK は「キュー投入成功」であり AG が受理した保証ではない**(SLC 未確立時の拒否は BTC 側でサイレントに起こり得る)。範囲外値のチェックは API 側に無い → 呼び出し側で保証する。
- `ESP_HF_CLIENT_VOLUME_CONTROL_EVT`: `param->volume_control.type`(`esp_hf_volume_control_target_t`)/ `.volume`(0..15)。AG の `+VGS` / `+VGM` 通知で発生。
- 規約: `esp_hf_client_*` は BtAppT(`bt_app_core` ワークキュー)からのみ呼ぶ(`bt_hf_client.c` 冒頭コメント / §13.7.4)。`bt_hf_cb` は BTC 文脈で軽処理のみ(ISS-013)。`bt_app_core.c` / `bt_data_path.c` は本 CR で変更不要(`bt_data_path` は SCO データ経路でボリュームとは無関係)。

### 16.3 関連ファイル
| ファイル | 変更 |
|---|---|
| `main/app_config.h` | `HF_SPK_VOLUME` 追加 |
| `main/bt_hf_client.c` | `send_spk_volume()` 追加、SLC_CONNECTED / SCO CONNECTED から呼出し、`VOLUME_CONTROL_EVT` ログ case 追加 |
| `main/bt_app_core.c`, `bt_data_path.c`, `hf_unit/*` | 変更なし |

### 16.4 現行処理フロー
SRL3 接続 → `CONNECTION_STATE_EVT(SLC_CONNECTED)`(BTC)→ `hf_post` → BtAppT `handle_conn_state`(状態更新のみ)→ …SRL3 が通話開始時に SCO を開く → `AUDIO_STATE_EVT` → BtAppT `handle_audio_state`(I2S 起動)。音量制御は一切無し。AG(SRL3)は自身の既定 SCO ゲインで送出。

### 16.5 変更方針
BtAppT 上の 2 箇所(SLC 確立直後 / SCO 確立直後)から共通関数 `send_spk_volume()` で `AT+VGS=HF_SPK_VOLUME` を送る。値はコンパイル時定数(`-1` で無効化)。AG 通知はログのみ(応答しない)。

### 16.6 詳細設計
**送信タイミング = 両方(SLC 直後 + SCO 確立時)**。理由:
- SLC 直後: HFP は SLC 確立後に HF から VGS を送るのが一般的で、AG が接続単位で保持する場合はこれで足りる。API の前提(SLC 確立)も満たす。
- SCO 確立時: AG によっては SCO(通話)開始で内部ボリュームを既定値へ戻す/通話ごとに再初期化するため、SLC 時の 1 回だと効かない可能性がある(SRL3 の挙動は未確認 = 実機要確認)。SCO 確立直後に再送するコストは AT 1 行のみ。
- 3 回目以降の周期再送は**しない**(AG が頻繁に書き換えるなら `+VGS` 通知が増え、ログで検知できる。対策は実機結果を見て別 CR)。
- SCO の CVSD↔mSBC 再ネゴで `AUDIO_STATE CONNECTED` が複数回来る場合も都度送られるが、軽量なので許容。

**SLC 未確立時の扱い**: 呼出し元が SLC_CONNECTED ハンドラと SCO CONNECTED(SLC 確立が前提)なので通常発生しない。ただし SCO CONNECTED 側は `s_slc` が真のときのみ送る(防御。SLC 切断直後のレース回避)。失敗(`ESP_OK` 以外)は `ESP_LOGW` で記録し、**リトライ・状態保持はしない**(次の SCO 確立で再送される)。音声経路・SCO・LED の処理は送信結果に依存させない。

**AG からの +VGS/+VGM 通知**: `bt_hf_cb` に専用 case を追加しログ化(`AG volume: SPK = n`)。BTC 文脈なので ESP_LOGI のみ、`hf_post` 不要、**VGS を送り返さない**(AG との往復ループ防止)。SRL3 が VGS を無視/書換えしているかを実機ログで判別する唯一の手段。

**BOSS 試作パッチとの差分(独立判断)**: 構造(関数 1 つ・2 箇所呼出し・VOLUME_CONTROL ログ)は採用。変更点: (1) SCO 側に `s_slc` ガード追加、(2) 失敗時を `LOGW`、成功時 `LOGI`(`esp_err_to_name` 併記)、(3) `HF_SPK_VOLUME` に範囲コンパイルチェック(`#if > 15` で `#error`)、(4) 無効時(-1)も関数は残し本体を `#if` で空にして呼出し側を `#if` だらけにしない(`(void)when` で未使用警告回避)、(5) `when` 引数でログに送信契機を出す。

### 16.7 データ構造変更
なし(`hf_work_t`・`s_slc`/`s_sco` 不変。新規 static 変数なし)。

### 16.8 インターフェース変更
公開 API 変更なし。`app_config.h` に `HF_SPK_VOLUME`(整数、0..15 または -1)追加。AG 役(`BT_ROLE_AG`)ビルドでは `bt_hf_client.c` がビルド対象外のため影響なし(要ビルド確認)。

### 16.9 影響範囲
#1 HF 役のみ。#2(hf_unit)、I2S、DSP、LED、再接続、ボンディング、SCO 開始手順(AT+BCC を送らない)は不変。SRL3 が受理すると SCO 受信 PCM の振幅が増える → #1 の `audio_dsp`(HPF/ゲート/リミット類)・I2S 出力・#2・PC 録音が全体に大きくなる。

### 16.10 リスク・注意事項
| ID | リスク | 対応 |
|---|---|---|
| R-1 | SRL3 が VGS を無視する(メッシュ音声レベルは固定) | 効果なし。ログ `AG volume`/AT 応答で判別 → 対策 B(別 CR)。副作用なし |
| R-2 | SRL3 が SCO 開始/再接続で音量をリセット | SCO 時再送で緩和。毎回リセットされ再送が遅れる場合は冒頭が小さい可能性 |
| R-3 | 最大音量でクリップ/歪み(SRL3 側出力や #1 の DSP リミット) | 実機で RMS ログ(`RMS_LOG`)とクリップ有無を確認、必要なら 12〜14 に下げる(定数のみ) |
| R-4 | SLC 直後の AT 送信が SLC 初期化(CIND/CHLD 等)と衝突し AT timeout を誘発 | Bluedroid は HF 側 AT をキュー/直列化するが、SRL3 で SLC 切断が増えるなら SLC 側送信を外し SCO 時のみにする(定数分岐で切替可能にしておく) |
| R-5 | `ESP_OK` は AG 受理を意味しない | ログ文言を "queued" とし誤解を避ける |
| R-6 | BtAppT 外から呼ぶとスタック規約違反 | 呼出しは `handle_*`(BtAppT)内のみ。`bt_hf_cb` から呼ばない |

### 16.11 CODER への実装指示
**変更対象**: `main/app_config.h`、`main/bt_hf_client.c` のみ。他ファイル(特に `hf_unit/`、`bt_app_core.c`、`bt_data_path.c`、`audio_*`)は変更禁止。

**(1) `main/app_config.h`**(`/* ---- Audio DSP ...*/` ブロックの後、`/* ---- LEDs` の前あたり)
```c
/* CR-014: HFP speaker volume (AT+VGS, 0..15) sent to the AG (SRL3) after SLC and
 * SCO setup. The AG scales its SCO output by it; 15 = loudest. -1 = do not send. */
#define HF_SPK_VOLUME            15
#if HF_SPK_VOLUME > 15 || HF_SPK_VOLUME < -1
#error "HF_SPK_VOLUME must be -1 (disabled) or 0..15"
#endif
```

**(2) `main/bt_hf_client.c`**
a. `handle_stack_up` の後・`handle_conn_state` の前に追加(BtAppT 専用の旨をコメント):
```c
/* CR-014: AT+VGS to the AG. Call from BtAppT only (handle_*), after SLC. ESP_OK
 * only means "queued to BTC", not "accepted by the AG". Failure is non-fatal. */
static void send_spk_volume(const char *when)
{
#if HF_SPK_VOLUME >= 0
    esp_err_t e = esp_hf_client_volume_update(ESP_HF_VOLUME_CONTROL_TARGET_SPK, HF_SPK_VOLUME);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "AT+VGS=%d queued (%s)", HF_SPK_VOLUME, when);
    } else {
        ESP_LOGW(TAG, "AT+VGS=%d failed (%s): %s", HF_SPK_VOLUME, when, esp_err_to_name(e));
    }
#else
    (void)when;
#endif
}
```
b. `handle_conn_state` の SLC_CONNECTED: 既存の `ESP_LOGI(TAG, "SLC connected");` の**直後**、`connect_audio` に関するコメントの前に `send_spk_volume("SLC");`。
c. `handle_audio_state` の CONNECTED/CONNECTED_MSBC: 既存の `ESP_LOGI(... "SCO connected")` の**直後**に
```c
        if (s_slc) {
            send_spk_volume("SCO");
        }
```
d. `bt_hf_cb` の `ESP_HF_CLIENT_PROF_STATE_EVT` case の後に追加(`hf_post` しない・API 呼出し禁止):
```c
    case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:   /* AG +VGS/+VGM: log only, never echo back */
        ESP_LOGI(TAG, "AG volume: %s = %d",
                 param->volume_control.type == ESP_HF_VOLUME_CONTROL_TARGET_SPK ? "SPK" : "MIC",
                 param->volume_control.volume);
        break;
```
e. `bt_hf_cb` 末尾コメントの列挙から `VOLUME_CONTROL` を削除(ログ専用 case 化したため)。

**現行動作 → 変更後**: 音量未送信 → SLC 確立直後と SCO 確立直後に `AT+VGS=15` を各 1 回送信、AG 通知をログ化。
**エラー処理**: 失敗は WARN のみ、リトライ・状態変更・return 伝播なし。`HF_SPK_VOLUME=-1` では送信せず警告も出さない。
**境界条件**: 値 0..15 以外は `#error`。SLC 未確立で SCO 側から呼ばない(`s_slc` ガード)。SCO 複数回 CONNECTED でも各回送ってよい。
**守るべき既存機能**: SCO 開始手順(AT+BCC/`connect_audio` を呼ばない)、I2S/データ経路の呼出し順(`set_input_rate`→`reset`→`set_sco_active`→`on_audio_connected`)、再接続タイマ、LED、ボンディング、factory reset、AG 役ビルド。
**実装制約**: `esp_hf_client_volume_update` を呼ぶのは `send_spk_volume()` のみ、それを呼ぶのは BtAppT 上の `handle_*` のみ。`bt_hf_cb` では呼ばない。重い処理・ブロッキング待ちを入れない。新規 static 状態なし。コードコメントは英語(既存流儀)。

**テスト観点**
- 静的(CODER/DEBUGGER 本環境で確認可): (a) `idf.py build` 警告・エラー 0(`HF_SPK_VOLUME=15` / `-1` / `0` の 3 通り。`-1` で unused 警告が出ないこと)、(b) `HF_SPK_VOLUME=16` で `#error`、(c) `grep -n esp_hf_client_volume_update main/` が `send_spk_volume` 内の 1 件のみ、`send_spk_volume` 呼出しが `handle_conn_state`/`handle_audio_state` 内のみ、(d) `bt_hf_cb` 内に `hf_post`/`esp_hf_client_*` の追加が無い、(e) git diff が `app_config.h`/`bt_hf_client.c` のみ、(f) ログ文言 `AT+VGS=15 queued (SLC)` / `(SCO)` / `AG volume: SPK = n` が実装と一致、(g) `BT_ROLE_AG` でもビルドが通る。
- 実機のみ: (T-1) SRL3 接続でログに `AT+VGS=15 queued (SLC)` が出て切断/AT timeout が増えない、(T-2) SCO 確立で `(SCO)` 再送、(T-3) AG が `+VGS` を返す/リセットするか(`AG volume:` ログ)、(T-4) PC 録音レベルが前後で増加(同条件の会話で RMS 比較、`RMS_LOG` 併用)、(T-5) 最大音量で歪み/クリップが無いか、(T-6) SRL3 が VGS を無視する場合の判定(効果なし → 対策 B)、(T-7) 再接続・SCO 再ネゴ(CVSD↔mSBC)後も有効、(T-8) 通話開始冒頭の音量が一定か(R-2)。

### 16.12 実装後レビュー結果(DESIGNER / 2026-10-01、`git diff main/` 対象・静的のみ)
対象: `main/app_config.h`、`main/bt_hf_client.c`(他に差分なし = hf_unit / bt_app_core / bt_data_path / audio_* 無変更)。DEBUGGER の `test_report.md §13` は**計画のみで B/S/R の結果は未記入**(本レビュー時点)。

**判定: 合格(Critical 0 / High 0 / Medium 0 / Low 2)。** §16.11 の指示と一致。
確認済み:
- `esp_hf_client_volume_update` の呼出しは `send_spk_volume()` 内の 1 件のみ(grep 確認)。呼出し元は `handle_conn_state`(SLC)と `handle_audio_state`(SCO、`if (s_slc)` ガード付)で、どちらも BtAppT 上。`bt_hf_cb`(BTC)には API 呼出し・`hf_post` の追加なし、VGS 返送なし。
- `#error` 範囲チェック(`>15 || <-1`)、`-1` 時は本体が空で `(void)when` により未使用警告なし(コード上)。引数 `when` は `-1` 以外でも使用される。
- ログ文言 `AT+VGS=%d queued (SLC|SCO)` / LOGW / `AG volume: SPK|MIC = n` は設計どおり。`VOLUME_CONTROL_EVT` の `param->volume_control.type/.volume` は API ヘッダ定義と一致。`bt_hf_cb` 末尾コメントから VOLUME_CONTROL を削除済み。
- 既存手順(`connect_audio` 非呼出し、SCO 時の I2S 呼出し順 `set_input_rate`→`reset`→`set_sco_active`→`on_audio_connected`)不変。送信は I2S 設定の後に置かれ、送信結果は音声経路に影響しない。新規 static 状態なし。

| 重大度 | ID | 内容 / 推奨 |
|---|---|---|
| Low | L-1 | `HF_SPK_VOLUME=-1` 時もログが一切出ないため「無効ビルド」であることがログから分からない。必要なら `-1` 側に `ESP_LOGI("AT+VGS disabled")` を 1 度出す(任意、現状のままで可) |
| Low | L-2 | `#error` は `app_config.h` が include される全 TU で評価される(設計意図どおりで問題なし)。AG 役ビルド(B-6)・`-1`/`0`/`16`/`-2`(B-2〜B-5)は未ビルド確認のため DEBUGGER の結果記入後に突合すること |

実機専用(T-1〜T-8)は §16.11 のとおり未確認。U-10〜U-12 は実機結果待ち。

### 16.13 未解決事項
- **U-10**: SRL3 が AT+VGS を受理し音量に反映するか(実機のみ)。無視なら対策 B(DSP ゲイン)を別 CR で。
- **U-11**: 最大値 15 で歪む場合の既定値(12〜14)。実機評価後に決定。
- **U-12**: SLC 直後送信が SRL3 の SLC 確立を不安定にする場合、SLC 側を外す(R-4)。実機結果次第。
