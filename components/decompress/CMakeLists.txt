idf_component_register(
    SRCS "decompress_format.cpp" "decompress_miniz.cpp"
    INCLUDE_DIRS "include"
    # ROM 里的 miniz（tinfl_*）由 esp_rom 提供
    REQUIRES esp_rom
)
