#pragma once

#include "sdkconfig.h"

// --- Pines SPI ---
// Cambiar estos valores para que coincidan con el PCB!
#ifdef CONFIG_FDR_ROL_MAESTRO
    #define PIN_SPI_MISO    13  // Ejemplo
    #define PIN_SPI_CLK     12  // Ejemplo
    #define PIN_SPI_MOSI    11  // Ejemplo
    #define PIN_SPI_CS      10  // Chip Select del Maestro
    
    // Pines de almacenamiento (SDMMC/SPI)
    #define PIN_SD_CMD      38  // Ejemplo
    #define PIN_SD_CLK      39  // Ejemplo
    #define PIN_SD_D0       40  // Ejemplo
    
    // Pines de GNSS
    #define PIN_GNSS_RX     5   // Ejemplo
    #define PIN_GNSS_TX     6   // Ejemplo
    #define PIN_GNSS_PPS    7   // Ejemplo

#elif defined(CONFIG_FDR_ROL_ESCLAVO)
    #define PIN_SPI_MISO    13  // Ejemplo
    #define PIN_SPI_MOSI    11  // Ejemplo
    #define PIN_SPI_CLK     12  // Ejemplo
    #define PIN_SPI_CS      10  // Chip Select del Esclavo

    // Pines de adquisición
    #define PIN_ADC_CNV     15  // Ejemplo
    #define PIN_ADC_BUSY    16  // Ejemplo
#endif
