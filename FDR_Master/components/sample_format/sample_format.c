
/**
 * @file sample_format.c
 * @brief Ensamblador de frames CEV con tablas de sub-conmutación.
 *
 * Implementa los 3 formatos de muestreo definidos por el CEV.
 * Cada formato tiene una estructura fija de frames con words de 16 bits
 * y un esquema de sub-conmutación donde ciertos slots rotan entre
 * diferentes parámetros según el ID del frame.
 *
 * Las tablas son constantes en ROM. El ensamblador solo lee la tabla
 * y copia los valores del buffer compartido al frame de salida.
 */

#include "sample_format.h"
#include "esp_log.h"

#include <math.h>
#include <string.h>

static const char *TAG = "SAMPLE_FMT";

// ============================================================================
// Buffer compartido de parámetros (últimos valores conocidos)
// ============================================================================
static int16_t s_params[PARAM_COUNT] = {0};

// Formato activo
static uint8_t s_active_format = 0;

// ============================================================================
// Tablas de sub-conmutación
// ============================================================================

// ---------- FORMATO 1: 40 frames × 17 words ----------
// Words 1-16: Siempre ANA 1..16 (no necesitan tabla)
// Word 17: Sub-conmutada según frame_id (0..39)
static const cev_param_id_t fmt1_word17[40] = {
    PARAM_DIG,     /*  ID1 */  PARAM_ANA_17,  /*  ID2 */
    PARAM_ANA_18,  /*  ID3 */  PARAM_ANA_19,  /*  ID4 */
    PARAM_ANA_20,  /*  ID5 */  PARAM_ANA_21,  /*  ID6 */
    PARAM_ANA_22,  /*  ID7 */  PARAM_ANA_23,  /*  ID8 */
    PARAM_ANA_24,  /*  ID9 */  PARAM_ANA_25,  /* ID10 */
    PARAM_DIG,     /* ID11 */  PARAM_ANA_26,  /* ID12 */
    PARAM_ANA_27,  /* ID13 */  PARAM_ANA_28,  /* ID14 */
    PARAM_ANA_29,  /* ID15 */  PARAM_ANA_30,  /* ID16 */
    PARAM_ANA_31,  /* ID17 */  PARAM_ANA_32,  /* ID18 */
    PARAM_DSP_1,   /* ID19 */  PARAM_DSP_2,   /* ID20 */
    PARAM_DIG,     /* ID21 */  PARAM_DSP_3,   /* ID22 */
    PARAM_DSP_4,   /* ID23 */  PARAM_DSP_5,   /* ID24 */
    PARAM_DSP_6,   /* ID25 */  PARAM_DSP_7,   /* ID26 */
    PARAM_DSP_8,   /* ID27 */  PARAM_DSP_9,   /* ID28 */
    PARAM_DSP_10,  /* ID29 */  PARAM_DSP_11,  /* ID30 */
    PARAM_DIG,     /* ID31 */  PARAM_DSP_12,  /* ID32 */
    PARAM_DIA,     /* ID33 */  PARAM_MES,     /* ID34 */
    PARAM_ANO,     /* ID35 */  PARAM_HOR,     /* ID36 */
    PARAM_MIN,     /* ID37 */  PARAM_SEG,     /* ID38 */
    PARAM_LIBRE,   /* ID39 */  PARAM_LIBRE,   /* ID40 */
};

