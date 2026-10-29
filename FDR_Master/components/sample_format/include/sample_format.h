
/**
 * @file sample_format.h
 * @brief Ensamblador de frames CEV para los formatos de muestreo 1, 2 y 3.
 *
 * Mantiene un buffer compartido con los últimos valores de todos los
 * parámetros del MAD. Las tareas de adquisición actualizan este buffer
 * y la tarea escritora llama a sample_format_assemble_frame() para
 * generar los frames binarios de 16 bits según la especificación CEV.
 *
 * Formatos soportados:
 *   Formato 1: 40 frames × 17 words, 75 ciclos/seg (3000 frames/seg)
 *   Formato 2: 40 frames × 11 words, 75 ciclos/seg (3000 frames/seg)
 *   Formato 3: 10 frames ×  6 words, 10 ciclos/seg  (100 frames/seg)
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

// ============================================================================
// Identificadores de parámetros CEV
// ============================================================================
typedef enum {
  // Canales analógicos (ANA 1..32)
  PARAM_ANA_1 = 0,   PARAM_ANA_2,  PARAM_ANA_3,  PARAM_ANA_4,
  PARAM_ANA_5,  PARAM_ANA_6,  PARAM_ANA_7,  PARAM_ANA_8,
  PARAM_ANA_9,  PARAM_ANA_10, PARAM_ANA_11, PARAM_ANA_12,
  PARAM_ANA_13, PARAM_ANA_14, PARAM_ANA_15, PARAM_ANA_16,
  PARAM_ANA_17, PARAM_ANA_18, PARAM_ANA_19, PARAM_ANA_20,
  PARAM_ANA_21, PARAM_ANA_22, PARAM_ANA_23, PARAM_ANA_24,
  PARAM_ANA_25, PARAM_ANA_26, PARAM_ANA_27, PARAM_ANA_28,
  PARAM_ANA_29, PARAM_ANA_30, PARAM_ANA_31, PARAM_ANA_32,

  // Señales discretas (16 bits empaquetados)
  PARAM_DIG,

  // Parámetros digitales RS-232 (IMU XSENS)
  PARAM_DSP_1,  PARAM_DSP_2,  PARAM_DSP_3,  PARAM_DSP_4,
  PARAM_DSP_5,  PARAM_DSP_6,  PARAM_DSP_7,  PARAM_DSP_8,
  PARAM_DSP_9,  PARAM_DSP_10, PARAM_DSP_11, PARAM_DSP_12,
  PARAM_DSP_13,

  // Tiempo real (GNSS)
  PARAM_DIA, PARAM_MES, PARAM_ANO, PARAM_HOR, PARAM_MIN, PARAM_SEG,

  // Posición GNSS (32 bits divididos en 2 words de 16 bits c/u)
  PARAM_LAT_H,   // Latitud: bits 31-16  (1 LSB = ~0.11 m)
  PARAM_LAT_L,   // Latitud: bits 15-0
  PARAM_LON_H,   // Longitud: bits 31-16
  PARAM_LON_L,   // Longitud: bits 15-0
  PARAM_ALT,     // Altitud en pies (1 LSB = 1 ft)

  // Palabra libre (se graba como 0x0000)
  PARAM_LIBRE,

  PARAM_COUNT
} cev_param_id_t;

// ============================================================================
// Escalas de conversión float → int16_t para la IMU XSENS MTi-28A53G35
// ============================================================================
// Cada float se multiplica por la escala y se satura a [-32768, +32767].
//
//   Parámetro        Rango sensor    Escala    Máximo int16    Resolución
//   Aceleración      ±49.05 m/s²     ×500      ±24,525         0.002 m/s²
//   Giróscopo        ±300 °/s        ×100      ±30,000         0.01 °/s
//   Magnetómetro     ±2 a.u.         ×10000    ±20,000         0.0001 a.u.
//   Euler (R/P/Y)    ±180°           ×100      ±18,000         0.01°

#define IMU_SCALE_ACC   500    // Aceleración: 1 LSB = 0.002 m/s²
#define IMU_SCALE_GYR   100    // Giróscopo:   1 LSB = 0.01  °/s
#define IMU_SCALE_MAG   10000  // Magnetómetro:1 LSB = 0.0001 a.u.
#define IMU_SCALE_EULER 100    // Euler:       1 LSB = 0.01  °

// ============================================================================
// Constantes de formato
// ============================================================================
#define CEV_FORMAT_1  1
#define CEV_FORMAT_2  2
#define CEV_FORMAT_3  3

#define FMT1_WORDS_PER_FRAME  17
#define FMT1_FRAMES_PER_CYCLE 40

#define FMT2_WORDS_PER_FRAME  11
#define FMT2_FRAMES_PER_CYCLE 40

#define FMT3_WORDS_PER_FRAME  6
#define FMT3_FRAMES_PER_CYCLE 10

// ============================================================================
// API Pública
// ============================================================================

/**
 * @brief Inicializa el módulo con el formato de muestreo seleccionado.
 * @param format_num Número de formato (1, 2 o 3).
 */
esp_err_t sample_format_init(uint8_t format_num);

/** @brief Devuelve el formato activo (1, 2 o 3). */
uint8_t sample_format_get_active(void);

/** @brief Devuelve la cantidad de words por frame del formato activo. */
int sample_format_get_words_per_frame(void);

/** @brief Devuelve la cantidad de frames por ciclo (40 o 10). */
int sample_format_get_frames_per_cycle(void);

/** @brief Devuelve el período del tick de escritura en ms (1 para F1/F2, 10 para F3). */
int sample_format_get_tick_period_ms(void);

/** @brief Devuelve cuántos frames ensamblar por tick (3 para F1/F2, 1 para F3). */
int sample_format_get_frames_per_tick(void);

/** @brief Último valor crudo (int16) de un parámetro CEV. */
int16_t sample_format_get_param(cev_param_id_t id);

// --- Funciones de actualización (llamadas por las tareas de adquisición) ---

/** @brief Actualiza canales rápidos ANA 1-16 (desde el Esclavo). */
void sample_format_update_fast(const int16_t channels[16]);

/** @brief Actualiza canales lentos ANA 17-32 (desde el Esclavo). */
void sample_format_update_slow(const int16_t channels[16]);

/** @brief Actualiza señales discretas DIG (16 bits empaquetados). */
void sample_format_update_discrete(uint16_t states);

/** @brief Actualiza parámetros IMU DSP 1-11 (convierte float→int16). */
void sample_format_update_imu(float acc_x, float acc_y, float acc_z,
                              float gyr_x, float gyr_y, float gyr_z,
                              float mag_x, float mag_y, float mag_z,
                              float roll, float pitch, float yaw);

/** @brief Actualiza DSP 12 (señal de sincronismo PPS). */
void sample_format_update_dsp12(int16_t sync_value);

/** @brief Actualiza fecha y hora desde el GNSS. */
void sample_format_update_time(uint8_t day, uint8_t month, uint16_t year,
                               uint8_t hour, uint8_t minute, uint8_t second);

/** @brief Actualiza posición GNSS: Lat/Lon (double→ 2×int16) y Altitud (m→ft→int16). */
void sample_format_update_gnss(double latitude, double longitude, float altitude_m);

/**
 * @brief Ensambla un frame CEV según el formato activo.
 *
 * @param frame_id Índice del frame dentro del ciclo (0..39 o 0..9).
 * @param output   Buffer de salida (mínimo words_per_frame × sizeof(int16_t)).
 * @return Cantidad de words escritas en output.
 */
int sample_format_assemble_frame(uint8_t frame_id, int16_t *output);
