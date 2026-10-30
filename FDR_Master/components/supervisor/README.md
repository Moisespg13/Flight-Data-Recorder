# supervisor — Perro Guardián por Software (Watchdog de Tareas)

Componente encargado de la supervisión lógica continua del estado de ejecución de las tareas críticas de FreeRTOS que conforman el firmware de la placa maestra.

---

## Principio de Funcionamiento

El supervisor por software implementa una tabla centralizada de monitoreo donde cada tarea concurrente se registra durante la etapa de inicio mediante `supervisor_register()`, especificando su nombre y el tiempo máximo admisible entre reportes de actividad (timeout).

Durante la operación normal:
1. **Llamadas de Check-in:** Cada tarea registrada invoca periódicamente a `supervisor_checkin()` al completar un ciclo de su bucle de trabajo.
2. **Tarea de Supervisión (`tsk_supervisor_master`):** Corre con prioridad 3 en el planificador de FreeRTOS y recorre periódicamente la tabla de tareas registradas:
   * Comprueba que la diferencia de tiempo entre el instante actual y el último check-in reportado no supere el umbral configurado.
   * Si una tarea no responde dentro de su ventana temporal admisible (indicando bloqueo en un semáforo, bucle infinito o inanición de CPU), emite un mensaje de advertencia o error crítico en los registros de diagnóstico.

---

## Interacción con el Watchdog por Hardware

* **Complementariedad:** Este supervisor por software complementa, pero no sustituye, a los mecanismos por hardware nativos del SoC ESP32-S3 (Interrupt Watchdog Timer e IDF Task Watchdog Timer - TWDT).
* **Exclusiones:** La tarea de atención de comandos (`tsk_cmd_port`) se excluye deliberadamente de la supervisión periódica estricta, debido a que operaciones de mantenimiento bloqueantes (como el formateo físico de las tarjetas MicroSD mediante el comando `0x04`) requieren varios segundos de ejecución ininterrumpida.
