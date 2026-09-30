# master_config

Pines y LED. No tiene tareas de datos.

`pin_config.h` es la tabla única de GPIO del maestro: enlace SPI2, las dos SD, GNSS (17/16/15), IMU (44/43), comandos (41/42), I2C del TCA9555 (4/5), power-loss (46) y el LED.

El LED está en GPIO 0, activo en bajo. `led_indicator` lo hace parpadear según el modo (vuelo, error, descarga) desde una tarea de prioridad baja en el núcleo 0.
