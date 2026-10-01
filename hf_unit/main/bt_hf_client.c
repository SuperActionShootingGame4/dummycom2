/*
 * GAP + HFP-HF event handling, SCO keepalive / ACL reconnect.
 * See software_design.md 13.5.7 / 13.4 / 13.2.1 (H-D: this role has no
 * CIND/CLCC/COPS response APIs - the SLC handshake itself is handled
 * internally by Bluedroid; the app only ever calls connect/connect_audio/
 * disconnect* and GAP setup, and every indicator-style event is log-only).
 *
 * Threading: the GAP/HF callbacks run on the Bluedroid BTC task and only do
 * light work - they copy what they need and dispatch to the BtAppT task
 * (bt_app_core), which is the single place esp_hf_client_* / esp_bt_gap_*
 * control APIs are called from (design 13.7.4). esp_timer callbacks likewise
 * only dispatch.
 */
#include <string.h>

#include "app_config.h"
#include "bt_hf_client.h"
#include "bt_data_path.h"
#include "bt_app_core.h"
#if AUDIO_IN_MODE == AUDIO_IN_ADC
#include "adc_capture.h"
#else
#include "i2s_capture.h"
#endif
#include "storage.h"
#include "leds.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"

static const char *TAG = "bt_hf_client";

/* ---- work items dispatched to BtAppT --------------------------------- */
typedef enum {
    HFW_STACK_UP = 0,
    HFW_CONN_STATE,          /* i1 = esp_hf_client_connection_state_t */
    HFW_AUDIO_STATE,         /* i1 = esp_hf_client_audio_state_t, i2 = sync_conn_handle */
    HFW_SAVE_PEER,           /* GAP AUTH_CMPL から */
    HFW_ACT_AUDIO_CONNECT,   /* keepalive タイマから */
    HFW_ACT_SLC_CONNECT,     /* reconnect タイマから */
    HFW_FACTORY_RESET,       /* BOND-CLR ボタンから */
} hf_work_kind_t;

typedef struct {
    hf_work_kind_t kind;
    esp_bd_addr_t addr;
    int i1;
    int i2;
} hf_work_t;

/* ---- module state (owned by BtAppT unless noted) -------------------- */
static esp_bd_addr_t s_peer;
static bool s_slc;
static bool s_sco;

static esp_timer_handle_t s_keepalive_timer;   /* 3s, SLC中のみ */
static esp_timer_handle_t s_reconnect_timer;   /* 5s, ACL断+peer既知 */

static void hf_work_handler(uint16_t event, void *param);

static void hf_post(const hf_work_t *w)
{
    bt_app_work_dispatch(hf_work_handler, 0, (void *)w, sizeof(*w), NULL);
}

/* ---- scan mode ------------------------------------------------------- */
void bt_hf_client_apply_scan_mode(void)
{
    if (storage_has_peer()) {
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    } else {
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    }
}

/* ---- timers (esp_timer task context: dispatch only) ------------------ */
static void keepalive_timer_cb(void *arg)
{
    (void)arg;
    hf_work_t w = { .kind = HFW_ACT_AUDIO_CONNECT };
    hf_post(&w);
}

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    hf_work_t w = { .kind = HFW_ACT_SLC_CONNECT };
    hf_post(&w);
}

static void timer_start_periodic(esp_timer_handle_t t, uint64_t us)
{
    if (!esp_timer_is_active(t)) {
        esp_timer_start_periodic(t, us);
    }
}

static void timer_stop(esp_timer_handle_t t)
{
    if (esp_timer_is_active(t)) {
        esp_timer_stop(t);
    }
}

/* ---- BtAppT: the only context that calls esp_hf_client_* / esp_bt_gap_* -- */
static void handle_stack_up(void)
{
    esp_bt_gap_set_device_name(BT_DEVICE_NAME);

    esp_bt_cod_t cod = { 0 };
    cod.major = BT_COD_MAJOR_AV;
    cod.minor = BT_COD_MINOR_MICROPHONE;
    cod.service = ESP_BT_COD_SRVC_AUDIO;
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);   /* 実効 ≈ 0x200410(§13.5.7 導出) */

    esp_bt_io_cap_t io = ESP_BT_IO_CAP_NONE;       /* Just Works 暫定(§13.9) */
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io, sizeof(io));

    esp_bt_pin_code_t pin = { '0', '0', '0', '0' };
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 4, pin);

    bt_hf_client_apply_scan_mode();
    if (storage_has_peer()) {
        storage_get_peer(s_peer);
        leds_set_link(LINK_BLINK);
        timer_start_periodic(s_reconnect_timer, (uint64_t)ACL_RECONNECT_MS * 1000);
    }
    ESP_LOGI(TAG, "HFP-HF ready, name=\"%s\"%s", BT_DEVICE_NAME,
             storage_has_peer() ? " (reconnecting to stored peer)" : " (discoverable)");
}

