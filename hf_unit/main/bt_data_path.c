/*
 * HFP-HF Voice-over-HCI data path. See software_design.md 13.5.6.
 *
 * recv_cb (Bluedroid BTC context): no-op. This unit has no playback hardware,
 * so the downlink (X4 -> HF) is simply discarded (design 13.0 H-D).
 * send_cb: non-blocking, BTC context. Feeds ADC/I2S-sourced PCM from the tx ring
 * buffer. Per the esp_hf_client_outgoing_data_cb_t contract, returns 0 (skip
 * send) rather than zero-filling when data is not available (RQ-H05).
 */
#include <string.h>
#include <stdint.h>

#include "bt_data_path.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_hf_client_api.h"

static const char *TAG = "bt_data_path";

static RingbufHandle_t s_tx_rb;
static volatile bool s_prefilled;
static uint32_t s_underrun, s_overrun;
static esp_timer_handle_t s_tx_ready_timer;   /* 7.5 ms, SCO only */

#if AUDIO_IN_MODE == AUDIO_IN_I2S
/* CR-012: prefill / silence-fill limits scale with the SCO rate (design 15.6.3). */
static volatile uint32_t s_rate_hz = 8000;
/* Consecutive zero-filled bytes. Updated from both the BTC context (send_cb)
 * and BtAppT (bt_data_path_reset). It is a 32-bit variable, so a race is
 * harmless: at worst one fill count is lost/kept across a reset. */
static uint32_t s_silence_bytes;
#define BYTES_PER_MS()   (s_rate_hz * PCM_BYTES_PER_SAMPLE / 1000)
#endif

/* ---- 受信(X4→HF): 再生ハード無し。完全に破棄。§13.0 H-D の対称形 ---- */
static void recv_cb(const uint8_t *buf, uint32_t len)
{
    (void)buf; (void)len;    /* no-op: 意図的。ダウンリンクは使わない(仕様§2「やらないこと」) */
}

/* ---- 送信(HF→X4、本機のマイク音声): 非ブロッキング、BTC文脈 ---- */
static uint32_t send_cb(uint8_t *buf, uint32_t len)
{
#if AUDIO_IN_MODE == AUDIO_IN_I2S
    const uint32_t prefill = BYTES_PER_MS() * TX_PREFILL_MS;
    if (!s_prefilled) {
        size_t avail = 0;
        vRingbufferGetInfo(s_tx_rb, NULL, NULL, NULL, NULL, &avail);
        if (avail < prefill) {
            s_underrun++;
            return 0;                          /* 接続直後の準備中: 契約どおり 0 */
        }
        s_prefilled = true;
        s_silence_bytes = 0;
    }
    /* BYTEBUF は折り返し境界で1回の取得が短くなるので最大2回で集める */
    uint32_t have = 0;
    for (int i = 0; i < 2 && have < len; i++) {
        size_t got = 0;
        void *p = xRingbufferReceiveUpTo(s_tx_rb, &got, 0, len - have);
        if (!p) {
            break;
        }
        memcpy(buf + have, p, got);
        vRingbufferReturnItem(s_tx_rb, p);
        have += got;
    }
    if (have < len) {
        /* 短い不足は無音で埋めて連続化。上限超過(#1停止/クロック喪失)なら再プリフィル */
        memset(buf + have, 0, len - have);
        s_underrun++;
        s_silence_bytes += len - have;
        if (s_silence_bytes > BYTES_PER_MS() * SILENCE_FILL_MAX_MS) {
            s_prefilled = false;
            s_silence_bytes = 0;
            return 0;
        }
    } else {
        s_silence_bytes = 0;
    }
    return len;
#else
    if (!s_prefilled) {
        size_t avail = 0;
        vRingbufferGetInfo(s_tx_rb, NULL, NULL, NULL, NULL, &avail);
        if (avail < TX_PREFILL_BYTES) {
            s_underrun++;
            return 0;                          /* 契約どおり: データ不足は0を返す(§13.2.1) */
        }
        s_prefilled = true;
    }
    size_t got = 0;
    void *p = xRingbufferReceiveUpTo(s_tx_rb, &got, 0, len);
    if (!p || got < len) {
        if (p) { vRingbufferReturnItem(s_tx_rb, p); }
        s_underrun++;
        s_prefilled = false;                   /* 再プリフィルへ(AGユニットTXタスクと同じ考え方) */
        return 0;
    }
    memcpy(buf, p, got);
    vRingbufferReturnItem(s_tx_rb, p);
    return len;
#endif
}

