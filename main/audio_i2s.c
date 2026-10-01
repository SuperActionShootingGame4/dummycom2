/*
 * I2S0 master TX @ I2S_OUTPUT_RATE_HZ / 16-bit + jitter buffer.
 * AUDIO_OUT_I2S16K (default, CR-012): 16 kHz mono to the hf_unit ESP32.
 * AUDIO_OUT_DAC48K: 48 kHz stereo to a PCM5102A.
 * See software_design.md 5.5 / 6.1 / 6.3.
 *
 * Flow: recv cb -> audio_i2s_write() -> jitter ring buffer (SCO-rate S16 mono)
 *       -> i2s_tx task -> audio_dsp (HPF + gate + upsample to I2S rate)
 *       -> (DAC48K: mono->stereo expand) -> i2s_channel_write().
 * i2s_channel_write() blocks on DMA space, which paces the whole chain at the
 * SCO rate; no separate rate-control timer is needed.
 */
#include <string.h>

#include "audio_i2s.h"
#include "audio_dsp.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "audio_i2s";

static RingbufHandle_t s_jitter_rb;
static i2s_chan_handle_t s_tx_chan;
static TaskHandle_t s_i2s_task;

static volatile bool s_sco_active;
static volatile bool s_prefilled;
static volatile uint32_t s_overrun;
static volatile uint32_t s_underrun;

/* Rate switch is requested from the BtAppT context but applied by the i2s_tx
 * task, so audio_dsp state has a single writer. 0 = nothing pending. */
static volatile uint32_t s_pending_rate;
static volatile bool s_pending_dsp_reset;

uint32_t audio_i2s_underrun_count(void) { return s_underrun; }
uint32_t audio_i2s_overrun_count(void) { return s_overrun; }

void audio_i2s_write(const uint8_t *pcm_s16_mono, uint32_t len)
{
    if (!s_jitter_rb || len < PCM_BYTES_PER_SAMPLE) {
        return;
    }
    len &= ~(uint32_t)(PCM_BYTES_PER_SAMPLE - 1);   /* whole samples only */

    if (xRingbufferSend(s_jitter_rb, pcm_s16_mono, len, 0) == pdTRUE) {
        return;
    }
    /* Buffer full: drop the oldest chunk so latency does not grow, then retry. */
    size_t got = 0;
    void *old = xRingbufferReceiveUpTo(s_jitter_rb, &got, 0, len);
    if (old) {
        vRingbufferReturnItem(s_jitter_rb, old);
    }
    xRingbufferSend(s_jitter_rb, pcm_s16_mono, len, 0);
    s_overrun++;
}

void audio_i2s_set_sco_active(bool active)
{
    s_sco_active = active;
    if (!active) {
        s_prefilled = false;
    }
}

void audio_i2s_set_input_rate(uint32_t hz)
{
    /* Applied by i2s_tx_task (single writer of audio_dsp state). */
    s_pending_rate = hz;
    ESP_LOGI(TAG, "SCO PCM %u Hz -> upsample -> %d Hz I2S", (unsigned)hz, I2S_OUTPUT_RATE_HZ);
}

void audio_i2s_reset(void)
{
    /* Drain the ring buffer (RB ops are internally locked - safe cross-task) and
     * re-arm prefill. The DSP reset is deferred to the tx task. Does not touch
     * s_sco_active. */
    if (s_jitter_rb) {
        size_t got;
        void *p;
        while ((p = xRingbufferReceiveUpTo(s_jitter_rb, &got, 0, JITTER_RB_BYTES)) != NULL) {
            vRingbufferReturnItem(s_jitter_rb, p);
        }
    }
    s_pending_dsp_reset = true;
    s_prefilled = false;
}

/* TX-task scratch buffers. Static (single task, one instance) to keep the task
 * stack small despite the upsampled chunk being I2S_UP_MAX x larger. */
static int16_t s_mono_in[I2S_CHUNK_MONO_SAMPLES];
static int16_t s_up_mono[I2S_CHUNK_MONO_SAMPLES * I2S_UP_MAX];
#if I2S_OUT_CHANNELS == 2
static int16_t s_stereo[I2S_CHUNK_MONO_SAMPLES * I2S_UP_MAX * 2];
#endif

