# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD adafruit_feather_rp2040 CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# ============================================================================
# FDR Slave - RP2040 (Pico SDK + FreeRTOS)
# ============================================================================

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- FreeRTOS ---
# El Pico SDK 2.x trae FreeRTOS-Kernel como submódulo en lib/.
# Si la carpeta está vacía, hay que inicializar el submódulo (ver README).
set(FREERTOS_KERNEL_PATH "${PICO_SDK_PATH}/lib/tinyusb/lib/FreeRTOS" CACHE STRING "")

# Intentar primero la ubicación estándar del SDK 2.x
if(NOT EXISTS "${FREERTOS_KERNEL_PATH}/include/FreeRTOS.h")
    # Intentar ubicación alternativa (submódulo directo)
    set(FREERTOS_KERNEL_PATH "${PICO_SDK_PATH}/lib/FreeRTOS-Kernel" CACHE STRING "" FORCE)
endif()

if(NOT EXISTS "${FREERTOS_KERNEL_PATH}/include/FreeRTOS.h")
    # Si no existe en ningún lado, buscar en carpeta local del proyecto
    set(FREERTOS_KERNEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib/FreeRTOS-Kernel" CACHE STRING "" FORCE)
endif()

include(pico_sdk_import.cmake)

# Importar FreeRTOS (necesita el archivo FreeRTOS_Kernel_import.cmake)
if(EXISTS "${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake")
    include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
elseif(EXISTS "${FREERTOS_KERNEL_PATH}/../FreeRTOS_Kernel_import.cmake")
    include(${FREERTOS_KERNEL_PATH}/../FreeRTOS_Kernel_import.cmake)
endif()

project(FDR_Slave C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# --- Fuentes del proyecto ---
add_executable(FDR_Slave
    src/main.c
    src/fast_signals.c
    src/spi_link_slave.c
)

# Incluir directorios de headers
target_include_directories(FDR_Slave PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../FDR_Common
)

# Linkear librerías del Pico SDK + FreeRTOS
target_link_libraries(FDR_Slave
    pico_stdlib
    hardware_spi
    hardware_gpio
    hardware_timer
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

# Habilitar salida USB para stdio (debug por USB serial)
pico_enable_stdio_usb(FDR_Slave 1)
pico_enable_stdio_uart(FDR_Slave 0)

# Generar archivos .uf2 (para flashear vía USB drag-and-drop)
pico_add_extra_outputs(FDR_Slave)
