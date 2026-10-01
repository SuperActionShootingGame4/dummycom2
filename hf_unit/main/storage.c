/*
 * NVS-backed peer bond storage (peer = Insta360 X4). See software_design.md 13.5.4.
 */
#include <stdlib.h>
#include <string.h>

#include "storage.h"
#include "app_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_gap_bt_api.h"

static const char *TAG = "storage";

static nvs_handle_t s_h;
static bool s_open;
static bool s_has_peer;
static esp_bd_addr_t s_peer;

void storage_init(void)
{
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &s_h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        s_open = false;
        s_has_peer = false;
        return;
    }
    s_open = true;

    size_t len = sizeof(s_peer);
    err = nvs_get_blob(s_h, NVS_KEY_PEER_BDA, s_peer, &len);
    if (err == ESP_OK && len == sizeof(s_peer)) {
        s_has_peer = true;
        ESP_LOGI(TAG, "peer BDA loaded: %02x:%02x:%02x:%02x:%02x:%02x",
                 s_peer[0], s_peer[1], s_peer[2], s_peer[3], s_peer[4], s_peer[5]);
    } else {
        s_has_peer = false;
        ESP_LOGI(TAG, "no peer BDA stored");
    }
}

bool storage_has_peer(void)
{
    return s_has_peer;
}

void storage_get_peer(esp_bd_addr_t out)
{
    memcpy(out, s_peer, sizeof(s_peer));
}

void storage_save_peer(const esp_bd_addr_t bda)
{
    if (s_has_peer && memcmp(s_peer, bda, sizeof(s_peer)) == 0) {
        return;
    }
    if (!s_open) {
        ESP_LOGW(TAG, "cannot save peer: NVS not open");
        return;
    }
    esp_err_t err = nvs_set_blob(s_h, NVS_KEY_PEER_BDA, bda, sizeof(esp_bd_addr_t));
    if (err == ESP_OK) {
        err = nvs_commit(s_h);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save peer failed: %s", esp_err_to_name(err));
        return;
    }
    memcpy(s_peer, bda, sizeof(s_peer));
    s_has_peer = true;
    ESP_LOGI(TAG, "peer BDA saved: %02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

void storage_factory_reset(void)
{
    int n = esp_bt_gap_get_bond_device_num();
    if (n > 0) {
        esp_bd_addr_t *list = calloc(n, sizeof(esp_bd_addr_t));
        if (list && esp_bt_gap_get_bond_device_list(&n, list) == ESP_OK) {
            for (int i = 0; i < n; i++) {
                esp_bt_gap_remove_bond_device(list[i]);
            }
        }
        free(list);
    }
    if (s_open) {
        nvs_erase_key(s_h, NVS_KEY_PEER_BDA);
        nvs_commit(s_h);
    }
    s_has_peer = false;
    ESP_LOGW(TAG, "factory reset: bonds and peer BDA erased");
}
