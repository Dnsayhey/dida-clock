idf_component_register(
    SRCS "button_event_detector.cpp" "button_event.cpp" "button_input.cpp"
    INCLUDE_DIRS "include"
    # button_event_detector / button_event 是纯逻辑（不依赖 ESP），参与主机测试；
    # button_input 是设备侧（gpio 驱动）。
    REQUIRES esp_driver_gpio
)
