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

# SoundTouch（用于倍速播放，仅 macOS 和 Windows）
if(APPLE OR WIN32)
    # SoundTouch 不提供标准的 .pc 文件，需要手动查找
    # 在不同平台上，库名和路径可能不同
    
    # 查找头文件
    find_path(SOUNDTOUCH_INCLUDE_DIR 
        NAMES soundtouch/SoundTouch.h
        PATHS 
            # macOS (Homebrew)
            /opt/homebrew/include
            /usr/local/include
            # Windows (vcpkg)
            ${CMAKE_PREFIX_PATH}/include
            $ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include
            # 通用路径
            /usr/include
    )
    
    # 查找库文件
    find_library(SOUNDTOUCH_LIBRARY
        NAMES 
            SoundTouch          # macOS/Linux
            soundtouch          # 小写变体
            SoundTouch.lib      # Windows (静态库)
            SoundTouch_dll.lib  # Windows (动态库)
        PATHS 
            # macOS (Homebrew)
            /opt/homebrew/lib
            /usr/local/lib
            # Windows (vcpkg)
            ${CMAKE_PREFIX_PATH}/lib
            $ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib
            # 通用路径
            /usr/lib
    )
    
    if(SOUNDTOUCH_INCLUDE_DIR AND SOUNDTOUCH_LIBRARY)
        set(SOUNDTOUCH_FOUND TRUE)
        set(SOUNDTOUCH_INCLUDE_DIRS ${SOUNDTOUCH_INCLUDE_DIR})
        set(SOUNDTOUCH_LIBRARIES ${SOUNDTOUCH_LIBRARY})
        message(STATUS "✓ SoundTouch found!")
        message(STATUS "  Include: ${SOUNDTOUCH_INCLUDE_DIRS}")
        message(STATUS "  Library: ${SOUNDTOUCH_LIBRARIES}")
    else()
        set(SOUNDTOUCH_FOUND FALSE)
        message(WARNING "✗ SoundTouch not found, playback rate control will be disabled")
        message(STATUS "  Searched include dir: ${SOUNDTOUCH_INCLUDE_DIR}")
        message(STATUS "  Searched library: ${SOUNDTOUCH_LIBRARY}")
        if(APPLE)
            message(STATUS "  Hint: Install with 'brew install sound-touch'")
        elseif(WIN32)
            message(STATUS "  Hint: Install with 'vcpkg install soundtouch'")
        endif()
    endif()
endif()

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/core/include
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

if(SOUNDTOUCH_FOUND)
    include_directories(${SOUNDTOUCH_INCLUDE_DIRS})
endif()

# 链接目录
link_directories(
    ${FFMPEG_LIBRARY_DIRS}
    ${SDL2_LIBRARY_DIRS}
)

if(SOUNDTOUCH_FOUND)
    link_directories(${SOUNDTOUCH_LIBRARY_DIRS})
endif()

# 核心库
add_subdirectory(core/src)

# 桌面版本
if(BUILD_DESKTOP AND (PLATFORM_MACOS OR PLATFORM_WINDOWS))
    find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)
    add_subdirectory(desktop)
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
