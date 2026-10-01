/*
 * I2S0 slave RX (16 kHz mono) -> SCO-rate PCM -> bt_data_path_write_pcm().
 * See software_design.md 15.6.2 / 15.6.3.
 *
 * The master (ESP32 #1) and the SCO consumer (BT controller clock) are
 * independent, so the tx ring-buffer fill level drifts. Each output block may
 * gain or lose ONE end sample when the fill leaves target +/- hysteresis
 * (at most once per DRIFT_MIN_INTERVAL_MS).
 *
 * Only static buffers are used; no malloc in the task.
 */
#include "app_config.h"

#if AUDIO_IN_MODE == AUDIO_IN_I2S   /* compiled out in ADC mode */

#include <string.h>

#include "i2s_capture.h"
#include "bt_data_path.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "i2s_capture";

#define HB_HIST 6   /* half-band FIR needs x[n-6..n-1] */

static i2s_chan_handle_t s_rx;
static volatile uint32_t s_rate = 8000;
static volatile bool s_active;
static volatile uint32_t s_clock_lost, s_drift_ins, s_drift_del;

static int16_t s_raw[I2S_IN_CHUNK_SAMPLES];
static int16_t s_ext[HB_HIST + I2S_IN_CHUNK_SAMPLES];
static int16_t s_out[I2S_IN_CHUNK_SAMPLES + 1];   /* +1: drift insertion */

void i2s_capture_set_rate(uint32_t sco_hz) { s_rate = sco_hz; }
void i2s_capture_set_active(bool on) { s_active = on; }
uint32_t i2s_capture_clock_lost_count(void) { return s_clock_lost; }
uint32_t i2s_capture_drift_ins_count(void) { return s_drift_ins; }
uint32_t i2s_capture_drift_del_count(void) { return s_drift_del; }

static inline int16_t clip16(int32_t v)
{
    return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

static void i2s_capture_task(void *arg)
{
    (void)arg;
    int16_t hist[HB_HIST] = {0};
    uint32_t hb_phase = 0;            /* 0: emit on next sample */
    uint32_t last_rate = 0;
    bool last_active = false;
    bool lost = false;
    uint32_t timeout_ms = 0;          /* consecutive read-timeout time */
    int64_t last_lost_log_us = 0;
    int64_t last_drift_us = 0;

    for (;;) {
        size_t got = 0;
        esp_err_t r = i2s_channel_read(s_rx, s_raw, sizeof(s_raw), &got,
                                       pdMS_TO_TICKS(I2S_IN_READ_TIMEOUT_MS));
        if (r != ESP_OK || got == 0) {
            timeout_ms += I2S_IN_READ_TIMEOUT_MS;
            if (timeout_ms >= I2S_IN_LOST_MS) {
                int64_t now = esp_timer_get_time();
                if (!lost) {
                    lost = true;
                    s_clock_lost++;
                }
                if (now - last_lost_log_us >= 1000000) {
                    ESP_LOGW(TAG, "I2S clock lost (%s)", esp_err_to_name(r));
                    last_lost_log_us = now;
                }
            }
            continue;
        }
        timeout_ms = 0;
        if (lost) {
            lost = false;
            memset(hist, 0, sizeof(hist));
            hb_phase = 0;
            ESP_LOGI(TAG, "I2S clock recovered");
        }

        bool active = s_active;
        if (!active) {
            last_active = false;          /* drop data so DMA never stalls */
            continue;
        }
        uint32_t rate = s_rate;
        if (!last_active || rate != last_rate) {
            memset(hist, 0, sizeof(hist));
            hb_phase = 0;
            last_drift_us = esp_timer_get_time();
            last_rate = rate;
            last_active = true;
        }

        uint32_t n = got / sizeof(int16_t);
        uint32_t m = 0;
        if (rate == 16000) {
            memcpy(s_out, s_raw, n * sizeof(int16_t));
            m = n;
        } else {
            /* 8 kHz: 7-tap half-band (-1 0 9 16 9 0 -1)/32, keep every 2nd sample */
            memcpy(s_ext, hist, sizeof(hist));
            memcpy(&s_ext[HB_HIST], s_raw, n * sizeof(int16_t));
            for (uint32_t i = 0; i < n; i++) {
                if (hb_phase == 0) {
                    const int16_t *x = &s_ext[i + HB_HIST];   /* x[0] = newest */
                    int32_t y = -x[-6] + 9 * x[-4] + 16 * x[-3] + 9 * x[-2] - x[0];
                    s_out[m++] = clip16(y >> 5);
                }
                hb_phase ^= 1u;
            }
            memcpy(hist, &s_ext[n], sizeof(hist));
        }
        if (m == 0) {
            continue;
        }

        /* Drift correction: at most one sample per block and per interval. */
        int64_t now = esp_timer_get_time();
        if (now - last_drift_us >= (int64_t)DRIFT_MIN_INTERVAL_MS * 1000) {
            int32_t bps = (int32_t)rate * PCM_BYTES_PER_SAMPLE / 1000;   /* bytes per ms */
            int32_t target = bps * TX_TARGET_MS;
            int32_t hyst = bps * TX_HYST_MS;
            int32_t fill = (int32_t)bt_data_path_fill_bytes();
            if (fill > target + hyst) {
                m--;                       /* drop the last sample */
                s_drift_del++;
                last_drift_us = now;
            } else if (fill < target - hyst) {
                s_out[m] = s_out[m - 1];   /* duplicate the last sample */
                m++;
                s_drift_ins++;
                last_drift_us = now;
            }
        }
        if (m) {
            bt_data_path_write_pcm(s_out, m);
        }
    }
}

void i2s_capture_init(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    cc.dma_desc_num = I2S_IN_DMA_DESC_NUM;
    cc.dma_frame_num = I2S_IN_DMA_FRAME_NUM;
    esp_err_t err = i2s_new_channel(&cc, NULL, &s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s - no audio input", esp_err_to_name(err));
        return;
    }

    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_IN_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = PIN_I2S_DIN,
            .invert_flags = { 0, 0, 0 },
        },
    };
    err = i2s_channel_init_std_mode(s_rx, &sc);
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_rx);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S RX slave init failed: %s - no audio input", esp_err_to_name(err));
        return;
    }

    xTaskCreate(i2s_capture_task, "i2s_capture", 3072, NULL, configMAX_PRIORITIES - 5, NULL);
    ESP_LOGI(TAG, "I2S0 RX slave up: %d Hz/16-bit mono, BCLK=%d WS=%d DIN=%d",
             I2S_IN_RATE_HZ, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DIN);
}

#endif /* AUDIO_IN_MODE == AUDIO_IN_I2S */
