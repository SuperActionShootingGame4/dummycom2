/*
 * GAP + HFP-AG event handling, call faking, SCO keepalive / reconnect.
 * See software_design.md 5.7 / 4.2 / 6.4.
 *
 * Threading: the GAP/HF callbacks run on the Bluedroid BTC task and only do
 * light work - they copy what they need and dispatch to the BtAppT task
 * (bt_app_core), which is the single place esp_hf_ag_* / esp_bt_gap_* control
 * APIs are called from (design 8.5). esp_timer callbacks likewise only dispatch.
 */
#include <string.h>

#include "bt_hf_ag.h"
#include "bt_data_path.h"
#include "bt_app_core.h"
#include "audio_i2s.h"
#include "storage.h"
#include "leds.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_ag_api.h"

static const char *TAG = "bt_hf_ag";

/* ---- work items dispatched to BtAppT --------------------------------- */
typedef enum {
    HFW_STACK_UP = 0,
    HFW_CONN_STATE,          /* i1 = esp_hf_connection_state_t */
    HFW_AUDIO_STATE,         /* i1 = esp_hf_audio_state_t, i2 = sync_conn_handle */
    HFW_CIND,
    HFW_COPS,
    HFW_CLCC,
    HFW_CNUM,
    HFW_IND_UPDATE,
    HFW_UNAT,                /* str = AT string */
    HFW_DIAL,                /* str = number, i1 = dial type */
    HFW_DIAL_FINISH,         /* from the 200 ms one-shot */
    HFW_ATA,
    HFW_CHUP,
    HFW_SAVE_PEER,           /* from GAP AUTH_CMPL */
    HFW_ACT_AUDIO_CONNECT,   /* from keepalive timer */
    HFW_ACT_SLC_CONNECT,     /* from reconnect timer */
    HFW_FACTORY_RESET,       /* from BOND-CLR button */
} hf_work_kind_t;

typedef struct {
    hf_work_kind_t kind;
    esp_bd_addr_t addr;
    int i1;
    int i2;
    char str[40];
} hf_work_t;

/* ---- module state (owned by BtAppT unless noted) -------------------- */
static esp_bd_addr_t s_peer;
static bool s_slc;
static bool s_sco;
static uint16_t s_sco_handle;
static int s_call;          /* faked call state */
static int s_callsetup;     /* faked call-setup state */

static esp_bd_addr_t s_dial_addr;
static char s_dial_num[40];

static esp_timer_handle_t s_keepalive_timer;   /* 3 s, SLC only */
static esp_timer_handle_t s_reconnect_timer;   /* 5 s, ACL down + peer known */
static esp_timer_handle_t s_dial_timer;        /* one-shot, 200 ms */

/* Mutable copies of the literal strings handed to esp_hf_ag_* (char*, not const). */
static char s_cops_name[] = BT_COPS_NAME;
static char s_empty_num[] = "";
static char s_unat_ok[] = "";                  /* "" -> HF sees OK (btc_hf_unat_response) */

static void hf_work_handler(uint16_t event, void *param);

static void hf_post(const hf_work_t *w)
{
    bt_app_work_dispatch(hf_work_handler, 0, (void *)w, sizeof(*w), NULL);
}

/* ---- scan mode ----------------------------------------------------- */
void bt_hf_ag_apply_scan_mode(void)
{
    /* CR-010: stay discoverable until an SLC is up, even with a stored peer,
     * so a new device (e.g. SENA SRL3) can still find us. */
    esp_err_t e = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                           s_slc ? ESP_BT_NON_DISCOVERABLE
                                                 : ESP_BT_GENERAL_DISCOVERABLE);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "set_scan_mode failed: %s", esp_err_to_name(e));
    }
}

/* ---- timers (esp_timer task context: dispatch only) --------------- */
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

