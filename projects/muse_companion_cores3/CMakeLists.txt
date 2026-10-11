cmake_minimum_required(VERSION 3.16)
set(COMPANION_BOARD cores3 CACHE STRING "Companion board")
set(ESP_IRIS_BUILD_PROFILE tcp CACHE STRING "Iris transport profile")
set(CONFIGDEP_ENABLE OFF CACHE BOOL "Use normal compiler dependencies" FORCE)
set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-utils")
set(COMPANION_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../muse_companion")
if(CMAKE_HOST_WIN32 AND NOT DEFINED ENV{GSPC_EXECUTABLE})
    find_program(companion_host_python NAMES python REQUIRED)
    execute_process(COMMAND "${companion_host_python}" "${COMPANION_SOURCE}/tools/windows_gspc_path.py"
        OUTPUT_VARIABLE companion_gspc_wrapper OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set(GSPC_EXECUTABLE "${companion_gspc_wrapper}" CACHE FILEPATH "Windows GSPC adapter" FORCE)
endif()
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_compiler.cmake")
list(APPEND EXTRA_COMPONENT_DIRS
    "${COMPANION_SOURCE}/components"
    "${MOSAICO_UTILS_ROOT}/ESP-Iris/components/esp_iris"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_gsp_bundle")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(muse_companion_cores3 VERSION 1.0.0)

# Codec 1.3.x relies on the legacy driver umbrella exporting these headers.
# IDF 6 requires the concrete driver components; keep the managed source intact.
idf_component_get_property(cores3_codec_lib espressif__esp_codec_dev COMPONENT_LIB)
target_link_libraries(${cores3_codec_lib} PRIVATE
    idf::esp_driver_i2c idf::esp_driver_gpio idf::esp_driver_i2s idf::esp_driver_spi)
# The SPI adapter also relied on a transitive version-header include.
target_compile_options(${cores3_codec_lib} PRIVATE -include esp_idf_version.h)

# BSP 3.0 uses LCD names removed in IDF 6. Limit the aliases to its two sources.
idf_component_get_property(cores3_bsp_lib espressif__m5stack_core_s3 COMPONENT_LIB)
target_compile_definitions(${cores3_bsp_lib} PRIVATE
    color_space=rgb_ele_order ESP_LCD_COLOR_SPACE_BGR=LCD_RGB_ELEMENT_ORDER_BGR)
idf_component_get_property(cores3_lcd_lib espressif__esp_lcd_ili9341 COMPONENT_LIB)
target_compile_definitions(${cores3_lcd_lib} PRIVATE
    rgb_endian=rgb_ele_order
    LCD_RGB_ENDIAN_RGB=LCD_RGB_ELEMENT_ORDER_RGB
    LCD_RGB_ENDIAN_BGR=LCD_RGB_ELEMENT_ORDER_BGR)
