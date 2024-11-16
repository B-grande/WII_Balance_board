idf_component_register(
    SRCS "esp_hid_host_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event esp_hidh esp_hid nvs_flash bt
)
