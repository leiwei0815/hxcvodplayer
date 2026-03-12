cmake_minimum_required(VERSION 3.15)
project(HXCVodPlayer VERSION 1.0.0 LANGUAGES CXX)

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
if(WIN32)
    # Windows 平台通常使用 vcpkg，直接使用 find_package
    find_package(PkgConfig)
    
    # FFmpeg
    find_package(FFMPEG)
    if(NOT FFMPEG_FOUND)
        # 手动查找 FFmpeg（vcpkg 方式）
        find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h)
        find_library(AVCODEC_LIBRARY avcodec)
        find_library(AVFORMAT_LIBRARY avformat)
        find_library(AVUTIL_LIBRARY avutil)
        find_library(SWSCALE_LIBRARY swscale)
        find_library(SWRESAMPLE_LIBRARY swresample)
        
        if(FFMPEG_INCLUDE_DIR AND AVCODEC_LIBRARY AND AVFORMAT_LIBRARY AND AVUTIL_LIBRARY AND SWSCALE_LIBRARY AND SWRESAMPLE_LIBRARY)
            set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
            set(FFMPEG_LIBRARIES ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY} ${SWRESAMPLE_LIBRARY})
            message(STATUS "✓ FFmpeg found (manual)")
        else()
            message(FATAL_ERROR "FFmpeg not found. Please install via vcpkg: vcpkg install ffmpeg:x64-windows")
        endif()
    endif()
    
    # SDL2
    find_package(SDL2 CONFIG)
    if(NOT SDL2_FOUND)
        # 手动查找 SDL2
        find_path(SDL2_INCLUDE_DIR SDL2/SDL.h)
        find_library(SDL2_LIBRARY SDL2)
        find_library(SDL2MAIN_LIBRARY SDL2main)
        
        if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
            set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
            set(SDL2_LIBRARIES ${SDL2_LIBRARY} ${SDL2MAIN_LIBRARY})
            message(STATUS "✓ SDL2 found (manual)")
        else()
            message(FATAL_ERROR "SDL2 not found. Please install via vcpkg: vcpkg install sdl2:x64-windows")
        endif()
    endif()
else()
    # macOS/Linux 使用 pkg-config
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
endif()

# SoundTouch（用于倍速播放，仅 macOS 和 Windows）
if(APPLE)
    # ==========================================
    # macOS: 使用 Homebrew 安装的 SoundTouch
    # ==========================================
    message(STATUS "Looking for SoundTouch (macOS)...")
    
    # 查找头文件
    find_path(SOUNDTOUCH_INCLUDE_DIR 
        NAMES soundtouch/SoundTouch.h
        PATHS 
            /opt/homebrew/include  # Apple Silicon
            /usr/local/include     # Intel Mac
        NO_DEFAULT_PATH
    )
    
    # 如果没找到，使用默认路径
    if(NOT SOUNDTOUCH_INCLUDE_DIR)
        find_path(SOUNDTOUCH_INCLUDE_DIR 
            NAMES soundtouch/SoundTouch.h
        )
    endif()
    
    # 查找库文件
    find_library(SOUNDTOUCH_LIBRARY
        NAMES SoundTouch soundtouch
        PATHS 
            /opt/homebrew/lib      # Apple Silicon
            /usr/local/lib         # Intel Mac
        NO_DEFAULT_PATH
    )
    
    # 如果没找到，使用默认路径
    if(NOT SOUNDTOUCH_LIBRARY)
        find_library(SOUNDTOUCH_LIBRARY
            NAMES SoundTouch soundtouch
        )
    endif()
    
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
        message(STATUS "  Hint: Install with 'brew install sound-touch'")
    endif()
    
elseif(WIN32)
    # ==========================================
    # Windows: 优先使用本地编译的 SoundTouch
    # ==========================================
    message(STATUS "Looking for SoundTouch (Windows)...")
    
    # 根据构建类型选择目录
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(SOUNDTOUCH_LOCAL_DIR "${CMAKE_SOURCE_DIR}/win-third/soundtouch-install/debug")
    else()
        set(SOUNDTOUCH_LOCAL_DIR "${CMAKE_SOURCE_DIR}/win-third/soundtouch-install/release")
    endif()
    
    message(STATUS "  Priority 1: Local build at ${SOUNDTOUCH_LOCAL_DIR}")
    
    # 查找头文件
    find_path(SOUNDTOUCH_INCLUDE_DIR 
        NAMES soundtouch/SoundTouch.h
        PATHS 
            # 1. 本地编译版本（优先）
            ${SOUNDTOUCH_LOCAL_DIR}/include
            # 2. vcpkg 安装
            ${CMAKE_PREFIX_PATH}/include
            $ENV{VCPKG_ROOT}/installed/x64-windows/include
        NO_DEFAULT_PATH
    )
    
    # 如果没找到，使用默认路径
    if(NOT SOUNDTOUCH_INCLUDE_DIR)
        find_path(SOUNDTOUCH_INCLUDE_DIR 
            NAMES soundtouch/SoundTouch.h
        )
    endif()
    
    # 查找库文件
    find_library(SOUNDTOUCH_LIBRARY
        NAMES SoundTouch SoundTouch.lib
        PATHS 
            # 1. 本地编译版本（优先）
            ${SOUNDTOUCH_LOCAL_DIR}/lib
            # 2. vcpkg 安装
            ${CMAKE_PREFIX_PATH}/lib
            $ENV{VCPKG_ROOT}/installed/x64-windows/lib
        NO_DEFAULT_PATH
    )
    
    # 如果没找到，使用默认路径
    if(NOT SOUNDTOUCH_LIBRARY)
        find_library(SOUNDTOUCH_LIBRARY
            NAMES SoundTouch SoundTouch.lib
        )
    endif()
    
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
        message(STATUS "  Hint 1: Compile locally with 'cd win-third && .\\build_soundtouch_multi.bat'")
        message(STATUS "  Hint 2: Or install via vcpkg: 'vcpkg install soundtouch:x64-windows'")
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
message(STATUS "HXCVodPlayer Configuration:")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  FFmpeg: ${FFMPEG_VERSION}")
message(STATUS "  SDL2: ${SDL2_VERSION}")
if(BUILD_DESKTOP)
    message(STATUS "  Qt5: ${Qt5_VERSION}")
endif()
message(STATUS "")