static void dial_timer_cb(void *arg)
{
    (void)arg;
    hf_work_t w = { .kind = HFW_DIAL_FINISH };
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

/* ---- BtAppT: the only context that calls esp_hf_ag_* / esp_bt_gap_* -- */
static void handle_stack_up(void)
{
    esp_bt_gap_set_device_name(BT_DEVICE_NAME);

    esp_bt_cod_t cod = { 0 };
#if BT_COD_PROFILE == 1
    cod.major = ESP_BT_COD_MAJOR_DEV_AV;
    cod.minor = BT_COD_MINOR_AV_HANDSFREE;
#else
    cod.major = ESP_BT_COD_MAJOR_DEV_PHONE;
    cod.minor = BT_COD_MINOR_SMARTPHONE;
#endif
    cod.service = ESP_BT_COD_SRVC_TELEPHONY | ESP_BT_COD_SRVC_AUDIO;
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);

#if !BT_LEGACY_PAIRING
    esp_bt_io_cap_t io = ESP_BT_IO_CAP_NONE;          /* SSP Just Works */
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io, sizeof(io));
#endif

    /* Technical validation only: fixed legacy PIN, to be hardened later. */
    _Static_assert(sizeof(BT_PIN_CODE) - 1 == 4, "BT_PIN_CODE must be 4 digits");
    esp_bt_pin_code_t pin;
    memcpy(pin, BT_PIN_CODE, 4);
    esp_err_t pin_err = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin);
    if (pin_err != ESP_OK) {
        ESP_LOGW(TAG, "set_pin failed: %s", esp_err_to_name(pin_err));
    }

    bt_hf_ag_apply_scan_mode();

    if (storage_has_peer()) {
        storage_get_peer(s_peer);
        leds_set_link(LINK_BLINK);
        timer_start_periodic(s_reconnect_timer, (uint64_t)ACL_RECONNECT_MS * 1000);
    }
    ESP_LOGI(TAG, "HFP-AG ready, name=\"%s\"%s", BT_DEVICE_NAME,
             storage_has_peer() ? " (reconnecting to stored peer, discoverable)" : " (discoverable)");
}

static void handle_conn_state(const hf_work_t *w)
{
    switch (w->i1) {
    case ESP_HF_CONNECTION_STATE_CONNECTING:
        leds_set_link(LINK_BLINK);
        break;

    case ESP_HF_CONNECTION_STATE_SLC_CONNECTED:
        s_slc = true;
        memcpy(s_peer, w->addr, sizeof(s_peer));
        storage_save_peer(s_peer);
        bt_hf_ag_apply_scan_mode();               /* discoverable off */
        leds_set_link(LINK_ON);
        timer_stop(s_reconnect_timer);
        timer_start_periodic(s_keepalive_timer, (uint64_t)SCO_KEEPALIVE_MS * 1000);
        ESP_LOGI(TAG, "SLC connected");
        esp_hf_ag_audio_connect(s_peer);          /* bring SCO up immediately */
        break;

    case ESP_HF_CONNECTION_STATE_DISCONNECTED:
        s_slc = false;
        s_sco = false;
        s_sco_handle = 0;
        s_call = 0;
        s_callsetup = 0;
        timer_stop(s_keepalive_timer);
        audio_i2s_set_sco_active(false);
        bt_data_path_on_audio_disconnected();
        leds_set_sco(SCO_OFF);
        ESP_LOGI(TAG, "ACL disconnected");
        bt_hf_ag_apply_scan_mode();               /* discoverable again */
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
    case ESP_HF_AUDIO_STATE_CONNECTED:
    case ESP_HF_AUDIO_STATE_CONNECTED_MSBC: {
        bool msbc = (w->i1 == ESP_HF_AUDIO_STATE_CONNECTED_MSBC);
        ESP_LOGI(TAG, "%s SCO - %s audio path",
                 msbc ? "mSBC/WBS" : "CVSD", msbc ? "16 kHz wideband" : "8 kHz narrowband");
        s_sco = true;
        s_sco_handle = (uint16_t)w->i2;
        audio_i2s_set_input_rate(msbc ? 16000 : 8000);
        audio_i2s_reset();
        audio_i2s_set_sco_active(true);
        bt_data_path_on_audio_connected();
        leds_set_sco(SCO_ON);
        ESP_LOGI(TAG, "SCO connected (handle 0x%04x)", s_sco_handle);
        break;
    }

    case ESP_HF_AUDIO_STATE_DISCONNECTED:
        s_sco = false;
        s_sco_handle = 0;
        audio_i2s_set_sco_active(false);
        bt_data_path_on_audio_disconnected();
        leds_set_sco(SCO_OFF);
        ESP_LOGI(TAG, "SCO disconnected");
        break;

    default:
        break;
    }
}

