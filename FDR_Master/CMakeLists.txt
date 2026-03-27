# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Agregar FDR_Common como directorio de includes extra
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../FDR_Common")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(FDR_Master)
