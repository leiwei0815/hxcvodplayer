set(CORE_SOURCES
    hxc_player_types.cpp
    hxc_packet_queue.cpp
    hxc_decoder.cpp
    hxc_player_core.cpp
    hxc_player_core_c_bridge.cpp
)

add_library(hxcplayer_core STATIC ${CORE_SOURCES})

target_include_directories(hxcplayer_core PUBLIC
    ${CMAKE_SOURCE_DIR}/core/include
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(hxcplayer_core PUBLIC
    ${FFMPEG_LIBRARIES}
    ${SDL2_LIBRARIES}
)

# SoundTouch（倍速播放）
if(SOUNDTOUCH_FOUND)
    target_link_libraries(hxcplayer_core PUBLIC ${SOUNDTOUCH_LIBRARIES})
    target_compile_definitions(hxcplayer_core PUBLIC HAS_SOUNDTOUCH)
endif()

# 线程库
find_package(Threads REQUIRED)
target_link_libraries(hxcplayer_core PUBLIC Threads::Threads)

# 平台特定链接
if(APPLE)
    target_link_libraries(hxcplayer_core PUBLIC
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
elseif(WIN32)
    # Windows 平台需要链接的系统库
    target_link_libraries(hxcplayer_core PUBLIC
        ws2_32      # Windows Sockets
        secur32     # Security library
        bcrypt      # Cryptography library
        mfplat      # Media Foundation Platform
        mfuuid      # Media Foundation UUIDs
        strmiids    # Streaming Media Interfaces
    )
    
    # 添加 UTF-8 编码支持
    target_compile_options(hxcplayer_core PRIVATE
        /utf-8      # 源文件和执行字符集都使用 UTF-8
    )
endif()
