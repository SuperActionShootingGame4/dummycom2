/*
 * PWR / LINK / SCO status LEDs + BOND-CLR button. See software_design.md 5.4.
 */
#include "leds.h"
#include "app_config.h"
#include "audio_i2s.h"
#include "bt_role.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "leds";

static volatile link_led_t s_link = LINK_OFF;
static volatile sco_led_t s_sco = SCO_OFF;

void leds_set_link(link_led_t s) { s_link = s; }
void leds_set_sco(sco_led_t s) { s_sco = s; }

static void leds_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(LED_TASK_PERIOD_MS);
    uint32_t tick = 0;                 /* increments every LED_TASK_PERIOD_MS */
    uint32_t btn_held_ms = 0;
    uint32_t last_underrun = 0;
    uint32_t underrun_1s = 0;
    uint32_t sec_tick = 0;
    bool reset_sent = false;

    for (;;) {
        tick++;

        if (reset_sent) {
            /* factory reset requested - stop driving LEDs, wait for restart */
            vTaskDelay(period);
            continue;
        }

        /* --- LINK LED --- */
        int link_level = 0;
        switch (s_link) {
        case LINK_ON:    link_level = 1; break;
        case LINK_BLINK: link_level = (tick / (500 / LED_TASK_PERIOD_MS)) & 1; break;
        case LINK_OFF:   default: link_level = 0; break;
        }
        gpio_set_level(PIN_LED_LINK, link_level);

        /* --- SCO LED --- */
        /* Once per second sample the underrun counter for the error-blink decision. */
        if (++sec_tick >= (1000 / LED_TASK_PERIOD_MS)) {
            sec_tick = 0;
            uint32_t now = audio_i2s_underrun_count();
            underrun_1s = now - last_underrun;
            last_underrun = now;
        }
        int sco_level = 0;
        if (s_sco == SCO_ON) {
            sco_level = (underrun_1s > SCO_ERR_UNDERRUN_THRESH)
                        ? (tick & 1)          /* trouble: ~10 Hz blink */
                        : 1;                  /* healthy: solid */
        } else if (s_sco == SCO_ERR) {
            sco_level = (tick & 1);
        }
        gpio_set_level(PIN_LED_SCO, sco_level);

        /* --- BOND-CLR button (active low, internal pull-up) --- */
        if (gpio_get_level(PIN_BTN_BOND) == 0) {
            btn_held_ms += LED_TASK_PERIOD_MS;
            if (btn_held_ms >= BTN_HOLD_MS) {
                ESP_LOGW(TAG, "BOND-CLR held: requesting factory reset");
                if (bt_role_request_factory_reset()) {
                    reset_sent = true;
                    gpio_set_level(PIN_LED_PWR, 0);
                    gpio_set_level(PIN_LED_LINK, 0);
                    gpio_set_level(PIN_LED_SCO, 0);
                }
                /* else: BtAppT work queue not up yet - retry next tick */
            }
        } else {
            btn_held_ms = 0;
        }

        vTaskDelay(period);
    }
}

void leds_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_LED_PWR) | (1ULL << PIN_LED_LINK) | (1ULL << PIN_LED_SCO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);

    gpio_config_t btn = {
        .pin_bit_mask = (1ULL << PIN_BTN_BOND),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);

    gpio_set_level(PIN_LED_PWR, 1);
    gpio_set_level(PIN_LED_LINK, 0);
    gpio_set_level(PIN_LED_SCO, 0);

    xTaskCreate(leds_task, "leds", 2560, NULL, 5, NULL);
}