static void handle_conn_state(const hf_work_t *w)
{
    switch (w->i1) {
    case ESP_HF_CLIENT_CONNECTION_STATE_CONNECTING:
        leds_set_link(LINK_BLINK);
        break;

    case ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED:
        s_slc = true;
        memcpy(s_peer, w->addr, sizeof(s_peer));
        storage_save_peer(s_peer);
        bt_hf_client_apply_scan_mode();
        leds_set_link(LINK_ON);
        timer_stop(s_reconnect_timer);
        timer_start_periodic(s_keepalive_timer, (uint64_t)SCO_KEEPALIVE_MS * 1000);
        ESP_LOGI(TAG, "SLC connected");
        esp_hf_client_connect_audio(s_peer);      /* 即SCO要求。AGユニットと対称(§13.4.2) */
        break;

    case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED:
        s_slc = false;
        s_sco = false;
        timer_stop(s_keepalive_timer);
#if AUDIO_IN_MODE == AUDIO_IN_I2S
        i2s_capture_set_active(false);
#endif
        bt_data_path_on_audio_disconnected();
        leds_set_sco(SCO_OFF);
        ESP_LOGI(TAG, "ACL disconnected");
        if (storage_has_peer()) {
            leds_set_link(LINK_BLINK);
            timer_start_periodic(s_reconnect_timer, (uint64_t)ACL_RECONNECT_MS * 1000);
        } else {
            leds_set_link(LINK_OFF);
        }
        break;

    default:
        break;
    }
}

static void handle_audio_state(const hf_work_t *w)
{
    switch (w->i1) {
    case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED:
    case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC: {
        bool msbc = (w->i1 == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC);
        s_sco = true;
#if AUDIO_IN_MODE == AUDIO_IN_ADC
        adc_capture_set_decimation(msbc ? ADC_DECIM_FACTOR_MSBC : ADC_DECIM_FACTOR_CVSD);
#else
        bt_data_path_set_rate(msbc ? 16000 : 8000);
        i2s_capture_set_rate(msbc ? 16000 : 8000);
#endif
        bt_data_path_reset();
#if AUDIO_IN_MODE == AUDIO_IN_I2S
        i2s_capture_set_active(true);
#endif
        bt_data_path_on_audio_connected();
        leds_set_sco(SCO_ON);
        ESP_LOGI(TAG, "%s SCO connected", msbc ? "mSBC/16kHz" : "CVSD/8kHz");
        break;
    }

    case ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED:
        s_sco = false;
#if AUDIO_IN_MODE == AUDIO_IN_I2S
        i2s_capture_set_active(false);
#endif
        bt_data_path_on_audio_disconnected();
        leds_set_sco(SCO_OFF);
        ESP_LOGI(TAG, "SCO disconnected");
        break;

    default:
        break;
    }
}

static void hf_work_handler(uint16_t event, void *param)
{
    (void)event;
    const hf_work_t *w = (const hf_work_t *)param;

    switch (w->kind) {
    case HFW_STACK_UP:
        handle_stack_up();
        break;

    case HFW_CONN_STATE:
        handle_conn_state(w);
        break;

    case HFW_AUDIO_STATE:
        handle_audio_state(w);
        break;

    case HFW_SAVE_PEER:
        memcpy(s_peer, w->addr, sizeof(s_peer));
        storage_save_peer(s_peer);
        bt_hf_client_apply_scan_mode();
        ESP_LOGI(TAG, "bonded with peer");
        break;

    case HFW_ACT_AUDIO_CONNECT:
        if (s_slc && !s_sco) {
            esp_err_t e = esp_hf_client_connect_audio(s_peer);
            if (e != ESP_OK) {
                ESP_LOGW(TAG, "keepalive connect_audio: %s", esp_err_to_name(e));
            }
        }
        break;

    case HFW_ACT_SLC_CONNECT:
        if (!s_slc && storage_has_peer()) {
            storage_get_peer(s_peer);
            leds_set_link(LINK_BLINK);
            esp_err_t e = esp_hf_client_connect(s_peer);
            if (e != ESP_OK) {
                ESP_LOGW(TAG, "reconnect connect: %s", esp_err_to_name(e));
            }
        }
        break;

    case HFW_FACTORY_RESET: {
        /* AGユニットISS-019と同一パターン: BtAppT で実行し、非同期 bond 削除の
         * 完了(または1秒タイムアウト)を待ってから再起動する。 */
        ESP_LOGW(TAG, "factory reset: erasing stored peer and all bonds");
        storage_factory_reset();
        TickType_t start = xTaskGetTickCount();
        while (esp_bt_gap_get_bond_device_num() > 0 &&
               (xTaskGetTickCount() - start) < pdMS_TO_TICKS(1000)) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        vTaskDelay(pdMS_TO_TICKS(300));   /* let the BTC task flush bond NVS writes */
        ESP_LOGW(TAG, "restarting now");
        esp_restart();
        break;
    }

    default:
        break;
    }
}

