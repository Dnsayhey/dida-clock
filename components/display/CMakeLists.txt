idf_component_register(
    SRCS "display_device.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_spi esp_lcd lvgl esp_lvgl_port
)
