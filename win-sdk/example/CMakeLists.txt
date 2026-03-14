# CMakeLists.txt for HXCPlayer SDK Example

cmake_minimum_required(VERSION 3.15)
project(HXCPlayerExample)

# 设置 C 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 查找 HXCPlayer SDK
# 假设 SDK 在 ../lib 和 ../include
set(HXCPLAYER_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(HXCPLAYER_INCLUDE_DIR "${HXCPLAYER_SDK_DIR}/include")
set(HXCPLAYER_LIB_DIR "${HXCPLAYER_SDK_DIR}/lib")
set(HXCPLAYER_BIN_DIR "${HXCPLAYER_SDK_DIR}/bin")

# 创建示例可执行文件
add_executable(simple_player simple_player.c)

# 包含头文件
target_include_directories(simple_player PRIVATE ${HXCPLAYER_INCLUDE_DIR})

# 链接 HXCPlayer 导入库
target_link_libraries(simple_player PRIVATE "${HXCPLAYER_LIB_DIR}/hxcplayer.lib")

# Windows 特定设置
if(WIN32)
    # 复制 DLL 到输出目录
    add_custom_command(TARGET simple_player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HXCPLAYER_BIN_DIR}/hxcplayer.dll"
            "$<TARGET_FILE_DIR:simple_player>"
        COMMENT "Copying hxcplayer.dll to output directory"
    )
    
    # 如果有其他依赖 DLL，也复制它们
    if(EXISTS "${HXCPLAYER_BIN_DIR}")
        file(GLOB DEPENDENCY_DLLS "${HXCPLAYER_BIN_DIR}/*.dll")
        foreach(dll ${DEPENDENCY_DLLS})
            add_custom_command(TARGET simple_player POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${dll}"
                    "$<TARGET_FILE_DIR:simple_player>"
            )
        endforeach()
    endif()
endif()

message(STATUS "HXCPlayer SDK Example configured")
message(STATUS "  SDK Dir: ${HXCPLAYER_SDK_DIR}")
message(STATUS "  Include: ${HXCPLAYER_INCLUDE_DIR}")
message(STATUS "  Library: ${HXCPLAYER_LIB_DIR}")
