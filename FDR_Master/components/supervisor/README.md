# supervisor

Watchdog por software de las tareas del maestro.

Cada tarea se registra con `supervisor_register` (nombre y timeout) y llama a `supervisor_checkin` en su lazo. `tsk_supervisor_master` (prioridad 3) recorre esa tabla. Si una tarea no se reporta a tiempo, lo deja en el log.

No reemplaza al interrupt watchdog ni al task watchdog del ESP-IDF. El puerto de comandos no se registra: puede bloquearse en el formateo de la SD.
