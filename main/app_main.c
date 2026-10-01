/*
 * Dummy Intercom firmware - entry point. See software_design.md 4.1.
 *
 * Boot order: NVS -> LEDs -> storage -> I2S -> BT controller (BR/EDR only)
 * -> Bluedroid -> BtAppT work queue -> HFP-AG.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"

#include "app_config.h"
#include "leds.h"
#include "storage.h"
#include "audio_i2s.h"
#include "bt_app_core.h"
#include "bt_role.h"

static const char *TAG = "app_main";

/* Blink PWR forever without returning. period_ms = half-cycle. */
static void pwr_blink_forever(uint32_t period_ms)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_LED_PWR,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    for (;;) {
        gpio_set_level(PIN_LED_PWR, 1);
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        gpio_set_level(PIN_LED_PWR, 0);
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

/* BT bring-up failed: fast PWR blink, stop here. */
static void fatal_blink(void)
{
    pwr_blink_forever(100);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s)", esp_err_to_name(err));
        if (nvs_flash_erase() == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        /* ISS-022: NVS still unusable after an erase retry. Degrade instead of
         * ESP_ERROR_CHECK() abort (which would just reboot-loop): no Bluetooth,
         * slow PWR blink, wait for the user. */
        ESP_LOGE(TAG, "NVS init failed: %s - degraded mode, Bluetooth disabled",
                 esp_err_to_name(err));
        pwr_blink_forever(900);
    }

    leds_init();
    storage_init();
    audio_i2s_init();

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((err = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "controller init failed: %s", esp_err_to_name(err));
        fatal_blink();
    }
    if ((err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(TAG, "controller enable failed: %s", esp_err_to_name(err));
        fatal_blink();
    }

    esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bd_cfg.ssp_en = !BT_LEGACY_PAIRING;
    if ((err = esp_bluedroid_init_with_cfg(&bd_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(err));
        fatal_blink();
    }
    if ((err = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(err));
        fatal_blink();
    }

    const uint8_t *bda = esp_bt_dev_get_address();
    ESP_LOGI(TAG, "own address: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));

    bt_app_task_start_up();
    bt_role_start();
}
