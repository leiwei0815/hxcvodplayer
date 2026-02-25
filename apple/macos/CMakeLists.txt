cmake_minimum_required(VERSION 3.15)

# 项目根目录
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# 为 Xcode generator 设置编译器
set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
set(CMAKE_OBJC_COMPILER "/usr/bin/clang")
set(CMAKE_OBJCXX_COMPILER "/usr/bin/clang++")

# 设置项目名称
project(HXCPlayer-macOS)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 只支持 macOS
if(NOT APPLE)
    message(FATAL_ERROR "此项目仅支持 macOS 平台")
endif()

# 输出编译信息
message(STATUS "========================================")
message(STATUS "HXC Player - macOS Native (纯 Cocoa)")
message(STATUS "========================================")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "项目根目录: ${PROJECT_ROOT}")
message(STATUS "========================================")

# FFmpeg 路径（使用 Homebrew 安装的 FFmpeg）
set(FFMPEG_ROOT "/opt/homebrew")
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")

# SoundTouch 路径（使用 Homebrew 安装的 SoundTouch）
set(SOUNDTOUCH_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(SOUNDTOUCH_LIB_DIR "${FFMPEG_ROOT}/lib")

# 注意：macOS 原生项目不使用 SDL2
# 音视频渲染使用 AVFoundation + AudioQueue

# 包含目录
include_directories(
    ${PROJECT_ROOT}/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}  # 包含当前目录（HXCPlayerView）
    ${FFMPEG_INCLUDE_DIR}
    ${SOUNDTOUCH_INCLUDE_DIR}
)

# 链接目录
link_directories(
    ${FFMPEG_LIB_DIR}
    ${SOUNDTOUCH_LIB_DIR}
)

# 核心库源文件
set(CORE_SOURCES
    ${PROJECT_ROOT}/core/src/hxc_player_types.cpp
    ${PROJECT_ROOT}/core/src/hxc_packet_queue.cpp
    ${PROJECT_ROOT}/core/src/hxc_decoder.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_core.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_core_c_bridge.cpp
)

# 核心库头文件
set(CORE_HEADERS
    ${PROJECT_ROOT}/core/include/hxc_player_types.h
    ${PROJECT_ROOT}/core/include/hxc_packet_queue.h
    ${PROJECT_ROOT}/core/include/hxc_decoder.h
    ${PROJECT_ROOT}/core/include/hxc_player_core.h
    ${PROJECT_ROOT}/core/include/hxc_player_core_c_bridge.h
    ${PROJECT_ROOT}/core/include/hxc_frame_queue.h
    ${PROJECT_ROOT}/core/include/hxc_logger.h
    ${PROJECT_ROOT}/core/include/hxc_debug_helper.h
)

# macOS 播放器控制类（使用统一的播放器）
set(PLAYER_SOURCES
    ${PROJECT_ROOT}/apple/HXCPlayerControl.mm
    ${PROJECT_ROOT}/apple/HXCPlayerView.mm
)

set(PLAYER_HEADERS
    ${PROJECT_ROOT}/apple/HXCPlayerControl.h
    ${PROJECT_ROOT}/apple/HXCPlayerView.h
)

# macOS 应用源文件
set(MACOS_SOURCES
    main.mm
    AppDelegate.mm
    PlayerViewController.mm
)

set(MACOS_HEADERS
    AppDelegate.h
    PlayerViewController.h
)

# 为纯 C++ 文件（核心库）设置编译标志
set_source_files_properties(
    ${CORE_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-x c++ -Wno-deprecated-declarations"
)

# 为 Objective-C++ 文件设置编译标志
set_source_files_properties(
    ${PLAYER_SOURCES}
    ${MACOS_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-x objective-c++ -fobjc-arc -Wno-deprecated-declarations"
)

# 创建可执行文件
add_executable(HXCPlayer-macOS MACOSX_BUNDLE
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${PLAYER_SOURCES}
    ${PLAYER_HEADERS}
    ${MACOS_SOURCES}
    ${MACOS_HEADERS}
)

# 链接 FFmpeg 库
target_link_libraries(HXCPlayer-macOS
    # FFmpeg
    avcodec
    avformat
    avutil
    swscale
    swresample
    
    # SoundTouch
    SoundTouch
    
    # 注意：macOS 原生项目不使用 SDL2
    # 音视频渲染使用 AVFoundation + AudioQueue
    
    # macOS 系统框架
    "-framework Foundation"
    "-framework Cocoa"
    "-framework AVFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework VideoToolbox"
    "-framework AudioToolbox"
    "-framework QuartzCore"
    "-framework Security"
    
    # 系统库
    bz2
    z
    iconv
    c++
)

# 编译选项
target_compile_options(HXCPlayer-macOS PRIVATE
    -Wno-deprecated-declarations
)

# 定义 NO_SDL 宏（macOS 原生项目不使用 SDL）
target_compile_definitions(HXCPlayer-macOS PRIVATE
    NO_SDL=1
)

# 设置 macOS Bundle 属性
set_target_properties(HXCPlayer-macOS PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
    MACOSX_BUNDLE_BUNDLE_NAME "HXC Player"
    MACOSX_BUNDLE_EXECUTABLE_NAME "HXCPlayer-macOS"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.hxcplayer.macos"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 在 Xcode 中组织文件
source_group("Core/Sources" FILES ${CORE_SOURCES})
source_group("Core/Headers" FILES ${CORE_HEADERS})
source_group("Player/Sources" FILES ${PLAYER_SOURCES})
source_group("Player/Headers" FILES ${PLAYER_HEADERS})
source_group("macOS/Sources" FILES ${MACOS_SOURCES})
source_group("macOS/Headers" FILES ${MACOS_HEADERS})

# 输出信息
message(STATUS "")
message(STATUS "HXCPlayer-macOS 配置完成:")
message(STATUS "  FFmpeg 路径: ${FFMPEG_ROOT}")
message(STATUS "  SoundTouch 路径: ${SOUNDTOUCH_INCLUDE_DIR}")
message(STATUS "  输出路径: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "")
message(STATUS "编译命令:")
message(STATUS "  mkdir -p build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make -j8")
message(STATUS "")
message(STATUS "运行命令:")
message(STATUS "  open bin/HXCPlayer-macOS.app")
message(STATUS "")
