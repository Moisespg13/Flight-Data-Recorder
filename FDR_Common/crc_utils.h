
/**
 * @file crc_utils.h
 * @brief Utilidad de cálculo CRC32 portable.
 *
 * Provee una función para calcular CRC32, utilizada para verificar
 * la integridad de los paquetes SPI. Implementación por software
 * (tabla) para ser compatible con cualquier plataforma.
 *
 * En el ESP32 se podría reemplazar internamente por esp_rom_crc
 * para mayor velocidad, pero esta versión funciona en ambos lados.
 *
 * Polinomio: 0xEDB88320 (CRC-32 estándar, IEEE 802.3)
 * Tabla: 256 entradas completas.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Calcula el CRC32 de un buffer
 *
 * Implementación por software con tabla precalculada. Compatible con
 * cualquier plataforma.
 *
 * @param data Buffer de datos
 * @param len Longitud en bytes
 * @return uint32_t Valor CRC32
 */
static inline uint32_t fdr_calc_crc32(const uint8_t *data, size_t len) {
  // Tabla CRC32 completa (256 entradas, polinomio 0xEDB88320)
  static const uint32_t crc_table[256] = {
      0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, /*   0 -   3 */
      0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3, /*   4 -   7 */
      0x0EDB8832, 0x79DCB8A4, 0xE0D5E91B, 0x97D2D988, /*   8 -  11 */
      0x09B64C2B, 0x7EB17CBB, 0xE7B82D09, 0x90BF1D9F, /*  12 -  15 */
      0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, /*  16 -  19 */
      0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, /*  20 -  23 */
      0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, /*  24 -  27 */
      0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5, /*  28 -  31 */
      0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, /*  32 -  35 */
      0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, /*  36 -  39 */
      0x35B5A8FA, 0x42B2986C, 0xDBBBB9D6, 0xACBCB9C2, /*  40 -  43 */
      0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59, /*  44 -  47 */
      0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, /*  48 -  51 */
      0x21B4F6B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F, /*  52 -  55 */
      0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, /*  56 -  59 */
      0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, /*  60 -  63 */
      0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, /*  64 -  67 */
      0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433, /*  68 -  71 */
      0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, /*  72 -  75 */
      0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01, /*  76 -  79 */
      0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, /*  80 -  83 */
      0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, /*  84 -  87 */
      0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, /*  88 -  91 */
      0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65, /*  92 -  95 */
      0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, /*  96 -  99 */
      0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, /* 100 - 103 */
      0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, /* 104 - 107 */
      0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, /* 108 - 111 */
      0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, /* 112 - 115 */
      0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F, /* 116 - 119 */
      0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, /* 120 - 123 */
      0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD, /* 124 - 127 */
      0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, /* 128 - 131 */
      0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, /* 132 - 135 */
      0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, /* 136 - 139 */
      0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1, /* 140 - 143 */
      0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, /* 144 - 147 */
      0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7, /* 148 - 151 */
      0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, /* 152 - 155 */
      0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, /* 156 - 159 */
      0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, /* 160 - 163 */
      0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B, /* 164 - 167 */
      0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, /* 168 - 171 */
      0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79, /* 172 - 175 */
      0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, /* 176 - 179 */
      0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, /* 180 - 183 */
      0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, /* 184 - 187 */
      0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D, /* 188 - 191 */
      0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, /* 192 - 195 */
      0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713, /* 196 - 199 */
      0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, /* 200 - 203 */
      0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, /* 204 - 207 */
      0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, /* 208 - 211 */
      0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777, /* 212 - 215 */
      0x88085AE6, 0xFF0F6B70, 0x66063BCA, 0x11010B5C, /* 216 - 219 */
      0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45, /* 220 - 223 */
      0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, /* 224 - 227 */
      0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, /* 228 - 231 */
      0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, /* 232 - 235 */
      0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9, /* 236 - 239 */
      0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, /* 240 - 243 */
      0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF, /* 244 - 247 */
      0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, /* 248 - 251 */
      0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D, /* 252 - 255 */
  };

  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFF;
}
