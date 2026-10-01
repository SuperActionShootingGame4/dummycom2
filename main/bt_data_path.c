/*
 * HFP-AG Voice-over-HCI data path. See software_design.md 5.6 / 6.5 / 6.6.
 *
 * recv_cb (Bluedroid BTC context): copy PCM into the jitter buffer, accumulate
 * RMS. NOTHING else - no logging, no malloc, no blocking calls (ISS-013).
 * send_cb: always hand back `len` bytes of silence (uplink is silent, RQ-05).
 * The buffer `len` is honoured exactly to avoid the #6218 size mismatch.
 */
#include <string.h>
#include <stdint.h>

#include "bt_data_path.h"
#include "app_config.h"
#include "audio_i2s.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_hf_ag_api.h"
#include "esp_hf_client_api.h"

static const char *TAG = "bt_data_path";

static esp_timer_handle_t s_tx_ready_timer;   /* 7.5 ms, SCO only */
static esp_timer_handle_t s_rms_timer;        /* 1 s */

/* RMS accumulators - written by recv_cb, snapshot+reset by the RMS timer. */
static int64_t s_rms_acc;
static uint32_t s_rms_cnt;

static uint32_t isqrt64(uint64_t n)
{
    if (n == 0) {
        return 0;
    }
    uint64_t x = n;
    uint64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return (uint32_t)x;
}

/* ---- data callbacks (BTC context) ------------------------------------- */

static void recv_cb(const uint8_t *buf, uint32_t len)
{
    const int16_t *s = (const int16_t *)buf;
    uint32_t n = len / 2;
    int64_t acc = 0;
    for (uint32_t i = 0; i < n; i++) {
        acc += (int32_t)s[i] * (int32_t)s[i];
    }
    s_rms_acc += acc;
    s_rms_cnt += n;

    audio_i2s_write(buf, len);
}

static uint32_t send_cb(uint8_t *buf, uint32_t len)
{
    memset(buf, 0, len);
    return len;                 /* honour `len` exactly (#6218 workaround) */
}

/* ---- timers ---------------------------------------------------------- */

/* esp_timer context. esp_hf_[ag|client]_outgoing_data_ready() is a data-path trigger
 * (not a state-mutating control API) and per design 5.6 is called directly at
 * the 7.5 ms rate - dispatching it via BtAppT would flood the work queue. */
static void tx_ready_timer_cb(void *arg)
{
    (void)arg;
#if BT_ROLE == BT_ROLE_HF
    esp_hf_client_outgoing_data_ready();
#else
    esp_hf_ag_outgoing_data_ready();
#endif
}

static void rms_timer_cb(void *arg)
{
    (void)arg;
    int64_t acc = s_rms_acc;
    uint32_t cnt = s_rms_cnt;
    s_rms_acc = 0;
    s_rms_cnt = 0;
    if (cnt == 0) {
        return;
    }
    uint32_t rms = isqrt64((uint64_t)(acc / cnt));
    ESP_LOGI(TAG, "rx PCM: %u samples, rms=%u, underrun=%u overrun=%u",
             (unsigned)cnt, (unsigned)rms,
             (unsigned)audio_i2s_underrun_count(), (unsigned)audio_i2s_overrun_count());
}

/* ---- public API ----------------------------------------------------- */

void bt_data_path_init(void)
{
    /* U-02: register once, here, after bluedroid enable + esp_hf_ag_init().
     * If real hardware hangs or stays silent, move this call (and an
     * unregister on disconnect) into bt_data_path_on_audio_connected/
     * _disconnected() - the split is kept deliberately to make that easy. */
#if BT_ROLE == BT_ROLE_HF
    esp_err_t err = esp_hf_client_register_data_callback(recv_cb, send_cb);
#else
    esp_err_t err = esp_hf_ag_register_data_callback(recv_cb, send_cb);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register_data_callback failed: %s", esp_err_to_name(err));
    }

    const esp_timer_create_args_t tx_args = {
        .callback = tx_ready_timer_cb,
        .name = "hf_tx_ready",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tx_args, &s_tx_ready_timer));

    const esp_timer_create_args_t rms_args = {
        .callback = rms_timer_cb,
        .name = "hf_rms",
    };
    ESP_ERROR_CHECK(esp_timer_create(&rms_args, &s_rms_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_rms_timer, RMS_LOG_PERIOD_US));
}

void bt_data_path_on_audio_connected(void)
{
#if BT_ROLE == BT_ROLE_HF
    /* HF role is receive-only: the uplink-silence timer made the controller
     * panic (vhci ISR watchdog) right after SCO came up on real hardware. */
    return;
#endif
    if (!esp_timer_is_active(s_tx_ready_timer)) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(s_tx_ready_timer, PCM_TX_BLOCK_US));
    }
}

void bt_data_path_on_audio_disconnected(void)
{
    if (esp_timer_is_active(s_tx_ready_timer)) {
        esp_timer_stop(s_tx_ready_timer);
    }
}
