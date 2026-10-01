/*
 * Dummy Intercom HF unit - compile-time configuration.
 * All GPIO assignments, buffer sizes, timer periods and names live here.
 * See software_design.md 13.5.2.
 */
#pragma once

/* ---- GPIO(仕様 §4.5。AGユニットと同一番号だが別基板なので衝突なし)---- */
#define PIN_LED_PWR         32
#define PIN_LED_LINK        33
#define PIN_LED_SCO         13
#define PIN_BTN_BOND        14
#define BTN_HOLD_MS         3000

/* ---- 音声入力モード(CR-012)----
 * AUDIO_IN_I2S(既定)= 1台目 ESP32 から I2S スレーブで 16kHz モノ受信。
 * AUDIO_IN_ADC = 従来の ADC1(GPIO34)入力。ロールバック用に残す。 */
#define AUDIO_IN_ADC         0
#define AUDIO_IN_I2S         1
#define AUDIO_IN_MODE        AUDIO_IN_I2S

#if AUDIO_IN_MODE == AUDIO_IN_I2S
/* I2S スレーブ RX(1台目の I2S マスタ出力と同一配線: 26/25/27)。16kHz/16bit/
 * モノ/Philips/左スロット固定。SCO が 8kHz(CVSD)でも I2S は常に 16kHz。 */
#define PIN_I2S_BCLK          26
#define PIN_I2S_WS            25
#define PIN_I2S_DIN           27
#define I2S_IN_RATE_HZ        16000
#define I2S_IN_DMA_DESC_NUM   6
#define I2S_IN_DMA_FRAME_NUM  128        /* 8 ms/desc, 6 desc = 48 ms */
#define I2S_IN_CHUNK_SAMPLES  128        /* 1 read = 128 sample = 256 B = 8 ms */
#define I2S_IN_READ_TIMEOUT_MS 50        /* BCLK 断検出 */
#define I2S_IN_LOST_MS        200        /* 連続タイムアウトでクロック喪失扱い */
/* ジッタバッファ(SCO レート換算 ms で規定) */
#define TX_PREFILL_MS         30
#define TX_TARGET_MS          40
#define TX_HYST_MS            20         /* target ± 20ms でドリフト補正 */
#define DRIFT_MIN_INTERVAL_MS 100        /* 補正の最短間隔(最大 10 sample/s) */
#define SILENCE_FILL_MAX_MS   200        /* send_cb 無音補填の上限 */
#else
/* ---- ADC 入力(§13.2.4 の制約により GPIO25/26/27 は不可。ADC1 のみ使用) ---- */
#define PIN_ADC_AUDIO_IN     34            /* ADC1_CHANNEL_6, 入力専用ピン。§13.9: 基板露出要確認 */
#define ADC_CHANNEL_USED     ADC_CHANNEL_6
#define ADC_UNIT_USED        ADC_UNIT_1
#endif

/* ---- Bluetooth(仕様 §5.5)---- */
#define BT_DEVICE_NAME       "DummyIntercomMic"
#define SCO_KEEPALIVE_MS     3000
#define ACL_RECONNECT_MS     5000

/* Class of Device: major=Audio/Video, minor=Microphone, service=Audio。
 * esp_gap_bt_api.h は AV の minor 値を定義していない(Peripheral の minor しか
 * enum化されていない)ため、Bluetooth SIG Assigned Numbers (Baseband, Major
 * Device Class = Audio/Video の minor field) の "Microphone" = 0b000100 を
 * ローカル定数として定義する。実機(X4)で不適合なら Headset(0x01)/
 * Hands-free(0x02)へ変更(§13.9)。 */
#define BT_COD_MAJOR_AV           4        /* ESP_BT_COD_MAJOR_DEV_AV */
#define BT_COD_MINOR_MICROPHONE   0x04

#if AUDIO_IN_MODE == AUDIO_IN_ADC
/* ---- ADC / オーディオ(仕様 §6.2)----
 * ESP32 の adc_continuous は 20kHz 未満を受け付けない(SOC_ADC_SAMPLE_FREQ_THRES_LOW,
 * 実測 §13.2.4)。8kHz/16kHz を直接サンプリングできないため、固定 32kHz で
 * オーバーサンプルし、SCOのレートに整数比(÷4 / ÷2)で間引く。 */
#define ADC_SAMPLE_RATE_HZ        32000
#define ADC_BIT_WIDTH             12                 /* SOC_ADC_DIGI_MAX_BITWIDTH (esp32) */
#define ADC_ATTEN_USED            ADC_ATTEN_DB_12     /* 0-3.3V 付近をカバー。旧称 DB_11 は非推奨 */
#define ADC_DECIM_FACTOR_CVSD     4                   /* 32000/4  = 8000 Hz */
#define ADC_DECIM_FACTOR_MSBC     2                   /* 32000/2  = 16000 Hz */
#define ADC_FRAME_SAMPLES         256                 /* 1 DMA フレーム = 256 sample @32kHz = 8ms */
#define ADC_FRAME_BYTES           (ADC_FRAME_SAMPLES * 2)   /* SOC_ADC_DIGI_RESULT_BYTES = 2 (esp32) */
#define ADC_STORE_BUF_BYTES       (ADC_FRAME_BYTES * 4)     /* max_store_buf_size */
#define ADC_OUT_BATCH_SAMPLES     64                  /* 間引き後、bt_data_path へまとめて渡す単位 */
#endif

/* ---- オーディオ共通 ---- */
#define PCM_BYTES_PER_SAMPLE      2
#define PCM_TX_BLOCK_US           7500   /* outgoing_data_ready 周期。AGユニットの eSCO Tesco 整合と同じ根拠 */
#define TX_RB_BYTES               4096   /* 送信用リングバッファ。約256ms@8kHz / 128ms@16kHz mono S16 */
#if AUDIO_IN_MODE == AUDIO_IN_ADC
#define TX_PREFILL_BYTES          480    /* 30ms@8kHz。これに満たない間は send_cb が 0 を返す */
#endif

#if AUDIO_IN_MODE == AUDIO_IN_ADC
#define AUDIO_DCBLOCK_CUTOFF_HZ   30.0f  /* AGユニット audio_dsp.c の HPF と同一トポロジー・同一定数 */
#endif

/* ---- LEDs ---- */
#define LED_TASK_PERIOD_MS        50
#define SCO_ERR_UNDERRUN_THRESH   5

/* ---- NVS ---- */
#define NVS_NS               "hfunit"
#define NVS_KEY_PEER_BDA     "peer_bda"   /* blob, 6 bytes(ピア=X4 の BDA) */