// ---------- FORMATO 2: 40 frames × 11 words ----------
// Words 1-9: Siempre ANA 1..9 (no necesitan tabla)
// Word 10: Sub-conmutada
static const cev_param_id_t fmt2_word10[40] = {
    PARAM_DIG,     /*  ID1 */  PARAM_ANA_10,  /*  ID2 */
    PARAM_ANA_11,  /*  ID3 */  PARAM_ANA_12,  /*  ID4 */
    PARAM_ANA_13,  /*  ID5 */  PARAM_ANA_14,  /*  ID6 */
    PARAM_ANA_15,  /*  ID7 */  PARAM_ANA_16,  /*  ID8 */
    PARAM_ANA_17,  /*  ID9 */  PARAM_ANA_18,  /* ID10 */
    PARAM_DIG,     /* ID11 */  PARAM_ANA_19,  /* ID12 */
    PARAM_ANA_20,  /* ID13 */  PARAM_ANA_21,  /* ID14 */
    PARAM_ANA_22,  /* ID15 */  PARAM_ANA_23,  /* ID16 */
    PARAM_ANA_24,  /* ID17 */  PARAM_DIG,     /* ID18 */
    PARAM_DSP_1,   /* ID19 */  PARAM_DSP_2,   /* ID20 */
    PARAM_DIG,     /* ID21 */  PARAM_DSP_3,   /* ID22 */
    PARAM_DSP_4,   /* ID23 */  PARAM_DSP_5,   /* ID24 */
    PARAM_DSP_6,   /* ID25 */  PARAM_DSP_7,   /* ID26 */
    PARAM_DSP_8,   /* ID27 */  PARAM_DSP_9,   /* ID28 */
    PARAM_DSP_10,  /* ID29 */  PARAM_DSP_11,  /* ID30 */
    PARAM_DIG,     /* ID31 */  PARAM_DSP_12,  /* ID32 */
    PARAM_DIA,     /* ID33 */  PARAM_MES,     /* ID34 */
    PARAM_ANO,     /* ID35 */  PARAM_HOR,     /* ID36 */
    PARAM_MIN,     /* ID37 */  PARAM_SEG,     /* ID38 */
    PARAM_LIBRE,   /* ID39 */  PARAM_LIBRE,   /* ID40 */
};

// Word 11: Sub-conmutada (ANA 26-32 en los primeros 7 frames, luego Libre)
static const cev_param_id_t fmt2_word11[40] = {
    PARAM_ANA_26,  /*  ID1 */  PARAM_ANA_27,  /*  ID2 */
    PARAM_ANA_28,  /*  ID3 */  PARAM_ANA_29,  /*  ID4 */
    PARAM_ANA_30,  /*  ID5 */  PARAM_ANA_31,  /*  ID6 */
    PARAM_ANA_32,  /*  ID7 */  PARAM_LIBRE,   /*  ID8 */
    PARAM_LIBRE,   /*  ID9 */  PARAM_LIBRE,   /* ID10 */
    PARAM_LIBRE,   /* ID11 */  PARAM_LIBRE,   /* ID12 */
    PARAM_LIBRE,   /* ID13 */  PARAM_LIBRE,   /* ID14 */
    PARAM_LIBRE,   /* ID15 */  PARAM_LIBRE,   /* ID16 */
    PARAM_LIBRE,   /* ID17 */  PARAM_LIBRE,   /* ID18 */
    PARAM_LIBRE,   /* ID19 */  PARAM_LIBRE,   /* ID20 */
    PARAM_LIBRE,   /* ID21 */  PARAM_LIBRE,   /* ID22 */
    PARAM_LIBRE,   /* ID23 */  PARAM_LIBRE,   /* ID24 */
    PARAM_LIBRE,   /* ID25 */  PARAM_LIBRE,   /* ID26 */
    PARAM_LIBRE,   /* ID27 */  PARAM_LIBRE,   /* ID28 */
    PARAM_LIBRE,   /* ID29 */  PARAM_LIBRE,   /* ID30 */
    PARAM_LIBRE,   /* ID31 */  PARAM_LIBRE,   /* ID32 */
    PARAM_LIBRE,   /* ID33 */  PARAM_LIBRE,   /* ID34 */
    PARAM_LIBRE,   /* ID35 */  PARAM_LIBRE,   /* ID36 */
    PARAM_LIBRE,   /* ID37 */  PARAM_LIBRE,   /* ID38 */
    PARAM_LIBRE,   /* ID39 */  PARAM_LIBRE,   /* ID40 */
};

