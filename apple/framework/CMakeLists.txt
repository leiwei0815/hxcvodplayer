cmake_minimum_required(VERSION 3.20)
project(HXCPlayerFramework VERSION 1.0.0)

# 设置为 Framework
set(CMAKE_MACOSX_RPATH YES)
set(CMAKE_INSTALL_RPATH "@rpath")
set(CMAKE_BUILD_WITH_INSTALL_RPATH YES)

# 项目根目录
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 平台检测
if(IOS)
    set(PLATFORM "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
else()
    set(PLATFORM "macOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
endif()

message(STATUS "========================================")
message(STATUS "构建 HXCPlayer Framework (${PLATFORM})")
message(STATUS "========================================")

# ==================== 源文件 ====================

# 核心 C++ 源文件
set(CORE_SOURCES
    ${PROJECT_ROOT}/core/src/hxc_player_core.cpp
    ${PROJECT_ROOT}/core/src/hxc_decoder.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_types.cpp
    ${PROJECT_ROOT}/core/src/hxc_packet_queue.cpp
    ${PROJECT_ROOT}/core/src/hxc_audio_resampler.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_core_c_bridge.cpp
    ${PROJECT_ROOT}/core/src/hxc_custom_io.cpp
)

# Apple 平台 Objective-C++ 源文件
set(APPLE_SOURCES
    ${PROJECT_ROOT}/apple/HXCPlayerControl.mm
    ${PROJECT_ROOT}/apple/HXCPlayerView.mm
)

# HXCVD 下载模块（纯 Objective-C + Core Data）
set(HXCVD_SOURCES
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadItem.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDPersistenceController.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDFileStore.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDPlaylistParser.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDHLSDownloadEngine.m
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadManager.m
)

# 公共头文件（对外暴露的 API）
set(PUBLIC_HEADERS
    ${PROJECT_ROOT}/apple/HXCPlayer.h
    ${PROJECT_ROOT}/apple/HXCPlayerControl.h
    ${PROJECT_ROOT}/apple/HXCPlayerView.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownload.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadTypes.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadItem.h
    ${PROJECT_ROOT}/apple/HXCVDownload/HXCVDownloadManager.h
)

# 私有头文件（内部使用）
set(PRIVATE_HEADERS
    ${PROJECT_ROOT}/core/include/hxc_player_core.h
    ${PROJECT_ROOT}/core/include/hxc_decoder.h
    ${PROJECT_ROOT}/core/include/hxc_player_types.h
    ${PROJECT_ROOT}/core/include/hxc_packet_queue.h
    ${PROJECT_ROOT}/core/include/hxc_frame_queue.h
    ${PROJECT_ROOT}/core/include/hxc_player_core_c_bridge.h
    ${PROJECT_ROOT}/core/include/hxc_custom_io.h
    ${PROJECT_ROOT}/core/include/hxc_audio_resampler.h
    ${PROJECT_ROOT}/core/include/hxc_logger.h
    ${PROJECT_ROOT}/core/include/hxc_debug_helper.h
)

# ==================== 依赖配置 ====================

# 包含目录
include_directories(
    ${PROJECT_ROOT}/core/include
    ${PROJECT_ROOT}/apple/HXCVDownload
)

if(IOS)
    # iOS: 使用新编译的静态库
    # 根据 SDK 类型选择库路径
    if(CMAKE_OSX_SYSROOT MATCHES "simulator" OR CMAKE_OSX_SYSROOT MATCHES "Simulator")
        message(STATUS "配置 iOS 模拟器库 (arm64 + x86_64)")
        set(IOS_PLATFORM_DIR "ios-simulator")
    else()
        message(STATUS "配置 iOS 真机库 (arm64)")
        set(IOS_PLATFORM_DIR "ios-device")
    endif()
    
    set(FFMPEG_ROOT "${PROJECT_ROOT}/ios-third/ffmpeg-build-ios/FFmpeg-iOS/${IOS_PLATFORM_DIR}")
    set(SOUNDTOUCH_ROOT "${PROJECT_ROOT}/ios-third/soundtouch-build-ios/SoundTouch-iOS/${IOS_PLATFORM_DIR}")
    
    # 检查 FFmpeg 是否存在
    if(EXISTS "${FFMPEG_ROOT}/lib/libavcodec.a")
        message(STATUS "✅ 找到 iOS FFmpeg 静态库")
        include_directories(${FFMPEG_ROOT}/include)
        link_directories(${FFMPEG_ROOT}/lib)
        set(HAS_FFMPEG TRUE)
    else()
        message(FATAL_ERROR "❌ 未找到 iOS FFmpeg 静态库！请先运行 ios-third/build_ffmpeg_ios.sh 编译")
    endif()
    
    # 检查 SoundTouch 是否存在
    if(EXISTS "${SOUNDTOUCH_ROOT}/lib/libSoundTouch.a")
        message(STATUS "✅ 找到 iOS SoundTouch 静态库")
        include_directories(${SOUNDTOUCH_ROOT}/include)
        link_directories(${SOUNDTOUCH_ROOT}/lib)
        set(HAS_SOUNDTOUCH TRUE)
    else()
        message(WARNING "⚠️  未找到 iOS SoundTouch 静态库，倍速播放功能将被禁用")
        set(HAS_SOUNDTOUCH FALSE)
    endif()
else()
    # macOS: 配置将在后面根据自编译库是否存在来决定
    # 不在这里设置 include_directories 和 link_directories
endif()

# ==================== 创建 Framework ====================

add_library(HXCPlayer SHARED
    ${CORE_SOURCES}
    ${APPLE_SOURCES}
    ${HXCVD_SOURCES}
    ${PUBLIC_HEADERS}
    ${PRIVATE_HEADERS}
)

# 设置 Framework 属性
set_target_properties(HXCPlayer PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION A
    MACOSX_FRAMEWORK_IDENTIFIER com.hxc.HXCPlayer
    MACOSX_FRAMEWORK_BUNDLE_VERSION 1.0.0
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0.0
    VERSION ${PROJECT_VERSION}
    SOVERSION 1.0
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
    PRIVATE_HEADER "${PRIVATE_HEADERS}"
    MACOSX_FRAMEWORK_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    XCODE_ATTRIBUTE_DEFINES_MODULE YES
    XCODE_ATTRIBUTE_MODULEMAP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/module.modulemap"
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_BUILD_LIBRARY_FOR_DISTRIBUTION YES
    XCODE_ATTRIBUTE_INFOPLIST_PREPROCESS YES
    XCODE_ATTRIBUTE_PRODUCT_NAME "HXCPlayer"
)

# 为纯 C++ 文件设置编译标志（添加 NO_SDL 宏定义）
# 注意：这里不能在 COMPILE_FLAGS 中定义 HAS_SOUNDTOUCH，因为它会被覆盖
set_source_files_properties(
    ${CORE_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-x c++ -DNO_SDL"
)

# 为 Objective-C++ 文件设置编译标志
set_source_files_properties(
    ${APPLE_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
)

# HXCVDownload 为纯 Objective-C
set_source_files_properties(
    ${HXCVD_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-x objective-c -fobjc-arc"
)

# 定义编译宏（在 target 级别定义，避免被覆盖）
target_compile_definitions(HXCPlayer PRIVATE NO_SDL=1)
if(HAS_SOUNDTOUCH)
    target_compile_definitions(HXCPlayer PRIVATE HAS_SOUNDTOUCH=1)
    message(STATUS "✅ Framework 已启用 SoundTouch 支持")
else()
    message(STATUS "⚠️  Framework SoundTouch 未启用")
endif()

# ==================== 链接库 ====================

# 系统 Framework
target_link_libraries(HXCPlayer PUBLIC
    "-framework Foundation"
    "-framework AVFoundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework AudioToolbox"
    "-framework QuartzCore"
    "-framework CoreData"
)

if(IOS)
    target_link_libraries(HXCPlayer PUBLIC
        "-framework UIKit"
        "-framework AVKit"
    )
else()
    target_link_libraries(HXCPlayer PUBLIC
        "-framework AppKit"
    )
endif()

# FFmpeg 和 SoundTouch
if(IOS)
    # iOS: 静态链接
    target_link_libraries(HXCPlayer PUBLIC
        avcodec
        avformat
        avutil
        swscale
        swresample
    )
    
    # 如果有 SoundTouch，链接它
    if(HAS_SOUNDTOUCH)
        target_link_libraries(HXCPlayer PUBLIC SoundTouch)
    endif()
else()
    # macOS: 优先使用自编译的静态库，否则使用 Homebrew
    set(MACOS_FFMPEG_ROOT "${PROJECT_ROOT}/macos-third/ffmpeg-build-macos/FFmpeg-macOS")
    set(MACOS_SOUNDTOUCH_ROOT "${PROJECT_ROOT}/macos-third/soundtouch-build-macos/SoundTouch-macOS")
    
    if(EXISTS "${MACOS_FFMPEG_ROOT}/lib/libavcodec.a")
        message(STATUS "使用自编译的 macOS FFmpeg 静态库")
        include_directories(${MACOS_FFMPEG_ROOT}/include)
        
        # 使用自编译的 FFmpeg 静态库
        target_link_libraries(HXCPlayer PUBLIC
            ${MACOS_FFMPEG_ROOT}/lib/libavcodec.a
            ${MACOS_FFMPEG_ROOT}/lib/libavformat.a
            ${MACOS_FFMPEG_ROOT}/lib/libavutil.a
            ${MACOS_FFMPEG_ROOT}/lib/libswscale.a
            ${MACOS_FFMPEG_ROOT}/lib/libswresample.a
        )
    else()
        message(STATUS "使用 Homebrew FFmpeg（请运行 macos-third/build_ffmpeg_macos.sh 编译静态库）")
        # 使用 Homebrew 的头文件和库
        include_directories(/opt/homebrew/include)
        link_directories(/opt/homebrew/lib)
        
        target_link_libraries(HXCPlayer PUBLIC
            avcodec
            avformat
            avutil
            swscale
            swresample
        )
    endif()
    
    if(EXISTS "${MACOS_SOUNDTOUCH_ROOT}/lib/libSoundTouch.a")
        message(STATUS "使用自编译的 macOS SoundTouch 静态库")
        include_directories(${MACOS_SOUNDTOUCH_ROOT}/include)
        
        target_link_libraries(HXCPlayer PUBLIC
            ${MACOS_SOUNDTOUCH_ROOT}/lib/libSoundTouch.a
        )
        set(HAS_SOUNDTOUCH TRUE)
    else()
        message(STATUS "使用 Homebrew SoundTouch（请运行 macos-third/build_soundtouch_macos.sh 编译静态库）")
        # 使用 Homebrew 的头文件和库
        include_directories(/opt/homebrew/include)
        link_directories(/opt/homebrew/lib)
        
        # 检查 Homebrew 是否有 SoundTouch
        if(EXISTS "/opt/homebrew/include/soundtouch/SoundTouch.h")
            target_link_libraries(HXCPlayer PUBLIC SoundTouch)
            set(HAS_SOUNDTOUCH TRUE)
        else()
            message(WARNING "⚠️  未找到 SoundTouch，倍速播放功能将被禁用")
            set(HAS_SOUNDTOUCH FALSE)
        endif()
    endif()
    
    # macOS FFmpeg 静态库可能需要的额外系统框架
    target_link_libraries(HXCPlayer PUBLIC
        "-framework CoreFoundation"
        "-framework CoreText"
        "-framework CoreImage"
    )
endif()

# FFmpeg 依赖的系统库
target_link_libraries(HXCPlayer PUBLIC
    "-framework VideoToolbox"
    "-framework CoreGraphics"
    "-framework Security"
    "-framework CFNetwork"
    "-lz"
    "-lbz2"
    "-liconv"
)

# ==================== 安装配置 ====================

install(TARGETS HXCPlayer
    FRAMEWORK DESTINATION .
    PUBLIC_HEADER DESTINATION Headers
    PRIVATE_HEADER DESTINATION PrivateHeaders
)

# 安装 modulemap 文件
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/module.modulemap
    DESTINATION HXCPlayer.framework/Modules
)

# ==================== 输出信息 ====================

message(STATUS "")
message(STATUS "HXCPlayer Framework 配置完成:")
message(STATUS "  平台: ${PLATFORM}")
message(STATUS "  版本: ${PROJECT_VERSION}")
message(STATUS "  公共头文件: ${PUBLIC_HEADERS}")
message(STATUS "")
message(STATUS "构建命令:")
message(STATUS "  cmake -G Xcode -DIOS=ON ..")
message(STATUS "  xcodebuild -scheme HXCPlayer")
message(STATUS "")
