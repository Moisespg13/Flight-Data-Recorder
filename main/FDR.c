#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "pin_config.h"
#include "protocol.h"

static const char *TAG = "FDR_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando el Firmware FDR...");

    #ifdef CONFIG_FDR_ROL_MAESTRO
        ESP_LOGI(TAG, "Rol: MAESTRO (Almacenamiento & Comunicación)");
        ESP_LOGI(TAG, "SPI Host ID: %d", CONFIG_FDR_SPI_HOST_ID);
        ESP_LOGI(TAG, "Pines: MISO=%d, MOSI=%d, CLK=%d, CS=%d", 
                 PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CLK, PIN_SPI_CS);
    #elif defined(CONFIG_FDR_ROL_ESCLAVO)
        ESP_LOGI(TAG, "Rol: ESCLAVO (Adquisición)");
        ESP_LOGI(TAG, "Pines: ADC_CONV=%d, ADC_OCUPADO=%d", PIN_ADC_CONV, PIN_ADC_OCUPADO);
    #else
        ESP_LOGE(TAG, "Rol NO definido! Revisa Kconfig.");
    #endif

    ESP_LOGI(TAG, "Protocol Magic: 0x%02X", FDR_BYTE_INICIO);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