static void i2s_tx_task(void *arg)
{
    (void)arg;
    const uint32_t chunk_bytes = I2S_CHUNK_MONO_SAMPLES * PCM_BYTES_PER_SAMPLE;
    int64_t last_err_log_us = 0;          /* rate-limit write-error log to 1/s */

    for (;;) {
        /* Apply a pending codec/rate change or reset before touching the DSP. */
        uint32_t pr = s_pending_rate;
        if (pr) {
            s_pending_rate = 0;
            s_pending_dsp_reset = false;      /* set_input_rate already resets */
            audio_dsp_set_input_rate(pr);
            s_prefilled = false;
        } else if (s_pending_dsp_reset) {
            s_pending_dsp_reset = false;
            audio_dsp_reset();
        }

        if (!s_prefilled && s_sco_active) {
            UBaseType_t avail = 0;
            vRingbufferGetInfo(s_jitter_rb, NULL, NULL, NULL, NULL, &avail);
            if (avail >= JITTER_PREFILL_BYTES) {
                s_prefilled = true;
            }
        }
        bool play = s_sco_active && s_prefilled;

        size_t got = 0;
        if (play) {
            void *p = xRingbufferReceiveUpTo(s_jitter_rb, &got, pdMS_TO_TICKS(8), chunk_bytes);
            if (p) {
                memcpy(s_mono_in, p, got);
                vRingbufferReturnItem(s_jitter_rb, p);
            }
        }

        uint32_t nmono = got / PCM_BYTES_PER_SAMPLE;
        if (nmono < I2S_CHUNK_MONO_SAMPLES) {
            memset(&s_mono_in[nmono], 0, (I2S_CHUNK_MONO_SAMPLES - nmono) * PCM_BYTES_PER_SAMPLE);
            if (play && nmono == 0) {
                s_underrun++;
                s_prefilled = false;          /* re-prefill before resuming */
            }
            nmono = I2S_CHUNK_MONO_SAMPLES;
        }

        /* HPF + gate + polyphase upsample to I2S_OUTPUT_RATE_HZ. Silence still
         * flows through so filter state stays continuous and I2S keeps ticking. */
        uint32_t nup = audio_dsp_process(s_mono_in, nmono, s_up_mono,
                                         I2S_CHUNK_MONO_SAMPLES * I2S_UP_MAX);

#if I2S_OUT_CHANNELS == 2
        for (uint32_t i = 0; i < nup; i++) {
            s_stereo[2 * i] = s_up_mono[i];
            s_stereo[2 * i + 1] = s_up_mono[i];
        }
        const int16_t *out = s_stereo;
        size_t out_bytes = nup * 2 * PCM_BYTES_PER_SAMPLE;
#else
        const int16_t *out = s_up_mono;
        size_t out_bytes = nup * PCM_BYTES_PER_SAMPLE;
#endif

        size_t written = 0;
        esp_err_t we = i2s_channel_write(s_tx_chan, out, out_bytes, &written, portMAX_DELAY);
        if (we != ESP_OK) {
            int64_t now = esp_timer_get_time();
            if (now - last_err_log_us >= 1000000) {
                ESP_LOGW(TAG, "i2s_channel_write failed: %s", esp_err_to_name(we));
                last_err_log_us = now;
            }
        }
    }
}

void audio_i2s_init(void)
{
    s_jitter_rb = xRingbufferCreate(JITTER_RB_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (!s_jitter_rb) {
        ESP_LOGE(TAG, "jitter ring buffer alloc failed");
        abort();
    }

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = I2S_DMA_DESC_NUM;
    cc.dma_frame_num = I2S_DMA_FRAME_NUM;
#if AUDIO_OUT_MODE == AUDIO_OUT_I2S16K
    cc.auto_clear = true;                 /* underrun -> keep clocking zeros, never stale data */
#endif
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &s_tx_chan, NULL));

    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_OUTPUT_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       (I2S_OUT_CHANNELS == 1 ? I2S_SLOT_MODE_MONO
                                                                              : I2S_SLOT_MODE_STEREO)),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { 0, 0, 0 },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &sc));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));

    audio_dsp_set_input_rate(PCM_SAMPLE_RATE_HZ);   /* default until a codec is negotiated */

    xTaskCreate(i2s_tx_task, "i2s_tx", 4096, NULL, configMAX_PRIORITIES - 4, &s_i2s_task);
    ESP_LOGI(TAG, "I2S0 TX up: %d Hz/16-bit %s, BCLK=%d WS=%d DOUT=%d",
             I2S_OUTPUT_RATE_HZ, I2S_OUT_CHANNELS == 1 ? "mono" : "stereo", PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT);
}
