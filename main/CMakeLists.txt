# ---- 可选本地配置的探测 ----
#
# 为什么需要它：dev_config.cpp / weather_config.h / time_config.h 需要探测几个
# **可选**的本地配置头（都被 .gitignore 忽略）是否存在。用 C++ 的 __has_include
# 看起来最自然，但它的依赖 **CMake 看不见** —— 首次创建 device_config_local.h 时
# 源文件不会重编，表现为"配置了却静默不生效"，会白白折腾一轮。
#
# 这里改成在配置阶段探测、把结果作为**编译宏**传下去：宏值变化 CMake 是看得见
# 的，会正常触发重编。
#
# 注意两处细节：
#   * CONFIGURE_DEPENDS 让构建系统在文件出现/消失时重跑 CMake；但它在 IDF 的
#     "脚本模式"（读取组件依赖时的 include）下是非法的，必须排除。
#   * target_compile_definitions 只能在 idf_component_register 之后、且非脚本
#     模式下调用。
if(NOT CMAKE_SCRIPT_MODE_FILE)
    file(GLOB _dida_opt_local CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_LIST_DIR}/include/device_config_local.h"
         "${CMAKE_CURRENT_LIST_DIR}/include/weather_config_local.h"
         "${CMAKE_CURRENT_LIST_DIR}/include/time_config_local.h")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/include/device_config_local.h")
    set(DIDA_HAS_DEVICE_CONFIG_LOCAL 1)
else()
    set(DIDA_HAS_DEVICE_CONFIG_LOCAL 0)
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/include/weather_config_local.h")
    set(DIDA_HAS_WEATHER_CONFIG_LOCAL 1)
else()
    set(DIDA_HAS_WEATHER_CONFIG_LOCAL 0)
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/include/time_config_local.h")
    set(DIDA_HAS_TIME_CONFIG_LOCAL 1)
else()
    set(DIDA_HAS_TIME_CONFIG_LOCAL 0)
endif()

idf_component_register(SRCS "app_main.cpp" "runtime.cpp"
                            "startup_task.cpp" "startup_flow.cpp"
                            "ui_wiring.cpp" "selftest.cpp" "dev_config.cpp"
                       INCLUDE_DIRS "." "include"
                       REQUIRES board display assets weather store pipeline
                                decompress scheduler pages storage net app portal
                                backlight input
                       PRIV_REQUIRES nvs_flash esp_app_format esp_system heap)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        DIDA_HAS_DEVICE_CONFIG_LOCAL=${DIDA_HAS_DEVICE_CONFIG_LOCAL}
        DIDA_HAS_WEATHER_CONFIG_LOCAL=${DIDA_HAS_WEATHER_CONFIG_LOCAL}
        DIDA_HAS_TIME_CONFIG_LOCAL=${DIDA_HAS_TIME_CONFIG_LOCAL})
endif()
