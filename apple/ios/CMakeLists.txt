cmake_minimum_required(VERSION 3.20)
project(YXVodPlayer-iOS VERSION 1.0.0 LANGUAGES C CXX OBJC OBJCXX)

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

# 根据 SDK 类型选择正确的库路径
if(CMAKE_OSX_SYSROOT MATCHES ".*simulator.*" OR CMAKE_OSX_SYSROOT MATCHES ".*Simulator.*")
    # iOS 模拟器 (arm64 + x86_64 通用二进制)
    set(IOS_PLATFORM_DIR "ios-simulator")
    message(STATUS "构建目标: iOS 模拟器")
else()
    # iOS 真机 (arm64)
    set(IOS_PLATFORM_DIR "ios-device")
    message(STATUS "构建目标: iOS 真机")
endif()

# FFmpeg 和 SoundTouch 路径
set(FFMPEG_ROOT "${PROJECT_ROOT}/ios-third/ffmpeg-build-ios/FFmpeg-iOS/${IOS_PLATFORM_DIR}")
set(SOUNDTOUCH_ROOT "${PROJECT_ROOT}/ios-third/soundtouch-build-ios/SoundTouch-iOS/${IOS_PLATFORM_DIR}")

# 检查 FFmpeg 是否存在
if(EXISTS "${FFMPEG_ROOT}/lib/libavcodec.a")
    message(STATUS "✅ 找到 iOS FFmpeg 静态库")
    set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
    set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")
    set(HAS_FFMPEG TRUE)
else()
    message(FATAL_ERROR "❌ 未找到 iOS FFmpeg 静态库！请先运行 ios-third/build_ffmpeg_ios.sh 编译")
endif()

# 检查 SoundTouch 是否存在
if(EXISTS "${SOUNDTOUCH_ROOT}/lib/libSoundTouch.a")
    message(STATUS "✅ 找到 iOS SoundTouch 静态库")
    set(SOUNDTOUCH_INCLUDE_DIR "${SOUNDTOUCH_ROOT}/include")
    set(SOUNDTOUCH_LIB_DIR "${SOUNDTOUCH_ROOT}/lib")
    set(HAS_SOUNDTOUCH TRUE)
else()
    message(WARNING "⚠️  未找到 iOS SoundTouch 静态库，倍速播放功能将被禁用")
    message(WARNING "    请运行 ios-third/build_soundtouch_ios.sh 编译")
    set(HAS_SOUNDTOUCH FALSE)
endif()

# 包含目录
include_directories(
    ${PROJECT_ROOT}/core/include
    ${FFMPEG_INCLUDE_DIR}
)

# 如果有 SoundTouch，添加其包含目录
if(HAS_SOUNDTOUCH)
    include_directories(${SOUNDTOUCH_INCLUDE_DIR})
endif()

# 链接目录
link_directories(
    ${FFMPEG_LIB_DIR}
)

# 如果有 SoundTouch，添加其链接目录
if(HAS_SOUNDTOUCH)
    link_directories(${SOUNDTOUCH_LIB_DIR})
endif()

# 核心库源文件
set(CORE_SOURCES
    ${PROJECT_ROOT}/core/src/hxc_player_types.cpp
    ${PROJECT_ROOT}/core/src/hxc_packet_queue.cpp
    ${PROJECT_ROOT}/core/src/hxc_decoder.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_core.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_core_c_bridge.cpp
    ${PROJECT_ROOT}/core/src/hxc_custom_io.cpp
    ${PROJECT_ROOT}/core/src/hxc_audio_resampler.cpp
    ${PROJECT_ROOT}/core/src/hxc_aes256_cbc.c
)

