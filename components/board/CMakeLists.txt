idf_component_register(
    INCLUDE_DIRS "include"
    # board 只放固定的硬件事实（引脚、外设通道），不含逻辑。
    # 因此这里是 header-only：有源文件的组件说明它承担了超出"事实"的职责。
    REQUIRES esp_hal_ana_conv
)
