idf_component_register(
    SRCS
        # 以下为纯逻辑（无 ESP 依赖），可主机测试
        "app_controller.cpp"
        "app_command_executor.cpp"
        "app_events.cpp"
        "page_type.cpp"
        "settings_view_data.cpp"
        "weather_view_data.cpp"
        "runtime_status.cpp"
    INCLUDE_DIRS "include"
    # 本组件是"UI 应用逻辑"：状态机、命令、视图数据。
    # 数据流水线（HTTP/解压/解析）已拆到 pipeline 组件，因此这里不再需要
    # net / decompress / storage / esp_timer。
    #
    # 这些是**公开头文件**里真正出现的类型，必须放 REQUIRES。
    # 依据 CONVENTIONS.md §1.4：IDF 的 REQUIRES 会传递可见，
    # 实现细节应放 PRIV_REQUIRES 让构建系统挡住上层越层访问。
    REQUIRES store weather input backlight
)
