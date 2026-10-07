idf_component_register(
    SRCS "weather_sync.cpp"
    INCLUDE_DIRS "include"
    # 公开依赖：只放公开头文件里真正需要的。
    # weather_sync.h 只暴露 DeviceConfig 与 WeatherStore 两种类型。
    REQUIRES storage store
    # 实现细节：HTTP、解压、协议解析都只出现在 .cpp 里。
    # 放 PRIV_REQUIRES 可让构建系统挡住上层越层访问（CONVENTIONS.md §1.4）。
    PRIV_REQUIRES net weather decompress esp_timer
)
