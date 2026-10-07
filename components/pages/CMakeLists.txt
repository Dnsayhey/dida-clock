idf_component_register(
    SRCS "page.cpp"
         "page_layout.cpp"
         "page_manager.cpp"
         "ui_theme.cpp"
         "theme_settings_page.cpp"
         "factory_reset_page.cpp"
         "init_page.cpp"
         "network_setup_page.cpp"
         "real_time_weather_page.cpp"
         "future_weather_page.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app assets lvgl
)
