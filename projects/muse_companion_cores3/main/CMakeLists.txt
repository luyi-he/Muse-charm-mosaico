set(common "${COMPANION_SOURCE}/main")
set(sources companion_ui.c companion_model.c companion_runtime.c companion_tts.c
    companion_qwen.c companion_pcm.c companion_setup.c companion_provision.c companion_generation.c)
list(TRANSFORM sources PREPEND "${common}/")
idf_component_register(SRCS "main.c" "board_display.c" ${sources}
    INCLUDE_DIRS "." "${common}"
    REQUIRES esp_iris m5stack_core_s3 esp_mosaico_gsp_bundle freertos nvs_flash muse_link_sdk companion_credentials
    PRIV_REQUIRES esp_timer esp-gsp esp_display_present esp_lcd esp_http_client esp-tls
        esp_http_server esp_wifi esp_netif esp_event esp_websocket_client mbedtls
    EMBED_TXTFILES "${common}/setup.html"
    EMBED_FILES "${COMPANION_SOURCE}/ui/fonts/NotoSansSC-Regular.ttf")
target_compile_definitions(${COMPONENT_LIB} PRIVATE COMPANION_CORES3=1)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_bundle.cmake")
mosaico_gsp_add_ui_bundle(${COMPONENT_LIB} "../ui/main.json;../ui/settings.json")
include("${PROJECT_DIR}/managed_components/espressif__esp-gsp/cmake/gsp_bundle.cmake")
gsp_enable_freetype()
esptool_py_flash_target_image(flash ui_apps "0xd00000" "${CMAKE_BINARY_DIR}/ui_apps.bin")
