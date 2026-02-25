cmake_minimum_required(VERSION 3.20)
project(YXVodPlayer-iOS VERSION 1.0.0 LANGUAGES CXX OBJCXX)

# iOS 平台配置
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum iOS deployment version")
set(CMAKE_XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2" CACHE STRING "iPhone and iPad")
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES")
set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)

# 项目根目录（从 apple/ios 向上两级）
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# FFmpeg 路径
set(FFMPEG_ROOT "${PROJECT_ROOT}/ios-third/ffmpeg-build/FFmpeg-iOS")
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")

# SoundTouch 路径（根据编译目标选择）
set(SOUNDTOUCH_ROOT "${PROJECT_ROOT}/ios-third/soundtouch-build/SoundTouch-iOS")
set(SOUNDTOUCH_INCLUDE_DIR "${SOUNDTOUCH_ROOT}/include")

# 根据 SDK 类型选择 SoundTouch 库路径
if(CMAKE_OSX_SYSROOT MATCHES ".*simulator.*" OR CMAKE_OSX_SYSROOT MATCHES ".*Simulator.*")
    # 模拟器
    set(SOUNDTOUCH_LIB_DIR "${SOUNDTOUCH_ROOT}/lib/simulator")
    set(SOUNDTOUCH_LIBRARY "${SOUNDTOUCH_LIB_DIR}/libSoundTouch.a")
    message(STATUS "使用 SoundTouch 模拟器库: ${SOUNDTOUCH_LIBRARY}")
else()
    # 真机
    set(SOUNDTOUCH_LIB_DIR "${SOUNDTOUCH_ROOT}/lib/device")
    set(SOUNDTOUCH_LIBRARY "${SOUNDTOUCH_LIB_DIR}/libSoundTouch.a")
    message(STATUS "使用 SoundTouch 真机库: ${SOUNDTOUCH_LIBRARY}")
endif()

# 包含目录
include_directories(
    ${PROJECT_ROOT}/core/include
    ${FFMPEG_INCLUDE_DIR}
    ${SOUNDTOUCH_INCLUDE_DIR}
)

# 链接目录（只需要 FFmpeg，SoundTouch 使用完整路径）
link_directories(
    ${FFMPEG_LIB_DIR}
)

# 核心库源文件
set(CORE_SOURCES
    ${PROJECT_ROOT}/core/src/hxc_player_core.cpp
    ${PROJECT_ROOT}/core/src/hxc_decoder.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_types.cpp
    ${PROJECT_ROOT}/core/src/hxc_packet_queue.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_core_c_bridge.cpp
)

# 核心库头文件
set(CORE_HEADERS
    ${PROJECT_ROOT}/core/include/hxc_player_core.h
    ${PROJECT_ROOT}/core/include/hxc_decoder.h
    ${PROJECT_ROOT}/core/include/hxc_player_types.h
    ${PROJECT_ROOT}/core/include/hxc_packet_queue.h
    ${PROJECT_ROOT}/core/include/hxc_frame_queue.h
    ${PROJECT_ROOT}/core/include/hxc_logger.h
    ${PROJECT_ROOT}/core/include/hxc_debug_helper.h
    ${PROJECT_ROOT}/core/include/hxc_player_core_c_bridge.h
)

# iOS 源文件（使用统一的播放器类）
set(IOS_SOURCES
    ${PROJECT_ROOT}/apple/HXCPlayerControl.mm
    ${PROJECT_ROOT}/apple/HXCPlayerView.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/PlayerViewController.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/AppDelegate.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/main.mm
)

# iOS 头文件
set(IOS_HEADERS
    ${PROJECT_ROOT}/apple/HXCPlayerControl.h
    ${PROJECT_ROOT}/apple/HXCPlayerView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/PlayerViewController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/AppDelegate.h
)

# 为纯 C++ 文件（不是 .mm）禁用 Objective-C++ 模式
# 为纯 C++ 文件强制使用标准 C++ 编译模式（避免看到 iOS 类型）
set_source_files_properties(
    ${CORE_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-x c++"
)

# 为 Objective-C++ 文件设置编译标志（解决 AVMediaType 冲突）
set_source_files_properties(
    ${PROJECT_ROOT}/apple/HXCPlayerControl.mm
    ${PROJECT_ROOT}/apple/HXCPlayerView.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/PlayerViewController.mm
    PROPERTIES
    COMPILE_FLAGS "-Wno-ambiguous-macro"
)

# 创建可执行文件（包含源文件和头文件）
add_executable(YXVodPlayer-iOS MACOSX_BUNDLE
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${IOS_SOURCES}
    ${IOS_HEADERS}
)

# 链接库
target_link_libraries(YXVodPlayer-iOS
    # FFmpeg
    avcodec
    avformat
    avutil
    swscale
    swresample
    
    # SoundTouch（使用完整路径）
    ${SOUNDTOUCH_LIBRARY}
    
    # iOS 系统框架
    "-framework Foundation"
    "-framework UIKit"
    "-framework AVFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework VideoToolbox"
    "-framework AudioToolbox"
    "-framework CoreGraphics"
    "-framework QuartzCore"
    "-framework Security"
    
    # 系统库
    bz2
    z
    iconv
    c++
)

# 编译选项
target_compile_options(YXVodPlayer-iOS PRIVATE
    -Wno-deprecated-declarations
    -fembed-bitcode-marker
)

# 禁用 SDL（iOS 不使用 SDL）
target_compile_definitions(YXVodPlayer-iOS PRIVATE
    NO_SDL=1
)

# 为所有 ObjC/ObjC++ 源文件启用 ARC
set_target_properties(YXVodPlayer-iOS PROPERTIES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)

# Bundle 属性
set_target_properties(YXVodPlayer-iOS PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
    MACOSX_BUNDLE_BUNDLE_NAME "HXCPlayer"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.hxcplayer.ios"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.hxcplayer.ios"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "13.0"
)

# Xcode 文件组织（创建虚拟文件夹）
source_group("Core\\Headers" FILES ${CORE_HEADERS})
source_group("Core\\Sources" FILES ${CORE_SOURCES})
source_group("iOS\\Headers" FILES ${IOS_HEADERS})
source_group("iOS\\Sources" FILES ${IOS_SOURCES})

# 输出信息
message(STATUS "")
message(STATUS "HXCPlayer iOS Configuration:")
message(STATUS "  FFmpeg: ${FFMPEG_ROOT}")
message(STATUS "  SoundTouch Include: ${SOUNDTOUCH_INCLUDE_DIR}")
message(STATUS "  SoundTouch Library: ${SOUNDTOUCH_LIBRARY}")
message(STATUS "  SDK: ${CMAKE_OSX_SYSROOT}")
message(STATUS "  Deployment Target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
message(STATUS "")
