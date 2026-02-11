cmake_minimum_required(VERSION 3.15)
project(YXVodPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 调试配置
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")
    add_compile_options(-fno-omit-frame-pointer)
endif()

# Xcode 特定配置
if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_XCODE_GENERATE_SCHEME ON)
    set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
    set(CMAKE_XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL "0")
    set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES")
endif()

# 平台检测
if(APPLE)
    if(IOS)
        set(PLATFORM_IOS ON)
    else()
        set(PLATFORM_MACOS ON)
    endif()
elseif(ANDROID)
    set(PLATFORM_ANDROID ON)
elseif(WIN32)
    set(PLATFORM_WINDOWS ON)
endif()

# 选项
option(BUILD_DESKTOP "Build desktop version (Qt5)" ON)
option(BUILD_TESTS "Build tests" OFF)

# 查找依赖
find_package(PkgConfig REQUIRED)

# FFmpeg
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
)

# SDL2
pkg_check_modules(SDL2 REQUIRED sdl2)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

# 链接目录
link_directories(
    ${FFMPEG_LIBRARY_DIRS}
    ${SDL2_LIBRARY_DIRS}
)

# 核心库
add_subdirectory(src/core)

# 平台抽象层
add_subdirectory(src/platform)

# 桌面版本
if(BUILD_DESKTOP AND (PLATFORM_MACOS OR PLATFORM_WINDOWS))
    find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)
    add_subdirectory(src/desktop)
endif()

# 输出信息
message(STATUS "")
message(STATUS "YXVodPlayer Configuration:")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  FFmpeg: ${FFMPEG_VERSION}")
message(STATUS "  SDL2: ${SDL2_VERSION}")
if(BUILD_DESKTOP)
    message(STATUS "  Qt5: ${Qt5_VERSION}")
endif()
message(STATUS "")
