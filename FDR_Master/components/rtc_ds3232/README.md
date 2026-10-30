# Componente RTC DS3232 / DS3231

Este componente implementa el soporte para el Reloj de Tiempo Real (**RTC**) externo Maxim DS3232 (o DS3231) por bus $I^2C$ en la dirección esclava `0x68`.

## ¿Qué es lo único que hace?

1. **Lectura inicial al encendido (Arranque en frío / Hangar):**  
   Al energizar el equipo, lee la fecha y hora por $I^2C$ para precargar la base temporal del sistema (`PARAM_ANO`, `PARAM_MES`, `PARAM_DIA`, `PARAM_HOR`, `PARAM_MIN`, `PARAM_SEG`). Esto garantiza que los primeros paquetes grabados en la MicroSD ya tengan fecha y hora real, incluso si el avión está dentro de un hangar techado o esperando el *cold start* del receptor GNSS.

2. **Calibración satelital automática:**  
   En cuanto el receptor GNSS adquiere satélites y obtiene fix UTC válido, la hora satelital toma el control del sistema y le escribe al RTC la hora atómica para dejarlo calibrado. A partir de ese momento, el sistema se disciplina directamente con el GNSS y el pulso PPS.

3. **Operación transparente ante ausencia física:**  
   Si la placa no tiene soldado físicamente el chip RTC (como en placas de desarrollo comerciales), el driver detecta que no responde en el bus, emite un aviso en el registro de inicio y se desactiva sin bloquear ninguna tarea ni afectar el funcionamiento normal.
