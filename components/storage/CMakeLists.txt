idf_component_register(
    SRCS "device_config.cpp" "nvs_config_store.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)