// ---------- FORMATO 3: 10 frames × 6 words ----------
// Todas las words son sub-conmutadas
static const cev_param_id_t fmt3_table[10][6] = {
    /* ID1  */ {PARAM_DIG, PARAM_ANA_1,  PARAM_ANA_20, PARAM_ANA_30, PARAM_DSP_8,  PARAM_DIA},
    /* ID2  */ {PARAM_DIG, PARAM_ANA_2,  PARAM_ANA_21, PARAM_ANA_31, PARAM_DSP_9,  PARAM_MES},
    /* ID3  */ {PARAM_DIG, PARAM_ANA_3,  PARAM_ANA_22, PARAM_ANA_32, PARAM_DSP_10, PARAM_ANO},
    /* ID4  */ {PARAM_DIG, PARAM_ANA_4,  PARAM_ANA_23, PARAM_DSP_1,  PARAM_DSP_11, PARAM_HOR},
    /* ID5  */ {PARAM_DIG, PARAM_ANA_5,  PARAM_ANA_24, PARAM_DSP_2,  PARAM_DSP_12, PARAM_MIN},
    /* ID6  */ {PARAM_DIG, PARAM_ANA_6,  PARAM_ANA_25, PARAM_DSP_3,  PARAM_LIBRE,  PARAM_SEG},
    /* ID7  */ {PARAM_DIG, PARAM_ANA_7,  PARAM_ANA_26, PARAM_DSP_4,  PARAM_LIBRE,  PARAM_LIBRE},
    /* ID8  */ {PARAM_DIG, PARAM_ANA_8,  PARAM_ANA_27, PARAM_DSP_5,  PARAM_LIBRE,  PARAM_LIBRE},
    /* ID9  */ {PARAM_DIG, PARAM_ANA_9,  PARAM_ANA_28, PARAM_DSP_6,  PARAM_LIBRE,  PARAM_LIBRE},
    /* ID10 */ {PARAM_DIG, PARAM_ANA_10, PARAM_ANA_29, PARAM_DSP_7,  PARAM_LIBRE,  PARAM_LIBRE},
};

// ============================================================================
// Conversión float → int16 con saturación
// ============================================================================
static int16_t float_to_int16(float value, int scale) {
  float scaled = value * (float)scale;

  // Saturar para evitar overflow
  if (scaled > 32767.0f) return 32767;
  if (scaled < -32768.0f) return -32768;

  return (int16_t)roundf(scaled);
}

// ============================================================================
// API Pública
// ============================================================================

esp_err_t sample_format_init(uint8_t format_num) {
  if (format_num < 1 || format_num > 3) {
    ESP_LOGE(TAG, "Formato inválido: %d (debe ser 1, 2 o 3)", format_num);
    return ESP_ERR_INVALID_ARG;
  }

  s_active_format = format_num;
  memset(s_params, 0, sizeof(s_params));

  ESP_LOGI(TAG, "Formato de muestreo %d activado (%d frames × %d words)",
           format_num,
           sample_format_get_frames_per_cycle(),
           sample_format_get_words_per_frame());

  return ESP_OK;
}

uint8_t sample_format_get_active(void) { return s_active_format; }

int sample_format_get_words_per_frame(void) {
  switch (s_active_format) {
  case CEV_FORMAT_1: return FMT1_WORDS_PER_FRAME;
  case CEV_FORMAT_2: return FMT2_WORDS_PER_FRAME;
  case CEV_FORMAT_3: return FMT3_WORDS_PER_FRAME;
  default:           return 0;
  }
}

int sample_format_get_frames_per_cycle(void) {
  switch (s_active_format) {
  case CEV_FORMAT_1: return FMT1_FRAMES_PER_CYCLE;
  case CEV_FORMAT_2: return FMT2_FRAMES_PER_CYCLE;
  case CEV_FORMAT_3: return FMT3_FRAMES_PER_CYCLE;
  default:           return 0;
  }
}

int sample_format_get_tick_period_ms(void) {
  // F1/F2: 75 ciclos/seg × 40 frames = 3000 frames/seg
  //        1 tick = 1 ms → 3 frames/tick → 3000/seg ✓
  // F3:    10 ciclos/seg × 10 frames = 100 frames/seg
  //        1 tick = 10 ms → 1 frame/tick → 100/seg ✓
  return (s_active_format == CEV_FORMAT_3) ? 10 : 1;
}

int sample_format_get_frames_per_tick(void) {
  return (s_active_format == CEV_FORMAT_3) ? 1 : 3;
}

// --- Actualización de parámetros ---

void sample_format_update_fast(const int16_t channels[16]) {
  // ANA 1..16 → PARAM_ANA_1..PARAM_ANA_16
  for (int i = 0; i < 16; i++) {
    s_params[PARAM_ANA_1 + i] = channels[i];
  }
}

