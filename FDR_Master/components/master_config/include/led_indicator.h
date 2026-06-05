#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Modos de operación del LED de estado.
 */
typedef enum {
    LED_MODE_FLIGHT_OK,         // Parpadeo 1 Hz (500ms ON / 500ms OFF)
    LED_MODE_FLIGHT_ERROR,      // Fijo ON
    LED_MODE_DOWNLOAD_PROGRESS, // Fijo ON
    LED_MODE_DOWNLOAD_DONE,     // Fijo OFF
    LED_MODE_DOWNLOAD_ERROR     // Parpadeo rápido 5 Hz (100ms ON / 100ms OFF)
} led_mode_t;

/**
 * @brief Inicializa el pin del LED y arranca la tarea de control.
 */
void led_init(void);

/**
 * @brief Cambia el modo de operación del LED.
 * @param mode El nuevo modo a establecer.
 */
void led_set_mode(led_mode_t mode);

#ifdef __cplusplus
}
#endif
