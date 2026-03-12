cmake_minimum_required(VERSION 3.22.1)
project(hxcplayer)

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 项目根目录（从 android/hxcplayer/src/main/cpp 向上 5 级到项目根）
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

# 第三方库路径
set(ANDROID_THIRD_ROOT "${PROJECT_ROOT}/android-third")
set(MBEDTLS_ROOT "${ANDROID_THIRD_ROOT}/mbedtls-build-android/mbedTLS-Android/${ANDROID_ABI}")
set(FFMPEG_ROOT "${ANDROID_THIRD_ROOT}/ffmpeg-build-android/FFmpeg-Android/${ANDROID_ABI}")
set(SOUNDTOUCH_ROOT "${ANDROID_THIRD_ROOT}/soundtouch-build-android/SoundTouch-Android/${ANDROID_ABI}")

message(STATUS "========================================")
message(STATUS "HXCPlayer Android Native 构建配置")
message(STATUS "========================================")
message(STATUS "项目根目录: ${PROJECT_ROOT}")
message(STATUS "Android ABI: ${ANDROID_ABI}")
message(STATUS "mbedTLS 路径: ${MBEDTLS_ROOT}")
message(STATUS "FFmpeg 路径: ${FFMPEG_ROOT}")
message(STATUS "SoundTouch 路径: ${SOUNDTOUCH_ROOT}")
message(STATUS "========================================")

# 核心库源文件
set(CORE_SOURCES
    ${PROJECT_ROOT}/core/src/hxc_player_core.cpp
    ${PROJECT_ROOT}/core/src/hxc_decoder.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_types.cpp
    ${PROJECT_ROOT}/core/src/hxc_packet_queue.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_core_c_bridge.cpp
)

# JNI 桥接源文件
set(JNI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/hxcplayer_jni.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android_player.cpp
)

# 包含目录
include_directories(
    ${PROJECT_ROOT}/core/include
    ${MBEDTLS_ROOT}/include
    ${FFMPEG_ROOT}/include
    ${SOUNDTOUCH_ROOT}/include
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# 创建共享库
add_library(hxcplayer SHARED
    ${CORE_SOURCES}
    ${JNI_SOURCES}
)

# 为纯 C++ 文件设置编译标志
set_source_files_properties(
    ${CORE_SOURCES}
    PROPERTIES
    COMPILE_FLAGS "-DNO_SDL"
)

# 查找所需的系统库
find_library(log-lib log)
find_library(android-lib android)
find_library(mediandk-lib mediandk)

# 导入 FFmpeg 动态库
add_library(avcodec SHARED IMPORTED)
set_target_properties(avcodec PROPERTIES IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/libavcodec.so)

add_library(avformat SHARED IMPORTED)
set_target_properties(avformat PROPERTIES IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/libavformat.so)

add_library(avutil SHARED IMPORTED)
set_target_properties(avutil PROPERTIES IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/libavutil.so)

add_library(swscale SHARED IMPORTED)
set_target_properties(swscale PROPERTIES IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/libswscale.so)

add_library(swresample SHARED IMPORTED)
set_target_properties(swresample PROPERTIES IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/libswresample.so)

# 导入 SoundTouch 动态库
add_library(soundtouch SHARED IMPORTED)
set_target_properties(soundtouch PROPERTIES IMPORTED_LOCATION ${SOUNDTOUCH_ROOT}/lib/libSoundTouch.so)

# 导入 mbedTLS 动态库
add_library(mbedtls SHARED IMPORTED)
set_target_properties(mbedtls PROPERTIES IMPORTED_LOCATION ${MBEDTLS_ROOT}/lib/libmbedtls.so)

add_library(mbedx509 SHARED IMPORTED)
set_target_properties(mbedx509 PROPERTIES IMPORTED_LOCATION ${MBEDTLS_ROOT}/lib/libmbedx509.so)

add_library(mbedcrypto SHARED IMPORTED)
set_target_properties(mbedcrypto PROPERTIES IMPORTED_LOCATION ${MBEDTLS_ROOT}/lib/libmbedcrypto.so)

# 链接库（顺序很重要！）
target_link_libraries(hxcplayer
    # FFmpeg 库（按依赖顺序）
    avformat
    avcodec
    swresample
    swscale
    avutil
    
    # mbedTLS 库（FFmpeg HTTPS 依赖）
    mbedtls
    mbedx509
    mbedcrypto
    
    # SoundTouch 库
    soundtouch
    
    # Android 系统库
    ${log-lib}
    ${android-lib}
    ${mediandk-lib}
    
    # OpenSL ES (音频输出)
    OpenSLES
    
    # C++ 标准库
    c++_shared
    
    # 系统库（FFmpeg 依赖）
    z
    m
)

