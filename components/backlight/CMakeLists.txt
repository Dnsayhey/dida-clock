idf_component_register(
    SRCS "backlight_policy.cpp" "backlight_device.cpp" "light_sensor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_gpio esp_driver_ledc esp_adc
)