void sample_format_update_slow(const int16_t channels[16]) {
  // ANA 17..32 → PARAM_ANA_17..PARAM_ANA_32
  for (int i = 0; i < 16; i++) {
    s_params[PARAM_ANA_17 + i] = channels[i];
  }
}

void sample_format_update_discrete(uint16_t states) {
  s_params[PARAM_DIG] = (int16_t)states;
}

void sample_format_update_imu(float acc_x, float acc_y, float acc_z,
                              float gyr_x, float gyr_y, float gyr_z,
                              float mag_x, float mag_y, float mag_z,
                              float roll, float pitch) {
  s_params[PARAM_DSP_1]  = float_to_int16(acc_x, IMU_SCALE_ACC);
  s_params[PARAM_DSP_2]  = float_to_int16(acc_y, IMU_SCALE_ACC);
  s_params[PARAM_DSP_3]  = float_to_int16(acc_z, IMU_SCALE_ACC);
  s_params[PARAM_DSP_4]  = float_to_int16(gyr_x, IMU_SCALE_GYR);
  s_params[PARAM_DSP_5]  = float_to_int16(gyr_y, IMU_SCALE_GYR);
  s_params[PARAM_DSP_6]  = float_to_int16(gyr_z, IMU_SCALE_GYR);
  s_params[PARAM_DSP_7]  = float_to_int16(mag_x, IMU_SCALE_MAG);
  s_params[PARAM_DSP_8]  = float_to_int16(mag_y, IMU_SCALE_MAG);
  s_params[PARAM_DSP_9]  = float_to_int16(mag_z, IMU_SCALE_MAG);
  s_params[PARAM_DSP_10] = float_to_int16(roll,  IMU_SCALE_EULER);
  s_params[PARAM_DSP_11] = float_to_int16(pitch, IMU_SCALE_EULER);
}

void sample_format_update_dsp12(int16_t sync_value) {
  s_params[PARAM_DSP_12] = sync_value;
}

void sample_format_update_time(uint8_t day, uint8_t month, uint16_t year,
                               uint8_t hour, uint8_t minute, uint8_t second) {
  s_params[PARAM_DIA] = (int16_t)day;
  s_params[PARAM_MES] = (int16_t)month;
  s_params[PARAM_ANO] = (int16_t)year;
  s_params[PARAM_HOR] = (int16_t)hour;
  s_params[PARAM_MIN] = (int16_t)minute;
  s_params[PARAM_SEG] = (int16_t)second;
}

// --- Ensamblado de frames ---

int sample_format_assemble_frame(uint8_t frame_id, int16_t *output) {
  if (output == NULL) return 0;

  switch (s_active_format) {

  case CEV_FORMAT_1: {
    if (frame_id >= FMT1_FRAMES_PER_CYCLE) return 0;

    // Words 1-16: ANA 1..16 (siempre)
    for (int i = 0; i < 16; i++) {
      output[i] = s_params[PARAM_ANA_1 + i];
    }
    // Word 17: sub-conmutada
    output[16] = s_params[fmt1_word17[frame_id]];

    return FMT1_WORDS_PER_FRAME;
  }

  case CEV_FORMAT_2: {
    if (frame_id >= FMT2_FRAMES_PER_CYCLE) return 0;

    // Words 1-9: ANA 1..9 (siempre)
    for (int i = 0; i < 9; i++) {
      output[i] = s_params[PARAM_ANA_1 + i];
    }
    // Word 10: sub-conmutada
    output[9] = s_params[fmt2_word10[frame_id]];
    // Word 11: sub-conmutada
    output[10] = s_params[fmt2_word11[frame_id]];

    return FMT2_WORDS_PER_FRAME;
  }

  case CEV_FORMAT_3: {
    if (frame_id >= FMT3_FRAMES_PER_CYCLE) return 0;

    // Todas las 6 words son sub-conmutadas
    for (int i = 0; i < FMT3_WORDS_PER_FRAME; i++) {
      output[i] = s_params[fmt3_table[frame_id][i]];
    }

    return FMT3_WORDS_PER_FRAME;
  }

  default:
    return 0;
  }
}
