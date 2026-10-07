idf_component_register(
    SRCS
        "weather_parser.cpp"
        "weather_api.cpp"
        "weather_types.cpp"
        "weather_category.cpp"
    INCLUDE_DIRS "include"
    REQUIRES cjson
)
