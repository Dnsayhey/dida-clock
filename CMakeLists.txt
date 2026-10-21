cmake_minimum_required(VERSION 3.16)

# 固件版本由 version.txt 决定，OTA manifest 用同一套语义化版本。
# 发版时修改这个文件；不要依赖 git describe，否则 -dirty 版本无法比较。
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/version.txt")
    # 让 version.txt 的变化自动触发 CMake 重新配置，否则 PROJECT_VER 会缓存旧值。
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_LIST_DIR}/version.txt")
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" DIDA_VERSION LIMIT_COUNT 1)
    if(DIDA_VERSION)
        set(PROJECT_VER "${DIDA_VERSION}")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(dida)
