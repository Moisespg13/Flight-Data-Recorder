#include "led_indicator.h"
#include "pin_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "LED";
static volatile led_mode_t s_current_mode = LED_MODE_FLIGHT_OK;

// Macro auxiliar: LED en GPIO 0 (Active-Low)
// 0V = LED ON (fluye corriente desde 3.3v)
// 3.3V = LED OFF (no hay diferencia de potencial)
#define SET_LED(is_on) gpio_set_level(PIN_LED, (is_on) ? 0 : 1)

static void tsk_led_indicator(void *pvParameters) {
    bool led_state = false;
    
    while (1) {
        led_mode_t mode = s_current_mode;
        
        switch (mode) {
            case LED_MODE_FLIGHT_OK:
                led_state = !led_state;
                SET_LED(led_state);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
                
            case LED_MODE_FLIGHT_ERROR:
            case LED_MODE_DOWNLOAD_PROGRESS:
                led_state = true;
                SET_LED(led_state);
                vTaskDelay(pdMS_TO_TICKS(100)); // Esperar poco para reaccionar rápido a cambios de modo
                break;
                
            case LED_MODE_DOWNLOAD_DONE:
                led_state = false;
                SET_LED(led_state);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case LED_MODE_DOWNLOAD_ERROR:
                led_state = !led_state;
                SET_LED(led_state);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            default:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

void led_init(void) {
    ESP_LOGI(TAG, "Inicializando LED en GPIO %d (Active-LOW)", PIN_LED);
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_LED),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf);
    
    // Iniciar apagado (1 = OFF en active-low)
    SET_LED(false);
    
    xTaskCreatePinnedToCore(tsk_led_indicator, "tsk_led", 2048, NULL, 5, NULL, 1);
}

void led_set_mode(led_mode_t mode) {
    s_current_mode = mode;
}