# 核心库头文件
set(CORE_HEADERS
    ${PROJECT_ROOT}/core/include/hxc_player_types.h
    ${PROJECT_ROOT}/core/include/hxc_packet_queue.h
    ${PROJECT_ROOT}/core/include/hxc_decoder.h
    ${PROJECT_ROOT}/core/include/hxc_player_core.h
    ${PROJECT_ROOT}/core/include/hxc_player_core_c_bridge.h
    ${PROJECT_ROOT}/core/include/hxc_custom_io.h
    ${PROJECT_ROOT}/core/include/hxc_frame_queue.h
    ${PROJECT_ROOT}/core/include/hxc_logger.h
    ${PROJECT_ROOT}/core/include/hxc_debug_helper.h
    ${PROJECT_ROOT}/core/include/hxc_audio_resampler.h
    ${PROJECT_ROOT}/core/include/hxc_aes256_cbc.h
)

# HXCVD 下载模块
set(HXCVD_SOURCES
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadItem.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDPersistenceController.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDFileStore.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDPlaylistParser.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDHLSDownloadEngine.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadManager.m
)

set(HXCVD_HEADERS
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownload.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadTypes.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadItem.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadItem+Internal.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDPersistenceController.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDFileStore.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDPlaylistParser.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDHLSDownloadEngine.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadManager.h
)

# Apple 播放器控制类（使用统一的播放器）
set(PLAYER_SOURCES
    ${PROJECT_ROOT}/apple/HXCPlayerControl.mm
    ${PROJECT_ROOT}/apple/HXCPlayerView.mm
)

set(PLAYER_HEADERS
    ${PROJECT_ROOT}/apple/HXCPlayerControl.h
    ${PROJECT_ROOT}/apple/HXCPlayerTypes.h
    ${PROJECT_ROOT}/apple/HXCPlayerLog.h
    ${PROJECT_ROOT}/apple/HXCPlayerView.h
    ${PROJECT_ROOT}/apple/HXCAESUtility.h
    ${PROJECT_ROOT}/apple/HXCPlayerLicenseManager.h
)

set(PLAYER_OBJC_SOURCES
    ${PROJECT_ROOT}/apple/HXCAESUtility.m
    ${PROJECT_ROOT}/apple/HXCPlayerLicenseManager.m
    ${PROJECT_ROOT}/apple/HXCPlayerLicenseCrypto.m
)

# iOS 源文件（使用统一的播放器类）
set(IOS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/PlayerViewController.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/HXCVDownloadViewController.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/AppDelegate.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/main.mm
)

# iOS 头文件
set(IOS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/PlayerViewController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HXCVDownloadViewController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/AppDelegate.h
)

