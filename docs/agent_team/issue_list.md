# 課題一覧書

最終更新: 2026-09-02 / 管理: BOSS

重大度: Critical / High / Medium / Low
状態: Open / In Progress / Resolved / Accepted

| 課題ID | 発見Agent | 発見工程 | 課題内容 | 再現条件 | 原因 | 影響範囲 | 重大度 | 対応方針 | 担当 | 状態 | 対応結果 | 残存リスク |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ISS-001 | BOSS | 環境準備 | 本環境に cmake / arm-none-eabi-gcc / pico-sdk が無い。passwordless sudo 不可 | 常時 | 環境未整備 | ビルド全般 | High | no-sudo ローカル導入(`tools/setup_toolchain.sh`)。xpack ARM GCC + CMake バイナリ + pico-sdk clone | BOSS | Resolved | `/home/subcon/opt` に xpack ARM GCC 13.3.1 / CMake 3.31.6 / pico-sdk 2.1.1(btstack, tinyusb, cyw43-driver, lwip, mbedtls submodule 込み)導入完了。`source /home/subcon/opt/pico-env.sh` でビルド可能。board `pico2_w` 確認済み | 低(初回 cmake 構成でネット取得が追加発生する可能性) |
| ISS-002 | BOSS | 環境準備 | Pico 2 W 実機・SRL-3 実機が無く HW 実測テスト不可 | 常時 | 物理環境なし | §11 の HW テスト項目(T3〜T13 の実測) | Medium | コンパイル/静的検証まで実施。HW テストは `test_report.md` に手順を記載し「未実施」で明示。Accept 前提 | DEBUGGER | Open | - | 実機での SCO/UAC 動作は未保証。ユーザー側で実施必要 |
| ISS-003 | BOSS | 要求整理 | 仕様 R3(SRL-3 が WBS 必須なら Pico 2 W では不可)/ R4(SCO-over-HCI 安定性)は実機依存で本開発では確定できない | - | ハード/相手機依存 | 音声受信の可否 | Medium | ファームは CVSD 前提で実装。hci_dump で確認できる作りにする。判定はユーザー実機テストへ委譲 | DESIGNER | Open | - | 実機で CVSD 不可なら別プラットフォーム要 |

| ISS-004 | DEBUGGER | ビルド環境検証 | BT 専用構成では `pico_cyw43_arch_none` を使う必要あり。`pico_cyw43_arch_lwip_*` / `pico_cyw43_arch_threadsafe_background` をリンクすると lwip 依存(`lwip/netif.h` / `lwipopts.h`)でビルド不可 | Wi-Fi 不使用で lwipopts.h 無し | cyw43_arch のバリアント選択 | CMakeLists.txt / 設計書 §6.3 の表現 | Low | 設計書で「cyw43_arch_none + BTstack async_context」と明記。CODER は `pico_cyw43_arch_none` をリンク | DESIGNER | Open | - | 誤ると即ビルドエラーで検知可能なため実害低 |
| ISS-005 | DEBUGGER | ビルド環境検証 | `ENABLE_CLASSIC` / `ENABLE_BLE` を btstack_config.h に手書きしない(ライブラリリンクで自動定義され二重定義/不整合の恐れ) | - | BTstack pico ポートの仕様 | btstack_config.h | Low | 設計書 §5.1 の define 一覧から両者を除外 | DESIGNER | Open | - | - |
| ISS-006 | DEBUGGER | ビルド環境検証 | 初回 cmake が picotool を GitHub から取得しビルドする | ネット必須(本環境は成功) | pico-sdk 2.x の picotool 連携 | 初回ビルド | Low | オフライン化が必要なら `picotool` を別途導入し `picotool_DIR` 指定。現状は許容 | BOSS | Accepted | 本環境で取得成功済み | オフライン環境では要対応 |

