cmake_minimum_required(VERSION 3.15)
project(SDKTestPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找 Qt5
find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)

# SDK 构建根目录（包含 win-sdk/Release 和 core/src/Release 的父目录）
# 支持两种传入方式：
#   1. -DSDK_BUILD_ROOT=<build/win-sdk-Release>  （推荐，自动推导子目录）
#   2. -DSDK_BUILD_DIR=<build/win-sdk-Release/win-sdk/Release>  （旧方式，只能链接 sdk，不含 core）
if(DEFINED SDK_BUILD_ROOT AND NOT SDK_BUILD_ROOT STREQUAL "")
    set(_SDK_WIN_LIB_DIR  "${SDK_BUILD_ROOT}/win-sdk/Release")
    set(_SDK_CORE_LIB_DIR "${SDK_BUILD_ROOT}/core/src/Release")
    set(_SDK_LABEL "${SDK_BUILD_ROOT}")
elseif(DEFINED SDK_BUILD_DIR AND NOT SDK_BUILD_DIR STREQUAL "")
    set(_SDK_WIN_LIB_DIR  "${SDK_BUILD_DIR}")
    # 从 win-sdk/Release 向上两级推导 core
    get_filename_component(_SDK_BUILD_PARENT "${SDK_BUILD_DIR}/../.." ABSOLUTE)
    set(_SDK_CORE_LIB_DIR "${_SDK_BUILD_PARENT}/core/src/Release")
    set(_SDK_LABEL "${SDK_BUILD_DIR}")
else()
    set(_DEFAULT_ROOT "${CMAKE_SOURCE_DIR}/../build/win-sdk-Release")
    set(_SDK_WIN_LIB_DIR  "${_DEFAULT_ROOT}/win-sdk/Release")
    set(_SDK_CORE_LIB_DIR "${_DEFAULT_ROOT}/core/src/Release")
    set(_SDK_LABEL "${_DEFAULT_ROOT}")
endif()

set(SDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../win-sdk")
set(SDK_LIB  "${_SDK_WIN_LIB_DIR}/hxcplayer_sdk.lib")
set(SDK_DLL  "${_SDK_WIN_LIB_DIR}/hxcplayer_sdk.dll")
set(CORE_LIB "${_SDK_CORE_LIB_DIR}/hxcplayer_core.lib")

if(NOT EXISTS "${SDK_LIB}")
    message(FATAL_ERROR
        "SDK lib not found: ${SDK_LIB}\n"
        "Please build the SDK first:\n"
        "  cmake --build <build/win-sdk-Release> --config Release\n"
        "Then pass -DSDK_BUILD_ROOT=<build/win-sdk-Release>")
endif()

message(STATUS "Using SDK from: ${_SDK_LABEL}")
message(STATUS "SDK include: ${SDK_INCLUDE_DIR}")
message(STATUS "SDK lib:     ${SDK_LIB}")
message(STATUS "Core lib:    ${CORE_LIB}")

# 创建可执行文件
add_executable(SDKTestPlayer
    main.cpp
    player_window.cpp
    player_window.h
    player_window.ui
)

# 包含 SDK 头文件（win-sdk 和 core/include，因为 hxcplayer_sdk.h 依赖 hxc_player_core_c_bridge.h）
target_include_directories(SDKTestPlayer PRIVATE
    ${SDK_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/../core/include
)

# 第三方依赖库路径
set(FFMPEG_LIB_DIR "${CMAKE_SOURCE_DIR}/../win-third/ffmpeg-install/lib")
set(SDL2_LIB_DIR   "${CMAKE_SOURCE_DIR}/../win-third/sdl2-install/release/lib")

# 链接 SDK、core 和 Qt（core 包含 player_core_* C 桥接函数）
# 同时链接 FFmpeg 和 SDL2 的 import lib（因为 core 静态库依赖它们）
set(_LINK_LIBS
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)

if(EXISTS "${CORE_LIB}")
    list(PREPEND _LINK_LIBS ${CORE_LIB})
    message(STATUS "Core lib found, linking: ${CORE_LIB}")
else()
    message(WARNING "Core lib not found at ${CORE_LIB}, player_core_* symbols may be missing")
endif()
list(PREPEND _LINK_LIBS ${SDK_LIB})

# FFmpeg import libs
foreach(_lib avcodec avformat avutil swresample swscale avfilter)
    set(_lib_path "${FFMPEG_LIB_DIR}/${_lib}.lib")
    if(EXISTS "${_lib_path}")
        list(APPEND _LINK_LIBS ${_lib_path})
    else()
        message(WARNING "FFmpeg lib not found: ${_lib_path}")
    endif()
endforeach()

# SDL2 import lib
set(_sdl2_lib "${SDL2_LIB_DIR}/SDL2.lib")
if(EXISTS "${_sdl2_lib}")
    list(APPEND _LINK_LIBS ${_sdl2_lib})
else()
    message(WARNING "SDL2 lib not found: ${_sdl2_lib}")
endif()

target_link_libraries(SDKTestPlayer PRIVATE ${_LINK_LIBS})

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
    
    # 复制 hxcplayer_sdk.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDK_DLL}"
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