# 为纯 C++ 文件（不是 .mm）禁用 Objective-C++ 模式
# 为纯 C++ 文件强制使用标准 C++ 编译模式（避免看到 iOS 类型）
set_source_files_properties(
    ${CORE_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-x c++ -Wno-deprecated-declarations"
)

# 纯 Objective-C 源文件
set_source_files_properties(
    ${HXCVD_SOURCES}
    ${PLAYER_OBJC_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-x objective-c -fobjc-arc -Wno-deprecated-declarations"
)

# Objective-C++ 源文件
set_source_files_properties(
    ${PLAYER_SOURCES}
    ${IOS_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-x objective-c++ -fobjc-arc -Wno-deprecated-declarations"
)

# 创建可执行文件（包含源文件和头文件）
add_executable(YXVodPlayer-iOS MACOSX_BUNDLE
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${PLAYER_SOURCES}
    ${PLAYER_OBJC_SOURCES}
    ${PLAYER_HEADERS}
    ${HXCVD_SOURCES}
    ${HXCVD_HEADERS}
    ${IOS_SOURCES}
    ${IOS_HEADERS}
)

# 设置包含目录（确保对所有源文件生效，包括设置了 -x c++ 的文件）
target_include_directories(YXVodPlayer-iOS PRIVATE
    ${PROJECT_ROOT}/core/include
    ${PROJECT_ROOT}/apple/HXCVDownload
    ${FFMPEG_INCLUDE_DIR}
)

# 如果有 SoundTouch，添加其包含目录
if(HAS_SOUNDTOUCH)
    target_include_directories(YXVodPlayer-iOS PRIVATE ${SOUNDTOUCH_INCLUDE_DIR})
endif()

# 强制设置 Xcode 的 HEADER_SEARCH_PATHS
if(HAS_SOUNDTOUCH)
    set_target_properties(YXVodPlayer-iOS PROPERTIES
        XCODE_ATTRIBUTE_HEADER_SEARCH_PATHS "${PROJECT_ROOT}/core/include ${FFMPEG_INCLUDE_DIR} ${SOUNDTOUCH_INCLUDE_DIR}"
    )
else()
    set_target_properties(YXVodPlayer-iOS PROPERTIES
        XCODE_ATTRIBUTE_HEADER_SEARCH_PATHS "${PROJECT_ROOT}/core/include ${FFMPEG_INCLUDE_DIR}"
    )
endif()

# 链接库
target_link_libraries(YXVodPlayer-iOS
    # FFmpeg
    avcodec
    avformat
    avutil
    swscale
    swresample
    
    # iOS 系统框架
    "-framework Foundation"
    "-framework UIKit"
    "-framework AVFoundation"
    "-framework AVKit"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework VideoToolbox"
    "-framework AudioToolbox"
    "-framework CoreGraphics"
    "-framework QuartzCore"
    "-framework Security"
    "-framework CFNetwork"
    "-framework CoreData"
    
    # 系统库
    bz2
    z
    iconv
    c++
)

# 如果有 SoundTouch，链接它
if(HAS_SOUNDTOUCH)
    target_link_libraries(YXVodPlayer-iOS SoundTouch)
endif()

# 编译选项
target_compile_options(YXVodPlayer-iOS PRIVATE
    -Wno-deprecated-declarations
    -fembed-bitcode-marker
)

# 定义宏
set(COMPILE_DEFINITIONS NO_SDL=1)

# 如果找到 SoundTouch，添加 HAS_SOUNDTOUCH 宏
if(HAS_SOUNDTOUCH)
    list(APPEND COMPILE_DEFINITIONS HAS_SOUNDTOUCH=1)
    message(STATUS "✅ 已启用 SoundTouch 支持（倍速播放）")
else()
    message(STATUS "⚠️  SoundTouch 未启用，倍速播放功能将被禁用")
endif()

target_compile_definitions(YXVodPlayer-iOS PRIVATE ${COMPILE_DEFINITIONS})

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
source_group("Core/Headers" FILES ${CORE_HEADERS})
source_group("Core/Sources" FILES ${CORE_SOURCES})
source_group("Player/Sources" FILES ${PLAYER_SOURCES})
source_group("Player/Headers" FILES ${PLAYER_HEADERS})
source_group("HXCVD/Sources" FILES ${HXCVD_SOURCES})
source_group("HXCVD/Headers" FILES ${HXCVD_HEADERS})
source_group("iOS/Headers" FILES ${IOS_HEADERS})
source_group("iOS/Sources" FILES ${IOS_SOURCES})

# 输出信息
message(STATUS "")
message(STATUS "HXCPlayer iOS Configuration:")
message(STATUS "  Platform: ${IOS_PLATFORM_DIR}")
message(STATUS "  FFmpeg Root: ${FFMPEG_ROOT}")
message(STATUS "  FFmpeg Include: ${FFMPEG_INCLUDE_DIR}")
message(STATUS "  FFmpeg Lib: ${FFMPEG_LIB_DIR}")
message(STATUS "  SoundTouch Root: ${SOUNDTOUCH_ROOT}")
message(STATUS "  SoundTouch Include: ${SOUNDTOUCH_INCLUDE_DIR}")
message(STATUS "  SoundTouch Lib: ${SOUNDTOUCH_LIB_DIR}")
message(STATUS "  SDK: ${CMAKE_OSX_SYSROOT}")
message(STATUS "  Deployment Target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
message(STATUS "")
