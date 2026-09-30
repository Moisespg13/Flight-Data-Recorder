# imu_xsens

IMU Xsens MTi-28A53G35 por UART0 a 115200 baudios.

| Señal | GPIO |
|-------|:----:|
| RX (entra la TX de la IMU) | 44 |
| TX (sale hacia el RX de la IMU) | 43 |

Lee el protocolo binario MT y publica aceleración, giroscopio, magnetómetro, roll, pitch y yaw en el buffer de `sample_format`. En la trama CEV son DSP 1–11 y DSP 13 (guiñada). DSP 12 no sale de la IMU: es el estado del PPS.