/* ---- producer: adc_capture / i2s_capture タスクから呼ばれる(タスク文脈、非ブロッキング) ---- */
void bt_data_path_write_pcm(const int16_t *mono, uint32_t n_samples)
{
    size_t bytes = (size_t)n_samples * PCM_BYTES_PER_SAMPLE;
    if (xRingbufferSend(s_tx_rb, mono, bytes, 0) != pdTRUE) {
        size_t got = 0;
        void *old = xRingbufferReceiveUpTo(s_tx_rb, &got, 0, bytes);   /* 最古を1個捨てる */
        if (old) { vRingbufferReturnItem(s_tx_rb, old); }
        xRingbufferSend(s_tx_rb, mono, bytes, 0);
        s_overrun++;
    }
}

uint32_t bt_data_path_fill_bytes(void)
{
    /* 使用量の近似(BYTEBUF は管理領域があるため数十 B の誤差あり)。閾値判定用。 */
    size_t free_b = xRingbufferGetCurFreeSize(s_tx_rb);
    return free_b >= TX_RB_BYTES ? 0 : (uint32_t)(TX_RB_BYTES - free_b);
}

void bt_data_path_set_rate(uint32_t hz)
{
#if AUDIO_IN_MODE == AUDIO_IN_I2S
    s_rate_hz = hz;
#else
    (void)hz;                                  /* ADC 版はプリフィル固定値 */
#endif
}

uint32_t bt_data_path_underrun_count(void) { return s_underrun; }
uint32_t bt_data_path_overrun_count(void) { return s_overrun; }

static void tx_ready_timer_cb(void *arg)
{
    (void)arg;
    esp_hf_client_outgoing_data_ready();   /* AGユニット§8.5チェック2 と同じ例外: データパスのキックのみ、直呼び可 */
}

void bt_data_path_init(void)
{
    s_tx_rb = xRingbufferCreate(TX_RB_BYTES, RINGBUF_TYPE_BYTEBUF);
    esp_err_t err = esp_hf_client_register_data_callback(recv_cb, send_cb);
    if (err != ESP_OK) { ESP_LOGE(TAG, "register_data_callback failed: %s", esp_err_to_name(err)); }

    const esp_timer_create_args_t tx_args = { .callback = tx_ready_timer_cb, .name = "hfc_tx_ready" };
    ESP_ERROR_CHECK(esp_timer_create(&tx_args, &s_tx_ready_timer));
}

void bt_data_path_reset(void)
{
    size_t got;
    void *p;
    while ((p = xRingbufferReceiveUpTo(s_tx_rb, &got, 0, TX_RB_BYTES)) != NULL) {
        vRingbufferReturnItem(s_tx_rb, p);
    }
    s_prefilled = false;
#if AUDIO_IN_MODE == AUDIO_IN_I2S
    s_silence_bytes = 0;
#endif
}

void bt_data_path_on_audio_connected(void)
{
    if (!esp_timer_is_active(s_tx_ready_timer)) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(s_tx_ready_timer, PCM_TX_BLOCK_US));
    }
}
void bt_data_path_on_audio_disconnected(void)
{
    if (esp_timer_is_active(s_tx_ready_timer)) { esp_timer_stop(s_tx_ready_timer); }
}