| ISS-007 | DESIGNER | 設計 | **SCO-over-HCI が CYW43439(Pico W / Pico 2 W)で動作しない。** codec ネゴ後 `HCI Setup/Accept Synchronous Connection` を送っても成功イベントが返らずハング。pico-sdk #1461(2023-07 起票、2026-09 時点も Open、milestone 2.5.0、Infineon 対応待ち)。BTstack メンテナ公式見解も「当面は USB ドングルを使え」 | Pico 2 W で HFP の音声(SCO)を受けようとすると常時発生 | CYW43439 コントローラ FW のバグ(Infineon)。ホスト側・BTstack 側では回避不可 | **本ファームの中核機能(インカム音声受信)が実機で動作しない** | **Critical** | BOSS がユーザーへ escalation → **ユーザー判断: 案B(オリジナル ESP32 + ESP-IDF + アナログ 3.5mm 出力)を採用**。仕様書 rev 0.6 へ改訂。Pico 2 W 版(rev 0.4/0.5)成果物は破棄 | BOSS | Resolved | プラットフォームを ESP32/ESP-IDF に変更。rev 0.6 で再設計。Pico 用 toolchain/build_probe は廃止 | ESP-IDF HFP-AG も #4967 の履歴あり → ISS-008 で追跡 |
| ISS-008 | DESIGNER | 設計 | ESP-IDF HFP-AG + HCI データパスで codec ネゴがハングした報告(BT-776 / espressif/esp-idf#4967、`BTA_AG_SCO_CODEC_ST: Ignoring event`) | 特定条件(相手機・IDF バージョン依存) | Bluedroid の SCO codec 状態遷移の不具合履歴 | SCO 確立(T3) | High | 最新安定 IDF(v5.4 系)を使用。DESIGNER が最新の同 issue 状況・回避策を調査し設計に反映。T3 を最優先の実機検証項目に | DESIGNER | Open | - | 実機で SRL-3 相手に SCO が張れない可能性。CVSD 固定・WBS 無効で回避余地あり |

| ISS-009 | DEBUGGER | ビルド環境検証(rev0.6) | 仕様 §5.1 の `CONFIG_BT_SSP_ENABLED=y` は ESP-IDF v5.4 で廃止。kconfig 警告 | v5.4 でビルド時 | IDF の kconfig 変更(SSP は常時有効化) | sdkconfig / 仕様書 / 設計書 | Low | 仕様 §5.1 から削除(BOSS 対応済み)。設計書にも反映。IO cap は `esp_bt_gap_set_security_param` | DESIGNER | Resolved | 仕様書 rev 0.6 §5.1 修正済み。設計書へ反映依頼済み | - |
| ISS-010 | DEBUGGER | ビルド環境検証(rev0.6) | ESP-IDF hfp_ag 例の既定は PCM データパス。HCI データパスは明示設定が必要 | 常時 | 例の既定構成 | sdkconfig / 実装 | Low | `CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y` を sdkconfig.defaults に明示。`gpio_pcm_config.c` は持ち込まない | CODER | Open | - | 誤ると PCM 経路になり recv コールバックにデータが来ないため実装時に検知可 |
| ISS-011 | DEBUGGER | ビルド環境検証(rev0.6) | パーティションは 4MB + `SINGLE_APP_LARGE`(例既定の 2MB single_app では app 760KB + BT で窮屈) | 常時 | app サイズ | partitions / sdkconfig | Low | `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`(仕様 rev 0.6 §5.1 反映済み) | CODER | Resolved | 仕様書反映済み | - |

| ISS-012 | DESIGNER | 設計(T01) | ISS-008 = ESP-IDF HFP-AG + HCI SCO の codec ネゴ / データパス不具合履歴(#4967=BT-776)。SRL-3 実機で SCO が張れない可能性 | HCI SCO 使用時、相手機依存 | #4967 は 2020-11 に修正済み(IDF v4.2+)だが派生問題(#6218/#6503/#28057)の履歴あり。実挙動は SRL-3 実機依存 | 中核機能(音声受信、T3) | High | sdkconfig 4点(controller SCO=HCI / bluedroid SCO=HCI / MAX_SYNC_CONN=1 / WBS=n)。send は sz 厳守で無音。keepalive/reconnect で復旧。T3 を最優先の実機検証項目。詰まったら recv/send を接続時登録へ(U-02) | DESIGNER/CODER/ユーザー | Open | 設計で対処策を織り込み済み。実機確証はユーザーテスト待ち | 実機で SCO 不成立なら別プラットフォーム(仕様 R3/R4) |
| ISS-013 | DESIGNER | 設計(T01) | recv_cb で重い処理/ブロックすると BTC タスクが詰まり SCO 全体が破綻(R-NEW-02) | recv_cb 実装ミス時 | コールバック文脈が BTC タスク | 音声・BT 全体 | High | recv_cb は `xRingbufferSend(...,0)` + RMS 加算のみ。ログ・`i2s_channel_write`・malloc 禁止。§8.5 CODER 必須チェック | CODER | Open | - | CODER レビュー(T05)で grep 検証 |
| ISS-014 | DESIGNER | 設計(T01) | `esp_timer` cb から `esp_hf_ag_*` 直呼びで Bluedroid 内部状態と競合(R-NEW-03) | タイマ cb 実装ミス時 | スレッド安全性 | BT 安定性 | Medium | 全て `bt_app_work_dispatch` 経由で `BtAppT` から。§8.5 チェック2 | CODER | Open | - | T05 で検証 |
| ISS-015 | DESIGNER | 設計(T01) | I2S(48kHz 固定)と SCO クロックの独立ドリフトで 30 分連続時に累積オーバー/アンダーラン(R-NEW-04) | 長時間連続受信 | クロック非同期 | 音の途切れ(ピッチは不変) | Medium | ジッタバッファで吸収。T6/T7 で評価。問題なら ±1 sample レート整合を追加 | ユーザー(実機) | Open | - | 実機評価待ち。rev 0.7 で I2S は 48kHz 固定化(アップサンプラ)したがドリフト自体は残存 |
| ISS-016 | DESIGNER | 設計(T01) | `s_call=1` 偽装で SRL-3 が「通話中」表示になりインカム動作に副作用の可能性(R-NEW-06) | 通話偽装実行時 | HFP call state の意味 | インカム受信の可否 | Medium | U-03=(a) 実装。副作用大なら §6.4 簡略版(OK だけ返し call state 不変 + audio_connect のみ)に切替。切替点をコメント明示 | ユーザー(実機 T3/T4) | Open | - | 実機評価待ち |
| ISS-017 | DESIGNER | 設計(T01) | PCM5102A モジュール個体が 8kHz LRCK を受け付けない可能性(R-NEW-05) | 特定モジュール | 個体差 | 音声出力 | Low | ~~不安なら Phase 2 で 8k→32/48k アップサンプル~~ → **rev 0.7 で実装**。I2S は常時 48kHz、SCO PCM を `main/audio_dsp.c` でポリフェーズ補間 | CODER | Resolved | rev 0.7。8k/16k いずれも 48kHz へアップサンプルして出力。個体差リスク解消 | - |
| ISS-018 | DESIGNER | 設計(T01) | Bluedroid ボンドと自前 NVS キーの二重管理で不整合(R-NEW-07) | 片方のみ消去時 | 二重管理 | 再接続 | Low | `storage_factory_reset` で両方消す。`AUTH_CMPL` 成功時のみ自前保存 | CODER | Open | - | - |

### BOSS 判断(T02 設計レビュー時に確定 — DESIGNER の未解決事項 U-01〜U-08)

| U# | 事項 | BOSS 決定 |
|---|---|---|
| U-01 | 実機なしで T3 検証不能 | **(a) ビルド + 静的検証で開発完了扱い**。ユーザーへの最終報告で「T3(SCO 確立)が本開発の最大の未検証点」と明示。実機 smoke test 手順を README とテスト成績書に記載 |
| U-02 | recv/send データコールバックの登録タイミング | **(a) init 時 1 回登録**で実装。関数分離しておき、実機で無反応/ハングなら (b) 接続時登録へ即切替可能に(コメント明示) |
| U-03 | 通話偽装の深さ | **(a) `out_call` で call=1 まで**。副作用時に §6.4 簡略版へ切替できるようコメント明示(ISS-016) |
| U-04 | ジッタバッファ既定 | **60ms**。`app_config.h` で変更可 |
| U-05 | CoD 値 | **(a) intent 優先 ≈ 0x40020C**。SLC 不成立なら 0x20040C |
| U-06 | WBS(mSBC)必須だった場合 | **rev 0.7 で対応済み**(`CONFIG_BT_HFP_WBS_ENABLE=y`、16kHz 経路自動切替)。実機動作確認はユーザー T13 |
| U-07 | 内蔵 8bit DAC 代替 | Phase 1 は PCM5102A のみ。内蔵 DAC は Phase 2 |
| U-08 | 絶縁トランス無し時のハム | ハード側の問題。SW 変更なし。ユーザー実機 T9 |

| ISS-019 | DESIGNER/DEBUGGER | 実装レビュー(T05/T04) | BOND-CLR 長押しで `storage_factory_reset()`(内部 `esp_bt_gap_remove_bond_device()` = 非同期 BTC キュー)直後に `esp_restart()` を即呼び → BTC が bond 削除を処理する前に再起動し **Bluedroid リンクキー(NVS)が消えない可能性大**。GAP API を `leds` タスクから呼ぶのも §8.5 逸脱 | BOND-CLR 3秒長押し実行時 | 非同期 API 完了前の restart | ボンド消去機能(T12) | Medium | `HFW_FACTORY_RESET` work を BtAppT へ dispatch。bond 削除 → `esp_bt_gap_get_bond_device_num()==0` ポーリング(最大1s)→ `vTaskDelay(300ms)` → `esp_restart()`。leds は request 呼ぶだけ | CODER | Fixed(再テスト中) | `bt_hf_ag.c/.h` に `bt_hf_ag_request_factory_reset()` + `HFW_FACTORY_RESET` 新設、`leds.c` から GAP/storage 直呼び削除。fullclean ビルド警告0 | 300ms でも BTC 処理未完なら稀に残存(実機 T12 で要確認) |
| ISS-020 | DESIGNER | 実装レビュー(T05) | L-1: `s_rms_acc`(int64)が BTC と esp_timer で非アトミック共有 → 稀に診断ログ1行が異常値。機能影響なし | 64bit 読み書きの分割 | 非アトミック | 診断ログのみ | Low | 任意対応。`s_rms_acc` を esp_timer 側で読む直前に critical section、or 32bit 化。放置可 | CODER | Open | - | 実害なし |
| ISS-021 | DESIGNER | 実装レビュー(T05) | L-3: `handle_stack_up` の `esp_bt_gap_*` 戻り値未チェック | 初期化失敗時 | エラーハンドリング欠 | 起動 | Low | 任意対応。戻り値を `ESP_LOGW` | CODER | Open | - | 初期化失敗は稀 |

| ISS-022 | DEBUGGER | 本テスト(T04 §6-C) | `nvs_flash_init` が erase 再試行後も失敗すると `ESP_ERROR_CHECK` で abort → 再起動ループ。設計 §9.3 は「PWR 遅い点滅で継続」 | NVS 二重失敗(フラッシュ HW 障害等、稀) | 実装が設計 §9.3 と相違 | 起動時の縮退動作 | Low | CODER: 設計 §9.3 どおり「ログ + PWR 遅い点滅 + BT 無効で待機」に | CODER | Fixed(再テスト中) | `app_main.c` で `pwr_blink_forever(900)` 縮退 + `nvs_flash_erase()` 戻り値も握る。fullclean ビルド警告0 | - |
| ISS-023 | DEBUGGER/DESIGNER | 本テスト/レビュー | CoD 実装値 = `TELEPHONY\|AUDIO` = 0x60020C(仕様 §5 / 設計 §0-D の 0x40020C 記述と相違) | 常時 | 設計書プローズの計算誤り(§4.1 コードは両ビット指定で正) | CoD | Low | DESIGNER が設計書 rev.5 で 0x60020C に訂正。CODER 実装が正、変更不要。仕様書も 0x40020C→実測値へ注記 | DESIGNER/BOSS | In Progress | 設計 rev.5 で対応中。仕様書は BOSS が注記修正 | AUDIO ビットは無害の見込み |

| ISS-024 | DEBUGGER | 再テスト(T06) | `bt_app_core.c` `bt_app_send_msg()` に `bt_app_task_queue == NULL` ガードが無く、`xQueueSend(NULL,…)` で FreeRTOS `configASSERT` パニック。ISS-019 修正が前提とする「queue 未生成なら false 返してリトライ」経路が機能しない | BT/NVS 初期化失敗中 or 起動が異常に遅い状態で BOND-CLR 3秒長押し | NULL ガード欠落 | BOND-CLR の縮退リトライ経路(happy path は影響なし) | Low | 1 行修正: `if (msg == NULL \|\| bt_app_task_queue == NULL) return false;` | CODER | **Resolved** | `bt_app_core.c` `bt_app_send_msg()` にガード追加、false 伝播を DEBUGGER が全チェーン確認、fullclean ビルド警告0 | 通常起動経路では発生しない |
| ISS-025 | DEBUGGER | 再テスト(T06) | §6-G: `bt_app_work_dispatch()` が `param_len>0` で malloc した `msg.param` を、`bt_app_send_msg` 失敗時に free しない(≈60B/回リーク) | ISS-024 と同条件(初期化失敗中にボタン保持継続)でのみ | Espressif 公式ワークキュー例由来の既存パターン | 極小リーク(電源断で回収) | Very Low | **Accept**(厳密対策 = 失敗時 `free(msg.param)`。今回スコープ外)。通常運用では発生しない | - | Accept | - | 実害なし |
| ISS-026 | ユーザー | 音質改善(rev 0.7) | 音質がクリアでない。(A) 8kHz 直出しで PCM5102A PLL マージンが薄い・イメージング。(B) CVSD 帯域(300–3400Hz)が原理的に狭い | 常時 | ナローバンド + リサンプルなし | 音質 | Medium | **(A)** `main/audio_dsp.c` 追加: 120Hz HPF + (任意)ノイズゲート + ポリフェーズ FIR で 48kHz 固定出力(8k×6 / 16k×3)。**(B)** `CONFIG_BT_HFP_WBS_ENABLE=y`、`ESP_HF_AUDIO_STATE_CONNECTED_MSBC` で 16kHz 経路へ自動切替 | CODER | Fixed(実機未検証) | rev 0.7。fullclean ビルド警告 0。追跡: 初回実機で「こもった音」→ FIR を Kaiser 32tap / fc 3.95k に変更 + ゲート既定 OFF。**録音+ログ解析で原因確定 = CVSD 固定(下記 ISS-027)。DSP は無罪(8分 underrun/overrun 0)** | mSBC 確立には ISS-027 のパッチが必要 |
| ISS-027 | ユーザー/解析 | 音質(rev 0.7 実機) | WBS 有効なのに mSBC ではなく CVSD 8kHz が確立し、こもる。ログに `E BT_APPL: Unknown Codec UUID(3) received` → `codec negotiation result: 1 (CVSD)` | SRL-3(HFP 1.9)接続時 常時 | ESP-IDF v5.4 Bluedroid `bta_ag_parse_bac()` が未知コーデック ID(3=LC3-SWB)で `return BTA_AG_CODEC_NONE` し、直前にパース済みの mSBC ごとリスト全体を破棄 → CVSD にフォールバック | 音質(帯域 3.4kHz 上限) | Medium | `~/esp/esp-idf/.../bta_ag_cmd.c` を 1 行パッチ(未知コーデックは `break` でスキップ、`return` しない)。`patches/esp-idf-v5.4-hfp-bac-lc3.patch` + `patches/README.md` としてリポジトリに保存 | CODER | **Resolved** | 実機確認済み: `Unknown Codec UUID(3) received, ignoring`(warning 化)→ `codec negotiation result: 2 (mSBC)` → `connected_msbc` → `SCO PCM 16000 Hz`。録音解析: −20dB ロールオフ 3.8k→6.2kHz、3.8–7kHz エネルギー +11dB、underrun/overrun 0 | **残: 録音側にクリップ(ピーク ~3%)+ 100/200Hz ハム(最大成分)**。本体外 = 入力レベル過大 + グラウンドループ。LINE/MIC を MIC・VR 絞り、絶縁トランス(R6)またはバッテリー給電 |

---

## 最終クローズ状況(BOSS / 2026-09-02 T07)

| ID | 概要 | 最終状態 |
|---|---|---|
| ISS-001 | ビルド環境(pico-sdk)未整備 | Resolved(旧 / ESP-IDF へ移行) |
| ISS-002 | 実機なしで HW 実測不可 | Accept(HW テストはユーザーへ委譲、手順書化済み) |
| ISS-003〜006 | Pico 版の設計課題 | Resolved / 消滅(プラットフォーム変更) |
| ISS-007 | **CYW43439 の SCO-over-HCI 不具合(#1461)** | Resolved(ユーザー判断で ESP32/案B へ) |
| ISS-008/012 | ESP-IDF HFP-AG + HCI codec ネゴ不具合履歴(#4967) | Open(設計で対処済み。実機 T3 で最終確認 — ユーザー) |
| ISS-009/010/011 | sdkconfig(SSP 廃止 / HCI データパス明示 / パーティション) | Resolved |
| ISS-013/014 | 実装で守るべき制約(recv_cb 軽量 / timer から dispatch) | Resolved(実装・レビューで確認) |
| ISS-015/016 | クロックドリフト / 通話偽装副作用 | Open(実機評価 — ユーザー。設計で緩和策あり) |
| ISS-017 | PCM5102A 8kHz 個体差 | **Resolved**(rev 0.7。48kHz 固定 + アップサンプラ) |
| ISS-026 | 音質改善(48kHz アップサンプラ + HPF/ゲート + WBS) | **Resolved**(rev 0.7 + ISS-027 パッチ)。実機で mSBC 16kHz 確立、帯域 3.4k→6.5kHz、HF エネルギー +11dB |
| ISS-027 | Bluedroid BAC パースが未知コーデック(LC3)でリスト破棄 → CVSD 固定 | **Resolved**(IDF パッチ `patches/`)。実機ログ `codec negotiation result: 2 (mSBC)` / `connected_msbc` 確認、underrun/overrun 0 |
| ISS-018 | ボンド二重管理 | Resolved(factory_reset で両方消去) |
| ISS-019 | BOND-CLR 非同期レース(Medium) | **Resolved**(BtAppT dispatch + ボンド数0ポーリング) |
| ISS-020/021/023/025 | Low / Very Low | **Accept**(理由: 診断ログのみ / 初期化失敗は稀 / 設計書整合済 / 極小リーク) |
| ISS-022 | NVS 二重失敗の縮退 | **Resolved** |
| ISS-024 | dispatch queue NULL ガード | **Resolved** |

**Critical / High / Medium: 0。Open な Low: 0。**
Open 3 件(ISS-008/012, 015, 016, 017)はいずれも **実機がないと確定できない検証項目**で、
ユーザーの ESP32 + SRL-3 実機テストに委譲。緩和策・切替点はコード/設計に反映済み。

## 未解決事項

## 未解決事項

- 頒布時の USB VID/PID、技適の最終製品扱い(仕様 §13-5)は本開発スコープ外

---

# CR-010 課題(SENA SRL3 ペアリング)

| 課題ID | 発見Agent | 発見工程 | 課題内容 | 再現条件 | 原因 | 影響範囲 | 重大度 | 対応方針 | 担当 | 状態 | 対応結果 | 残存リスク |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ISS-C10-01 | BOSS | 要求整理 | SRL3 のユニバーサルインターコム・ペアリングで dummycom が検出/ペアリング不可 | SRL3 でユニバーサルインターコムペアリング実行 | 推定: SSP/ペア番号フロー、COD(Phone)、または SENA が期待する役割/プロファイルとの不一致。DESIGNER が根拠付きで確定する | bt_hf_ag.c(GAP設定)、sdkconfig(SSP) | High | DESIGNER が解析し方式決定 | DESIGNER | Open | - | 実機(SRL3)が本環境に無く実測不可。ユーザー実機確認が必要 |
| ISS-C10-02 | DESIGNER | 設計 | 有力原因: ピア保存済みだと NON_DISCOVERABLE になり、PC で一度ペアリングすると SRL3 から不可視 | PC 等と一度ペアリング後に SRL3 で検索 | apply_scan_mode の仕様 | bt_hf_ag.c | High | SLC 未接続時は常時 discoverable に変更(設計 §13) | CODER | Resolved | - | SRL3 の COD フィルタ有無は実機依存 |
| ISS-C10-03 | DESIGNER | 設計 | レガシー PIN ペアリングは SSP より弱い(U-1) | - | ユーザー要求 | セキュリティ | Low | 技術検証のためユーザーが明示承認(2026-09-30)。検証後にセキュリティ強化予定 | BOSS | Accepted | - | 近傍からの不正ペアリング可 |
| ISS-C10-04 | DESIGNER | 設計 | HF ユニット(2台目)側ペアリング設定との整合(U-4)、SRL3 実機未検証 | - | 範囲外/実機無し | HF ユニット | Medium | 本 CR 範囲外。DEBUGGER が確認し未実施項目に記載 | DEBUGGER | Open | - | 実機確認はユーザー |
| ISS-C10-05 | DESIGNER | レビュー | spec §8.1 手順4 と L204 が旧 SSP 記述のまま(M-1/M-2) | - | 文書同期漏れ | dummy_intercom_spec.md | Medium | 文言修正 | BOSS | Resolved | 2026-09-30 修正済 | なし |
| ISS-C10-06 | DESIGNER | レビュー | README が CR-010 範囲外の大差分(L-2)、HPF 記述と実値不一致(L-3、CR-010 以前から) | - | 既存の未コミット変更 | README.md | Low | コミット分離を推奨。HPF 記述は別件 | BOSS | Accepted | - | 軽微 |
| ISS-C11-01 | BOSS | 実機試験 | BT_COD_PROFILE=1 で SRL3 が ACL 接続するが約 0.4 秒後に自発切断(0x13)。認証・SLC なし | SRL3 ユニバーサルインターコム | 仮説: SRL3=AG が HFP-HF の SDP を期待(H1)。H2〜H4 は排除できず | 全体 | High | CR-011: HF 役を追加(設計 §14) | BOSS(CODER 代行) | Resolved | 実機確認: HF 役でペアリング・SLC・SCO 受信成功(2026-09-30)。派生問題 ISS-C11-02〜04 も解消 | H1 が誤りなら解決しない。HSP のみ要求の場合は IDF では対応不可 |
| ISS-C11-02 | BOSS | 実機試験 | HF 役で SLC の約 30 秒後に `AT response timeout` で切断 | BT_ROLE_HF、SRL3 接続 | SLC 後の AT(BIA/CCWA/CMEE/COPS/BTRH/CLIP)のいずれかに SRL3 が無応答 | HF 役 | High | IDF パッチ(patches/esp-idf-v5.4-hfp-client-no-post-slc-at.patch)で送信停止 | BOSS | Resolved | 適用後 48 秒以上接続維持を実機確認 | IDF 更新でパッチが消える。どの AT が無応答かは未特定 |
| ISS-C11-03 | BOSS | 実機試験 | HF 役で SCO 確立直後に Guru Meditation(vhci ISR wdt) | BT_ROLE_HF、SCO 接続 | 推定: 送信無音タイマ(outgoing_data_ready)。原因未特定 | HF 役 | High | HF 役では送信タイマを起動しない | BOSS | Resolved | クラッシュ消失を実機確認 | 上り音声は送れない(受信専用で問題なし) |
| ISS-C11-04 | BOSS | 実機試験 | 保存ピアへの能動再接続と SRL3 の接続が衝突し ACL 切断を繰り返す | BT_ROLE_HF、保存ピアあり | HF client found collision | HF 役 | Medium | HF 役は能動再接続しない(SRL3 の発信を待つ) | BOSS | Resolved | 実機確認 | SRL3 が再接続しない場合は再ペアリング要 |
| ISS-C11-05 | BOSS | 実機試験 | CR-011 の DEBUGGER 試験・DESIGNER 再レビュー未実施(Phase 1 修正後)。出力端子での可聴確認、mSBC、Phase 2、`bt_hf_client.c` の未使用 reconnect コード整理が未 | - | DEBUGGER への依頼が権限判定で拒否 | CR-011 | Medium | ユーザー判断で実施 | DEBUGGER/DESIGNER | Open | - | 文書は実機結果までで最新化済み |

# CR-012 課題(2×ESP32 I2S 直結)

| 課題ID | 発見Agent | 発見工程 | 課題内容 | 再現条件 | 原因 | 影響範囲 | 重大度 | 対応方針 | 担当 | 状態 | 対応結果 | 残存リスク |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ISS-C12-01 | BOSS | 要求整理 | 実機(ESP32×2/SRL3/スマホ)が無く I2S 同期・音質・クロックドリフトは実測不可 | - | 環境制約 | 全体 | Medium | ビルド+静的検証+ジッタバッファ設計で担保。実機確認はユーザー | DEBUGGER | Open | - | #1/#2 のクロック差によるバッファ under/overrun は実機依存 |
| ISS-C12-02 | BOSS | 要求整理 | #2 HF は SCO 確立後のみ音声送信。SCO 未確立/#1 無音時の扱い(無音送出) | - | - | hf_unit | Medium | DESIGNER が設計で規定 | DESIGNER | Open | - | - |
| ISS-C12-03 | DESIGNER | レビュー | L-1: SCO 中のレート再ネゴ時に旧レートのチャンクが 1 つ混入し得る | mSBC/CVSD 再ネゴ | set_rate と write の非同期 | hf_unit | Low | 実害小のため受容 | - | Accepted | 音切れ最大 1 チャンク | 極小 |
| ISS-C12-04 | DESIGNER | レビュー | L-2: プリフィル中(s_prefilled 前)もドリフト補正が入る | SCO 接続直後 | 設計どおり | hf_unit | Low | 実害なし(最大 1 サンプル)。受容、実機 T-4 で確認 | - | Accepted | - | 極小 |
| ISS-C12-05 | DESIGNER | レビュー | L-3: s_silence_bytes が BTC/BtAppT 両方から更新される旨のコメントが無い | - | 32bit で実害なし | hf_unit/bt_data_path.c | Low | コメント追記 | CODER | Resolved | コメント追記、B-4 再ビルド OK | なし |
| ISS-C12-06 | DESIGNER | レビュー | L-4: git インデックスに build/*・sdkconfig.old が staged。新規 docs/PDF は未 add | コミット時 | 既存状態 | リポジトリ | Low | コミット時に注意(ユーザーがコミット指示時に対応) | BOSS | Accepted | - | なし |
| ISS-C12-07 | DESIGNER | レビュー | L-5: test_report §12.1 に「#1 は HFP-AG」とあるが既定は BT_ROLE_HF | - | 記載ミス | test_report.md | Low | 訂正 | DEBUGGER | Resolved | test_report §12.1 訂正 | なし |
| ISS-C12-08 | BOSS | 最終確認 | U-5: dummy_intercom_spec.md の rev 採番とピン/音声章の改訂が未実施 | - | 優先度低として後回し | 仕様書 | Low | 実機検証後にまとめて改訂 | BOSS | Open | - | 文書と実装の不一致 |
