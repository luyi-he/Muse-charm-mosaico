set(common "${COMPANION_SOURCE}/main")
set(sources companion_ui.c companion_model.c)
list(TRANSFORM sources PREPEND "${common}/")
idf_component_register(SRCS "main.c" "board_display.c" "local_runtime.c" ${sources}
    INCLUDE_DIRS "." "${common}"
    REQUIRES m5stack_core_s3 esp_mosaico_gsp_bundle freertos nvs_flash
    PRIV_REQUIRES esp_timer esp-gsp esp_display_present esp_lcd esp_http_client esp-tls
        esp_http_server esp_wifi esp_netif esp_event esp_websocket_client mbedtls esp_driver_usb_serial_jtag esp_driver_i2c
    EMBED_FILES "${COMPANION_SOURCE}/ui/fonts/NotoSansSC-Regular.ttf")
target_compile_definitions(${COMPONENT_LIB} PRIVATE COMPANION_CORES3=1 COMPANION_LOCAL_CODEX=1)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_bundle.cmake")
mosaico_gsp_add_ui_bundle(${COMPONENT_LIB} "../ui/main.json;../ui/settings.json")
include("${PROJECT_DIR}/managed_components/espressif__esp-gsp/cmake/gsp_bundle.cmake")
gsp_enable_freetype()
esptool_py_flash_target_image(flash ui_apps "0xd00000" "${CMAKE_BINARY_DIR}/ui_apps.bin")