static void handle_dial(const hf_work_t *w)
{
    ESP_LOGI(TAG, "DIAL (type %d, num \"%s\") -> faking call", w->i1, w->str);
    esp_hf_ag_cmee_send((uint8_t *)w->addr, ESP_HF_AT_RESPONSE_CODE_OK, ESP_HF_CME_AG_FAILURE);
    s_callsetup = ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING;
    esp_hf_ag_ciev_report((uint8_t *)w->addr, ESP_HF_IND_TYPE_CALLSETUP, s_callsetup);

    memcpy(s_dial_addr, w->addr, sizeof(s_dial_addr));
    strlcpy(s_dial_num, w->str, sizeof(s_dial_num));
    timer_stop(s_dial_timer);
    esp_timer_start_once(s_dial_timer, (uint64_t)DIAL_FAKE_DELAY_MS * 1000);
}

static void handle_dial_finish(void)
{
    /* U-03 / ISS-016: advance to call=1 ("(a)" path). If a real SRL-3 shows a
     * "in call" side effect that breaks intercom audio, switch to the 6.4
     * simplified variant: drop the two CIEV reports and the out_call below,
     * keep only esp_hf_ag_audio_connect(). */
    s_call = 1;
    s_callsetup = 0;
    esp_hf_ag_ciev_report(s_dial_addr, ESP_HF_IND_TYPE_CALL, s_call);
    esp_hf_ag_ciev_report(s_dial_addr, ESP_HF_IND_TYPE_CALLSETUP, s_callsetup);
    esp_hf_ag_out_call(s_dial_addr, 1, 0, ESP_HF_CALL_STATUS_CALL_IN_PROGRESS,
                       ESP_HF_CALL_SETUP_STATUS_IDLE, s_dial_num, ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
    if (s_slc && !s_sco) {
        esp_hf_ag_audio_connect(s_dial_addr);
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

    case HFW_CIND:
        esp_hf_ag_cind_response(
            (uint8_t *)w->addr,
            s_call ? ESP_HF_CALL_STATUS_CALL_IN_PROGRESS : ESP_HF_CALL_STATUS_NO_CALLS,
            (esp_hf_call_setup_status_t)s_callsetup,
            ESP_HF_NETWORK_STATE_AVAILABLE, 5, ESP_HF_ROAMING_STATUS_INACTIVE, 5,
            ESP_HF_CALL_HELD_STATUS_NONE);
        break;

    case HFW_COPS:
        esp_hf_ag_cops_response((uint8_t *)w->addr, s_cops_name);
        break;

    case HFW_CLCC:
        if (s_call) {
            esp_hf_ag_clcc_response((uint8_t *)w->addr, 1,
                                    ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING,
                                    ESP_HF_CURRENT_CALL_STATUS_ACTIVE,
                                    ESP_HF_CURRENT_CALL_MODE_VOICE,
                                    ESP_HF_CURRENT_CALL_MPTY_TYPE_SINGLE,
                                    s_empty_num, ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
        }
        esp_hf_ag_clcc_response((uint8_t *)w->addr, 0,
                                ESP_HF_CURRENT_CALL_DIRECTION_OUTGOING,
                                ESP_HF_CURRENT_CALL_STATUS_ACTIVE,
                                ESP_HF_CURRENT_CALL_MODE_VOICE,
                                ESP_HF_CURRENT_CALL_MPTY_TYPE_SINGLE,
                                s_empty_num, ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
        break;

    case HFW_CNUM:
        esp_hf_ag_cnum_response((uint8_t *)w->addr, s_empty_num, 129,
                                ESP_HF_SUBSCRIBER_SERVICE_TYPE_UNKNOWN);
        break;

    case HFW_IND_UPDATE:
        esp_hf_ag_ciev_report((uint8_t *)w->addr, ESP_HF_IND_TYPE_SERVICE, 1);
        esp_hf_ag_ciev_report((uint8_t *)w->addr, ESP_HF_IND_TYPE_SIGNAL, 5);
        esp_hf_ag_ciev_report((uint8_t *)w->addr, ESP_HF_IND_TYPE_ROAM, 0);
        esp_hf_ag_ciev_report((uint8_t *)w->addr, ESP_HF_IND_TYPE_BATTCHG, 5);
        esp_hf_ag_ciev_report((uint8_t *)w->addr, ESP_HF_IND_TYPE_CALL, s_call);
        esp_hf_ag_ciev_report((uint8_t *)w->addr, ESP_HF_IND_TYPE_CALLSETUP, s_callsetup);
        break;

    case HFW_UNAT:
        ESP_LOGI(TAG, "UNAT \"%s\" -> OK", w->str);
        esp_hf_ag_unknown_at_send((uint8_t *)w->addr, s_unat_ok);
        if (strstr(w->str, "CKPD") && s_slc && !s_sco) {
            esp_hf_ag_audio_connect(s_peer);
        }
        break;

    case HFW_DIAL:
        handle_dial(w);
        break;

    case HFW_DIAL_FINISH:
        handle_dial_finish();
        break;

    case HFW_ATA:
        ESP_LOGI(TAG, "ATA -> answer (fake)");
        s_call = 1;
        s_callsetup = 0;
        esp_hf_ag_answer_call((uint8_t *)w->addr, 1, 0,
                              ESP_HF_CALL_STATUS_CALL_IN_PROGRESS,
                              ESP_HF_CALL_SETUP_STATUS_IDLE,
                              s_empty_num, ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
        break;

    case HFW_CHUP:
        ESP_LOGI(TAG, "CHUP -> end call (fake)");
        s_call = 0;
        s_callsetup = 0;
        esp_hf_ag_end_call((uint8_t *)w->addr, 0, 0,
                           ESP_HF_CALL_STATUS_NO_CALLS,
                           ESP_HF_CALL_SETUP_STATUS_IDLE,
                           s_empty_num, ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
        break;

    case HFW_SAVE_PEER:
        memcpy(s_peer, w->addr, sizeof(s_peer));
        storage_save_peer(s_peer);
        bt_hf_ag_apply_scan_mode();
        ESP_LOGI(TAG, "bonded with peer");
        break;

    case HFW_ACT_AUDIO_CONNECT:
        if (s_slc && !s_sco) {
            esp_err_t e = esp_hf_ag_audio_connect(s_peer);
            if (e != ESP_OK) {
                ESP_LOGW(TAG, "keepalive audio_connect: %s", esp_err_to_name(e));
            }
        }
        break;

    case HFW_ACT_SLC_CONNECT:
        if (!s_slc && storage_has_peer()) {
            storage_get_peer(s_peer);
            leds_set_link(LINK_BLINK);
            esp_err_t e = esp_hf_ag_slc_connect(s_peer);
            if (e != ESP_OK) {
                ESP_LOGW(TAG, "reconnect slc_connect: %s", esp_err_to_name(e));
            }
        }
        break;

    case HFW_FACTORY_RESET: {
        /* ISS-019: run on BtAppT so GAP APIs stay off the LED task, and wait
         * for the (asynchronous) Bluedroid bond removal to finish before the
         * restart - otherwise link keys can survive in NVS. */
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

bool bt_hf_ag_request_factory_reset(void)
{
    hf_work_t w = { .kind = HFW_FACTORY_RESET };
    return bt_app_work_dispatch(hf_work_handler, 0, &w, sizeof(w), NULL);
}

/* ---- GAP callback (BTC context) ---------------------------------- */
static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            hf_work_t w = { .kind = HFW_SAVE_PEER };
            memcpy(w.addr, param->auth_cmpl.bda, sizeof(w.addr));
            hf_post(&w);
            ESP_LOGI(TAG, "paired: " ESP_BD_ADDR_STR " (%s)",
                     ESP_BD_ADDR_HEX(param->auth_cmpl.bda), param->auth_cmpl.device_name);
        } else {
            ESP_LOGW(TAG, "auth failed: %d (" ESP_BD_ADDR_STR ")", param->auth_cmpl.stat,
                     ESP_BD_ADDR_HEX(param->auth_cmpl.bda));
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

/* ---- HF callback (BTC context) --------------------------------- */
static const char *const c_conn_state[] = {
    "DISCONNECTED", "CONNECTING", "CONNECTED", "SLC_CONNECTED", "DISCONNECTING",
};
static const char *const c_audio_state[] = {
    "disconnected", "connecting", "connected", "connected_msbc",
};

static void bt_hf_cb(esp_hf_cb_event_t event, esp_hf_cb_param_t *param)
{
    hf_work_t w;
    memset(&w, 0, sizeof(w));

    switch (event) {
    case ESP_HF_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "connection state: %s",
                 c_conn_state[param->conn_stat.state]);
        w.kind = HFW_CONN_STATE;
        w.i1 = param->conn_stat.state;
        memcpy(w.addr, param->conn_stat.remote_bda, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "audio state: %s", c_audio_state[param->audio_stat.state]);
        w.kind = HFW_AUDIO_STATE;
        w.i1 = param->audio_stat.state;
        w.i2 = param->audio_stat.sync_conn_handle;
        memcpy(w.addr, param->audio_stat.remote_addr, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_CIND_RESPONSE_EVT:
        w.kind = HFW_CIND;
        memcpy(w.addr, param->cind_rep.remote_addr, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_COPS_RESPONSE_EVT:
        w.kind = HFW_COPS;
        memcpy(w.addr, param->cops_rep.remote_addr, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_CLCC_RESPONSE_EVT:
        w.kind = HFW_CLCC;
        memcpy(w.addr, param->clcc_rep.remote_addr, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_CNUM_RESPONSE_EVT:
        w.kind = HFW_CNUM;
        memcpy(w.addr, param->cnum_rep.remote_addr, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_IND_UPDATE_EVT:
        w.kind = HFW_IND_UPDATE;
        memcpy(w.addr, param->ind_upd.remote_addr, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_UNAT_RESPONSE_EVT:
        w.kind = HFW_UNAT;
        memcpy(w.addr, param->unat_rep.remote_addr, sizeof(w.addr));
        if (param->unat_rep.unat) {
            strlcpy(w.str, param->unat_rep.unat, sizeof(w.str));
        }
        hf_post(&w);
        break;

    case ESP_HF_DIAL_EVT:
        w.kind = HFW_DIAL;
        w.i1 = param->out_call.type;
        memcpy(w.addr, param->out_call.remote_addr, sizeof(w.addr));
        if (param->out_call.num_or_loc) {
            strlcpy(w.str, param->out_call.num_or_loc, sizeof(w.str));
        }
        hf_post(&w);
        break;

    case ESP_HF_ATA_RESPONSE_EVT:
        w.kind = HFW_ATA;
        memcpy(w.addr, param->ata_rep.remote_addr, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_CHUP_RESPONSE_EVT:
        w.kind = HFW_CHUP;
        memcpy(w.addr, param->chup_rep.remote_addr, sizeof(w.addr));
        hf_post(&w);
        break;

    case ESP_HF_VOLUME_CONTROL_EVT:
        ESP_LOGI(TAG, "volume control: target %d, gain %d",
                 param->volume_control.type, param->volume_control.volume);
        break;

    case ESP_HF_BCS_RESPONSE_EVT:
        /* 0 = CVSD only, 1 = CVSD, 2 = mSBC. WBS is enabled, so either is fine. */
        ESP_LOGI(TAG, "codec negotiation result: %d (%s)", param->bcs_rep.mode,
                 param->bcs_rep.mode == ESP_HF_WBS_YES ? "mSBC" : "CVSD");
        break;

    case ESP_HF_WBS_RESPONSE_EVT:
        ESP_LOGI(TAG, "WBS codec status: %d", param->wbs_rep.codec);
        break;

    case ESP_HF_BVRA_RESPONSE_EVT:
        ESP_LOGI(TAG, "voice recognition: %d", param->vra_rep.value);
        break;

    case ESP_HF_NREC_RESPONSE_EVT:
        ESP_LOGI(TAG, "NREC: %d", param->nrec.state);
        break;

    case ESP_HF_VTS_RESPONSE_EVT:
        ESP_LOGI(TAG, "DTMF: %s", param->vts_rep.code);
        break;

    case ESP_HF_PROF_STATE_EVT:
        ESP_LOGI(TAG, "HFP-AG profile state: %d", param->prof_stat.state);
        break;

    default:
        ESP_LOGI(TAG, "unhandled HF event %d", event);
        break;
    }
}

/* ---- entry point ------------------------------------------------ */
void bt_hf_ag_start(void)
{
    esp_bt_gap_register_callback(bt_gap_cb);
    esp_hf_ag_register_callback(bt_hf_cb);
    esp_hf_ag_init();

    const esp_timer_create_args_t ka = { .callback = keepalive_timer_cb, .name = "hf_keepalive" };
    ESP_ERROR_CHECK(esp_timer_create(&ka, &s_keepalive_timer));
    const esp_timer_create_args_t rc = { .callback = reconnect_timer_cb, .name = "hf_reconnect" };
    ESP_ERROR_CHECK(esp_timer_create(&rc, &s_reconnect_timer));
    const esp_timer_create_args_t dl = { .callback = dial_timer_cb, .name = "hf_dial" };
    ESP_ERROR_CHECK(esp_timer_create(&dl, &s_dial_timer));

    bt_data_path_init();

    hf_work_t w = { .kind = HFW_STACK_UP };
    hf_post(&w);
}
