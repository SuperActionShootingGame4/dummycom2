/*
 * ADC1 continuous-mode (DMA) audio capture. See software_design.md 13.5.5 /
 * 13.2.4 (H-A/H-B: ESP32 adc_continuous cannot sample below 20kHz and ADC2 is
 * not supported in continuous mode, hence fixed 32kHz oversample on ADC1 /
 * GPIO34 with software decimation to the SCO rate).
 *
 * conv_done_cb runs in ISR context (esp_adc contract) and must only call
 * vTaskNotifyGiveFromISR - no logging, no blocking calls (mirrors the AG
 * unit's recv_cb constraint, ISS-013).
 */
#include "app_config.h"

#if AUDIO_IN_MODE == AUDIO_IN_ADC   /* CR-012: compiled out in I2S mode */

#include <string.h>
#include <math.h>

#include "adc_capture.h"
#include "bt_data_path.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "esp_timer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "adc_capture";

static adc_continuous_handle_t s_adc;
static TaskHandle_t s_task;
static volatile uint32_t s_decim_factor = ADC_DECIM_FACTOR_CVSD;   /* デフォルト8kHz側 */
static volatile uint32_t s_pool_ovf_count;   /* ISS-034: on_pool_ovf 診断カウンタ */

static bool IRAM_ATTR conv_done_cb(adc_continuous_handle_t h,
                                    const adc_continuous_evt_data_t *edata, void *user_data)
{
    (void)h; (void)edata; (void)user_data;
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &hp);   /* ISR: これ以外の処理をしない(§13.2.4 制約) */
    return hp == pdTRUE;
}

/* ISS-034: 内部プールが飽和した際のイベント。conv_done_cb と同じ ISR 制約
 * (カウンタのインクリメントのみ、ログ・ブロッキング呼び出し禁止)。 */
static bool IRAM_ATTR pool_ovf_cb(adc_continuous_handle_t h,
                                   const adc_continuous_evt_data_t *edata, void *user_data)
{
    (void)h; (void)edata; (void)user_data;
    s_pool_ovf_count++;
    return false;   /* no task switch requested */
}

static void adc_task(void *arg)
{
    (void)arg;
    uint8_t raw[ADC_FRAME_BYTES];
    int32_t accum = 0; uint32_t accum_n = 0;
    float dc_x1 = 0.0f, dc_y1 = 0.0f;
    uint32_t last_rate = 0;                 /* a の再計算トリガ用 */
    float a = 0.0f;
    int16_t out_buf[ADC_OUT_BATCH_SAMPLES];
    uint32_t out_n = 0;
    int64_t last_err_log_us = 0;    /* ISS-034: レート制限(最短1秒間隔) */

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            uint32_t got = 0;
            esp_err_t r = adc_continuous_read(s_adc, raw, sizeof(raw), &got, 0);
            if (r == ESP_ERR_TIMEOUT) {
                break;                       /* 正常: 取り切った合図(§13.8.3) */
            }
            if (r != ESP_OK) {
                /* ISS-034: ESP_ERR_INVALID_STATE 等の異常(内部プールオーバーフロー
                 * 等)を無音のうちに握り潰さない。1秒に1回までのレート制限で記録。 */
                int64_t now = esp_timer_get_time();
                if (now - last_err_log_us >= 1000000) {
                    ESP_LOGW(TAG, "adc_continuous_read error: %s (pool_ovf=%u)",
                             esp_err_to_name(r), (unsigned)s_pool_ovf_count);
                    last_err_log_us = now;
                }
                break;
            }

            uint32_t factor = s_decim_factor;           /* この読み取りループ内で固定 */
            uint32_t rate = ADC_SAMPLE_RATE_HZ / factor;
            if (rate != last_rate) {                    /* レート切替時だけ再計算・状態リセット */
                a = expf(-2.0f * (float)M_PI * AUDIO_DCBLOCK_CUTOFF_HZ / (float)rate);
                dc_x1 = dc_y1 = 0.0f;
                accum = 0; accum_n = 0;
                last_rate = rate;
            }

            for (uint32_t i = 0; i + 1 < got; i += 2) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&raw[i];
                if (p->type1.channel != ADC_CHANNEL_USED) {
                    continue;                            /* 無効/他チャンネルのスロットは無視 */
                }
                accum += p->type1.data;                  /* 0..4095 */
                accum_n++;
                if (accum_n < factor) {
                    continue;
                }
                int32_t avg = accum / (int32_t)accum_n;  /* 簡易ボックスカー間引き(§13.7.2 R-HF-04) */
                accum = 0; accum_n = 0;

                float x = (float)((avg - 2048) * 16);    /* 12bit中心化 → 16bit スケール */
                float y = a * (dc_y1 + x - dc_x1);        /* 1次DCブロッカ(AGユニット同一式) */
                dc_x1 = x; dc_y1 = y;
                if (y > 32767.0f) y = 32767.0f;
                if (y < -32768.0f) y = -32768.0f;
                out_buf[out_n++] = (int16_t)y;

                if (out_n == ADC_OUT_BATCH_SAMPLES) {
                    bt_data_path_write_pcm(out_buf, out_n);  /* §13.5.6 へ引き渡し */
                    out_n = 0;
                }
            }
        }
    }
}

void adc_capture_set_decimation(uint32_t factor)
{
    s_decim_factor = factor;   /* 単純代入。adc_task 側で次ループ境界で反映・状態リセット */
}

uint32_t adc_capture_overrun_count(void)
{
    return s_pool_ovf_count;
}

void adc_capture_init(void)
{
    adc_continuous_handle_cfg_t hcfg = {
        .max_store_buf_size = ADC_STORE_BUF_BYTES,
        .conv_frame_size = ADC_FRAME_BYTES,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&hcfg, &s_adc));

    static adc_digi_pattern_config_t pat;    /* adc_continuous_config は adc_pattern を保持し続けるので static */
    pat.atten = ADC_ATTEN_USED;
    pat.channel = ADC_CHANNEL_USED;
    pat.unit = ADC_UNIT_USED;
    pat.bit_width = ADC_BIT_WIDTH;

    adc_continuous_config_t cfg = {
        .pattern_num = 1,
        .adc_pattern = &pat,
        .sample_freq_hz = ADC_SAMPLE_RATE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,   /* ESP32: これ以外は無効(§13.2.4) */
    };
    ESP_ERROR_CHECK(adc_continuous_config(s_adc, &cfg));

    xTaskCreate(adc_task, "adc_capture", 3584, NULL, configMAX_PRIORITIES - 5, &s_task);

    adc_continuous_evt_cbs_t cbs = { .on_conv_done = conv_done_cb, .on_pool_ovf = pool_ovf_cb };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(s_adc, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(s_adc));

    ESP_LOGI(TAG, "ADC1 continuous started: %d Hz, GPIO%d (ADC1_CHANNEL_6)",
             ADC_SAMPLE_RATE_HZ, PIN_ADC_AUDIO_IN);
}

#endif /* AUDIO_IN_MODE == AUDIO_IN_ADC */
