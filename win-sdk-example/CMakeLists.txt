cmake_minimum_required(VERSION 3.15)
project(SDKTestPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找 Qt5
find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)

# SDK 路径（相对于本项目）
set(SDK_DIR "${CMAKE_SOURCE_DIR}/../build/win-sdk-Release/HXCPlayerSDK")

if(NOT EXISTS "${SDK_DIR}")
    message(FATAL_ERROR "SDK not found at: ${SDK_DIR}\n"
        "Please build SDK first:\n"
        "  cd win-sdk\n"
        "  build_sdk.bat")
endif()

message(STATUS "Using SDK from: ${SDK_DIR}")

# 创建可执行文件
add_executable(SDKTestPlayer
    main.cpp
    player_window.cpp
    player_window.h
    player_window.ui
)

# 包含 SDK 头文件
target_include_directories(SDKTestPlayer PRIVATE
    ${SDK_DIR}/include
)

# 链接 SDK 和 Qt
target_link_libraries(SDKTestPlayer PRIVATE
    ${SDK_DIR}/lib/hxcplayer.lib
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)

# Windows 设置
if(WIN32)
    set_target_properties(SDKTestPlayer PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    
    target_compile_options(SDKTestPlayer PRIVATE /utf-8)
endif()

# 复制 SDK DLL 到输出目录
add_custom_command(TARGET SDKTestPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "复制 SDK DLL..."
    
    # 复制 hxcplayer.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDK_DIR}/bin/hxcplayer.dll"
        $<TARGET_FILE_DIR:SDKTestPlayer>
    
    # 复制本地编译的 FFmpeg 和 SDL2（优先）
    COMMAND ${CMAKE_COMMAND} -E echo "复制本地编译的依赖库..."
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/../win-third/ffmpeg-install/bin"
        $<TARGET_FILE_DIR:SDKTestPlayer>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/../win-third/sdl2-install/release/bin/SDL2.dll"
        $<TARGET_FILE_DIR:SDKTestPlayer>
    
    COMMENT "Copying SDK and dependencies to output directory"
)

# 输出位置
set_target_properties(SDKTestPlayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "SDK Test Player configured successfully")
