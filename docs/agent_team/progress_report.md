# タスク管理進捗結果

最終更新: 2026-09-07 / 管理: BOSS / **状態: 完了(本環境で可能な範囲)+ rev 0.7 音質改善(T08)**

## プロジェクト / 変更要求概要

- 対象: **ダミーインカム(受信専用 / アナログ出力型)ファームウェア** 新規開発
- ターゲット: **オリジナル ESP32(ESP32-WROOM-32)+ ESP-IDF v5.4 / Bluedroid**
- 仕様書: `dummy_intercom_spec.md` **rev. 0.6**
- 概要: SENA SRL-3 のユニバーサルインターコム音声を、ESP32 が HFP Audio Gateway
  (スマホ役)として Bluetooth Classic 受信し、SCO(CVSD 8kHz)を HCI データパスで取得 →
  ジッタバッファ → I2S0 → PCM5102A(外部 I2S DAC)→ 出力段 → 3.5mm アナログ mono 出力。
  録音機材の LINE IN、または Insta360 X4 の純正マイクアダプター経由でカメラへ。
- リポジトリ: `/home/subcon/SOURCE_CODE/android_BT_TEST`

### プラットフォーム変遷(要点)

| 段階 | 内容 |
|---|---|
| 当初 | Raspberry Pi Pico 2 W + BTstack + USB マイク(UAC)を計画 |
| ISS-007 | **CYW43439 は SCO-over-HCI が動作しない**(pico-sdk #1461、2026-09 も未修正)→ Pico 2 W では SRL-3 音声を受信不可 |
| ユーザー判断 | 案B = **オリジナル ESP32 + ESP-IDF + アナログ 3.5mm 出力** を採用。仕様 rev 0.6 に改訂 |

## ビルド環境

- passwordless sudo 不可 → no-sudo ローカル導入
- ESP-IDF **v5.4**: `~/esp/esp-idf` + `~/.espressif`(`tools/setup_espidf.sh`)
  - 有効化: `export IDF_TOOLS_PATH=/home/subcon/.espressif && . ~/esp/esp-idf/export.sh`
- 旧 Pico 資産: `docs/agent_team/_archive_pico/` へ退避、`/home/subcon/opt`(pico-sdk toolchain)は残置
- **実機(ESP32 実体 / SRL-3)は本環境に無し** → HW 実測不可。
  コンパイル・静的検証・回帰まで実施、HW テストは手順書化して「未実施」で明示(ISS-002 / 仕様 R11)

## タスク一覧

| ID | タスク名 | 担当 | 優先度 | 状態 | 進捗 | 開始 | 完了 | 依存 |
|---|---|---|---|---|---|---|---|---|
| T00 | プロジェクト確認・要求整理・BOSS 文書初版・環境準備 | BOSS | 高 | 完了 | 100% | 09-02 | 09-02 | - |
| T00b | プラットフォーム再選定(ISS-007)・仕様 rev 0.6 改訂・ESP-IDF 環境準備 | BOSS | 高 | 完了 | 100% | 09-02 | 09-02 | T00 |
| T01 | ESP-IDF/ESP32 ソフト設計書作成・CODER 実装指示 | DESIGNER | 高 | 完了(software_design.md rev.5a) | 100% | 09-02 | 09-02 | T00b |
| T02 | 設計レビュー | BOSS | 高 | 完了(条件付き承認 → 条件消化) | 100% | 09-02 | 09-02 | T01 |
| T03 | ファームウェア実装 | CODER | 高 | 完了 | 100% | 09-02 | 09-02 | T02 |
| T04 | ビルド環境構築・ビルド・テスト・テスト成績書 | DEBUGGER | 高 | 完了(test_report.md v0.5) | 100% | 09-02 | 09-02 | T01 / T03 |
| T05 | 実装差分レビュー・設計整合性確認 | DESIGNER | 高 | 完了(二次レビュー合格) | 100% | 09-02 | 09-02 | T03, T04 |
| T06 | 課題対応サイクル(ISS-019 / 022 / 024) | CODER/DEBUGGER/DESIGNER | 高 | 完了(全 Resolved、再テスト・再レビュー合格) | 100% | 09-02 | 09-02 | T05 |
| T07 | BOSS 最終確認・ユーザー報告 | BOSS | 高 | 完了 | 100% | 09-02 | 09-02 | T06 |
| T08 | 音質改善(ISS-026): 48kHz アップサンプラ + HPF/ノイズゲート(`audio_dsp.c`)+ WBS/mSBC 16kHz 有効化。仕様 rev 0.7 / 設計 rev.6 | CODER | 中 | 完了(fullclean ビルド警告0、app 723KB、ホスト数値テスト。mSBC 実機動作・主観評価は未検証) | 100% | 09-07 | 09-07 | T07 |

## 実施内容 / 結果

### 設計(T01 / DESIGNER)
- ESP-IDF v5.4 の `hfp_ag` サンプル + 実 API(`esp_hf_ag_api.h` / `i2s_std.h` / GAP)を解析
- **ISS-008 調査**: ESP-IDF HFP-AG + HCI データパスの codec ネゴ不具合(#4967/BT-776)は 2020-11 に
  修正済み(IDF v4.2+)。v5.4 は sdkconfig(`BT_HFP_AUDIO_DATA_PATH_HCI=y` + `MAX_SYNC_CONN=1` +
  `WBS_ENABLE=n` 明示)で HCI SCO 動作
- ファイル単位・関数単位の実装指示(§9)、タスク文脈分離、ジッタバッファ、通話偽装、
  keepalive/reconnect、エラー/境界条件を明文化

### 実装(T03 / CODER)
- `main/` 8 ソース + 7 ヘッダ + CMake ×2 + `sdkconfig.defaults` + `partitions.csv` + `README.md`
- `idf.py build` 警告0・エラー0。app 720,480 B(factory 0x1E0000 に 63% 空き)、DRAM 静的 43KB
- §8.5 CODER 必須チェック 4 項目 自己確認済み

### テスト(T04 / DEBUGGER) — test_report.md v0.5
- S1〜S10(ビルド / 警告 / sdkconfig 整合 / サイズ / パーティション / GPIO 静的 /
  静的解析代替 / コールバック規律 / ロジックレビュー / WBS Phase2 ビルド)= **全 OK**
- HW 実測 T1〜T13 + 追加 A1〜A8 = 実機なしで **未実施**(手順は README / 成績書に記載)

### レビュー(T02, T05 / BOSS, DESIGNER)
- BOSS: 設計 rev.3 を条件付き承認。仕様 rev 0.6 を設計に合わせ微修正
- DESIGNER T05 初回: 条件付き合格(Medium 1 = ISS-019)
- DESIGNER T05 二次: 合格(Critical/High/Medium 0)

### 課題対応サイクル(T06)
| ラウンド | 内容 | 結果 |
|---|---|---|
| 1 | ISS-019(BOND-CLR 非同期レース, Medium)、ISS-022(NVS 縮退, Low) | CODER 修正 → DEBUGGER 再テスト・DESIGNER 二次レビューで **Resolved** |
| 2 | ISS-024(dispatch queue NULL ガード, Low。ISS-019 修正の副作用) | CODER 1 行修正 → DEBUGGER 最終確認で **Resolved** |

### BOSS 最終確認(T07)
- クリーンビルド(`rm -rf build sdkconfig && idf.py set-target esp32 && idf.py build`)を BOSS 自身で実行 → **成功、警告0**、`dummy_intercom.bin` 720,480 B
- 4 成果物(software_design.md / test_report.md / progress_report.md / issue_list.md)最新確認
- recv_cb/send_cb の禁止 API 不在をスポット確認
- 完了条件チェックリスト(下記)全項目 満たすことを確認

## 完了条件チェック

| 条件 | 判定 |
|---|---|
| 要求された変更が実装されている | ✅ 仕様 rev 0.6 の RQ-01〜RQ-18 を実装 |
| ソフトウェアが正常にビルドできる | ✅ `idf.py build` 警告0、BOSS がクリーンビルドで確認 |
| 必須テストが完了している | ✅ 本環境で可能な S1〜S10 完了。HW 実測は実機なしで未実施(明示) |
| DEBUGGER の最終判定が合格 | ✅ 「本環境で可能な範囲(ビルド・静的・回帰)は合格」 |
| DESIGNER 最終レビューで Critical/High なし | ✅ Critical/High/Medium 0 |
| 未解決課題が課題一覧書へ記録 | ✅ issue_list.md(Accept 4 件、実機検証委譲項目を明記) |
| ソフト設計書が最新 | ✅ software_design.md rev.5a |
| テスト成績書が最新 | ✅ test_report.md v0.5 |
| タスク管理進捗結果が最新 | ✅ 本ファイル |
| 課題一覧書が最新 | ✅ issue_list.md |
| BOSS が最終成果物を確認 | ✅ T07 |

## 最終進捗サマリー

- **全体進捗: 100%(本環境で可能な範囲)**
- Critical/High/Medium 課題: **0**。Open な Low: **0**(Accept 4 件は issue_list に理由付き記録)
- **ユーザー側で必須の残作業 = HW 実測**(ESP32 実機 + SRL-3)。
  最大の未検証点 = **T3(SCO 確立)**: ESP-IDF HFP-AG + HCI データパスの codec ネゴが
  SRL-3 実機で成立するか。次点 T4(グループ音声)、T13(CVSD 受容)、T6/T7(30 分安定性)
- 成果物: ファームウェア一式 + 4 管理文書 + README(配線図・ビルド/書き込み・HW smoke test 手順)

---

# 【変更要求 CR-010】SENA SRL3 ユニバーサルインターコムからのペアリング対応

最終更新: 2026-09-30 / 管理: BOSS / **状態: 進行中**

## 変更要求概要

- 症状: SENA SRL3 のユニバーサルインターコム・ペアリングで dummycom が検出/ペアリングできない。PC→dummycom は可能だが、その際ペアリング番号(SSP 数値確認/パスキー)を求められる。
- 要求: インカムで一般的な方式(レガシーPIN "0000" ペアリング等)で SENA から認識・ペアリング可能にする。
- 現状: `bt_hf_ag.c` は COD=Phone/Smartphone、SSP IOCAP=NONE(Just Works)、PIN フォールバック 0000。
- 体制: BOSS(本エージェント)/ DESIGNER / CODER / DEBUGGER(Agent ツールで起動。tmux ペインは Agent ツールでは生成できないため、名前付きバックグラウンドエージェントで代替)

## タスク一覧

| ID | タスク名 | 担当 | 優先度 | 状態 | 進捗 | 開始 | 完了 | 依存 | 結果/残作業 |
|---|---|---|---|---|---|---|---|---|---|
| T10-1 | 要求整理・課題/進捗初版作成 | BOSS | High | 完了 | 100% | 2026-09-30 | 2026-09-30 | - | 本節 |
| T10-2 | 既存ソフト解析・変更設計・設計書更新 | DESIGNER | High | 完了 | 100% | 2026-09-30 | 2026-09-30 | T10-1 | - |
| T10-3 | 設計確認 | BOSS | High | 完了(U-1/U-3承認) | 100% | 2026-09-30 | 2026-09-30 | T10-2 | - |
| T10-4 | 実装 | BOSS(CODER代行) | High | 完了(ビルド成功) | 100% | 2026-09-30 | 2026-09-30 | T10-3 | - |
| T10-5 | テスト準備(観点抽出) | DEBUGGER | Medium | 完了 | 100% | - | - | T10-2 | T10-4 と並行 |
| T10-6 | ビルド・テスト・成績書更新 | DEBUGGER | High | 完了(C1-C13 全合格、実機 H1-H7 未実施) | 100% | - | - | T10-4 | - |
| T10-7 | コードレビュー・設計書へ結果追記 | DESIGNER | High | 合格確定 | 100% | - | - | T10-6 | - |
| T10-8 | 修正サイクル(必要時) | CODER/DEBUGGER/DESIGNER | High | 完了(Medium 修正・Low 起動ログ修正) | 100% | - | - | T10-7 | - |
| T10-9 | BOSS 最終確認・ユーザー報告 | BOSS | High | 完了 | 100% | - | - | T10-8 | - |

## 最終進捗サマリー
2026-09-30: CR-010 実装完了(SSP 無効+固定 PIN 0000、SLC 未接続中は常時 discoverable、BT_COD_PROFILE 追加)。CODER は権限判定で拒否されたためユーザー指示により BOSS が代行実装。DEBUGGER C1-C13 全合格、DESIGNER 合格(Critical/High 0)。**実機 SRL3 は未検証(要ユーザー確認)**。起動ログ文言(Low)は修正後に再ビルド成功。

---

# 【変更要求 CR-011】HF 役追加(SRL3 ユニバーサルインターコム対応)

最終更新: 2026-09-30 / 状態: **Phase 1 実機確認済み(ペアリング・SLC・SCO 受信)。DEBUGGER 試験・DESIGNER 再レビュー未実施**

| ID | タスク | 担当 | 状態 |
|---|---|---|---|
| T11-1 | 実機ログ解析・設計依頼 | BOSS→DESIGNER | 完了(§14) |
| T11-2 | ユーザー承認(Phase1 直行/ピア保存は役別/hf_unit 取り込み) | BOSS | 完了 |
| T11-3 | 実装(bt_hf_client.c 新規、BT_ROLE、data_path 分岐、sdkconfig、hf_unit コピー) | BOSS(CODER 代行) | 完了、AG/HF 両ビルド成功 |
| T11-4 | ビルド・回帰試験 | DEBUGGER | 未着手 |
| T11-5 | コードレビュー | DESIGNER | 未着手 |
| T11-6 | SRL3 実機試験(Phase 1) | ユーザー | 完了(ペアリング/SLC/SCO 受信 OK。追加修正 3 件+IDF パッチ)。可聴確認・mSBC は未 |
| T11-7 | Phase 2(NVS 役保存、GPIO14 短押し切替、ピア別保存) | 未定 | H1 支持後 |

---

# 【変更要求 CR-012】2×ESP32 I2S デジタル直結(AUX/DAC/ADC 廃止)

最終更新: 2026-09-30 / 管理: BOSS / 状態: **T12-1〜T12-3 完了、DESIGNER 設計中**

## 変更要求概要
- ESP32 #1(`main/`, HF 役, SRL3 メッシュ会話受信)の音声を、PCM5102A/3.5mm AUX/ADC を使わず **I2S デジタル(GPIO26=BCLK, 25=WS, 27=データ)で ESP32 #2(`hf_unit/`)へ直結**。#2 は I2S スレーブ受信し HFP-HF でスマホ/Insta360 X4 へ無線マイク送信。
- 電源: USB→#1、#1 の VIN(5V)→#2 の VIN。配線図: `esp32-esp32-wiring.pdf`。
- 方針: #1 は 16kHz モノ I2S マスタ出力(48kHz アップサンプル・PCM5102A 依存を除去)、#2 は `adc_capture.c` を I2S スレーブ受信へ置換。実機無し=ビルド+静的検証まで。

## タスク
| ID | タスク | 担当 | 優先度 | 状態 | 進捗 | 開始 | 完了 | 依存 | 結果/残作業 |
|---|---|---|---|---|---|---|---|---|---|
| T12-1 | プロジェクト確認・要求整理 | BOSS | High | 完了 | 100% | 2026-09-30 | 2026-09-30 | - | 配線図PDF作成済 |
| T12-2 | 進捗/課題書 初版 | BOSS | High | 完了 | 100% | 2026-09-30 | 2026-09-30 | T12-1 | 本節 |
| T12-3 | 既存解析・変更設計(設計書 §15) | DESIGNER | High | 完了 | 100% | 2026-09-30 | 2026-09-30 | T12-2 | - |
| T12-4 | BOSS 設計確認 | BOSS | High | 完了(U-1〜U-8 決定: 下記) | 100% | 2026-09-30 | 2026-09-30 | T12-3 | - |
| T12-5 | 実装 | CODER | High | 完了(B-1〜B-5 ビルド成功) | 100% | 2026-09-30 | 2026-09-30 | T12-4 | 設計承認前に着手禁止 |
| T12-6 | テスト観点準備 | DEBUGGER | Medium | 完了 | 100% | - | - | T12-3 | T12-5 と並行 |
| T12-7 | ビルド・テスト(成績書 CR-012 節) | DEBUGGER | High | 完了(B-1〜B-5, S, R 合格) | 100% | - | - | T12-5 | 両プロジェクト |
| T12-8 | レビュー(設計書へ追記) | DESIGNER | High | 完了(合格 Low5) | 100% | - | - | T12-7 | - |
| T12-9 | 修正サイクル | CODER/DEBUGGER/DESIGNER | High | 完了(L-3,L-5 修正) | 100% | - | - | T12-8 | - |
| T12-10 | BOSS 最終確認・報告 | BOSS | High | 完了 | 100% | - | - | T12-9 | - |

## 運用メモ
- 実行環境は tmux セッション外のため、teammate は tmux ペインではなくバックグラウンドのサブエージェントとして起動。

## BOSS 設計判断(T12-4、§15.12 未解決事項への回答)
U-1 既定は I2S(AUDIO_OUT_MODE=1 / AUDIO_IN_MODE=1)で承認(ユーザー要求)。U-2 1 サンプル挿入/削除で承認。U-3 #1 側補正は不要(実機で判断)。U-4 無音補填で承認。U-5 仕様書改訂は BOSS が後で実施。U-6 送信先は Insta360 X4 またはスマホ(ユーザー回答済)。U-7 devkit(ユーザー回答済、直列抵抗は任意)。U-8 BT_ROLE は HF のまま(ユーザー承認済)。

## 最終進捗サマリー(CR-012)
2026-09-30: 実装完了。ビルド 5 通り警告 0、静的検証・回帰合格、DESIGNER レビュー合格(Critical/High/Medium 0、Low 5 のうち 2 修正・3 受容)。**実機(ESP32×2/SRL3/スマホ)未検証**: I2S スロット左右・BCLK エッジ・ドリフト・CVSD 音質は初回通電で確認要。ADC/PCM5102A は #if で残置(削除は実機検証後の別 CR)。仕様書 rev 改訂(U-5)は未実施(Open)。