bool bt_hf_client_request_factory_reset(void)
{
    hf_work_t w = { .kind = HFW_FACTORY_RESET };
    return bt_app_work_dispatch(hf_work_handler, 0, &w, sizeof(w), NULL);
}

/* ---- GAP callback (BTC context) --------------------------------------- */
static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            hf_work_t w = { .kind = HFW_SAVE_PEER };
            memcpy(w.addr, param->auth_cmpl.bda, sizeof(w.addr));
            hf_post(&w);
        } else {
            ESP_LOGW(TAG, "auth failed: %d", param->auth_cmpl.stat);
        }
        break;

    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);   /* Just Works */
        break;

    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = { '0', '0', '0', '0' };
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
        break;
    }

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "SSP passkey: %06u", (unsigned)param->key_notif.passkey);
        break;

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "GAP mode change: %d", param->mode_chg.mode);
        break;

    default:
        break;
    }
}

/* ---- HF client callback (BTC context): control events dispatch, ----- */
/* ---- every indicator-style event is log-only (§13.0 H-D) ------------ */
static const char *const c_conn_state[] = {
    "DISCONNECTED", "CONNECTING", "CONNECTED", "SLC_CONNECTED", "DISCONNECTING",
};
static const char *const c_audio_state[] = {
    "disconnected", "connecting", "connected", "connected_msbc",
};

static void bt_hf_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    hf_work_t w;
    memset(&w, 0, sizeof(w));

    switch (event) {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "connection state: %s", c_conn_state[param->conn_stat.state]);
        w.kind = HFW_CONN_STATE;
        w.i1 = param->conn_stat.state;
        memcpy(w.addr, param->conn_stat.remote_bda, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "audio state: %s", c_audio_state[param->audio_stat.state]);
        w.kind = HFW_AUDIO_STATE;
        w.i1 = param->audio_stat.state;
        w.i2 = param->audio_stat.sync_conn_handle;
        memcpy(w.addr, param->audio_stat.remote_bda, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_CLIENT_PROF_STATE_EVT:
        ESP_LOGI(TAG, "HFP-HF profile state: %d", param->prof_stat.state);
        break;

    /* 以下すべてログのみ、応答不要(§13.0 H-D):
     * CIND_CALL/CALL_SETUP/CALL_HELD/SERVICE_AVAILABILITY/SIGNAL_STRENGTH/ROAMING/
     * BATTERY_LEVEL、COPS_CURRENT_OPERATOR、BTRH、CLIP、CCWA、CLCC、VOLUME_CONTROL、
     * AT_RESPONSE、CNUM、BSIR、BINP、PKT_STAT_NUMS_GET、BVRA、RING_IND */
    default:
        ESP_LOGI(TAG, "HF event %d (log only)", event);
        break;
    }
}

/* ---- entry point ------------------------------------------------------ */
void bt_hf_client_start(void)
{
    esp_bt_gap_register_callback(bt_gap_cb);
    esp_hf_client_register_callback(bt_hf_cb);
    esp_hf_client_init();

    const esp_timer_create_args_t ka = { .callback = keepalive_timer_cb, .name = "hfc_keepalive" };
    ESP_ERROR_CHECK(esp_timer_create(&ka, &s_keepalive_timer));
    const esp_timer_create_args_t rc = { .callback = reconnect_timer_cb, .name = "hfc_reconnect" };
    ESP_ERROR_CHECK(esp_timer_create(&rc, &s_reconnect_timer));

    bt_data_path_init();

    hf_work_t w = { .kind = HFW_STACK_UP };
    hf_post(&w);
}
