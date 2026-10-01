/*
 * Dummy Intercom HF unit firmware - entry point. See software_design.md 13.4.1.
 *
 * Boot order: NVS -> LEDs -> storage -> ADC capture -> BT controller
 * (BR/EDR only) -> Bluedroid -> BtAppT work queue -> HFP-HF.
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
#if AUDIO_IN_MODE == AUDIO_IN_ADC
#include "adc_capture.h"
#else
#include "i2s_capture.h"
#endif
#include "bt_app_core.h"
#include "bt_hf_client.h"

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
        /* AGユニットと同じ縮退: NVS がどうしても使えない場合は Bluetooth を
         * 起動せず、低速 PWR 点滅で待機する(ESP_ERROR_CHECK での再起動ループ
         * は避ける)。 */
        ESP_LOGE(TAG, "NVS init failed: %s - degraded mode, Bluetooth disabled",
                 esp_err_to_name(err));
        pwr_blink_forever(900);
    }

    leds_init();
    storage_init();
#if AUDIO_IN_MODE == AUDIO_IN_ADC
    adc_capture_init();
#else
    i2s_capture_init();
#endif

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
    bt_hf_client_start();
}
