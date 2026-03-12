# 源文件
set(CORE_SOURCES
    hxc_player_types.cpp
    hxc_packet_queue.cpp
    hxc_decoder.cpp
    hxc_player_core.cpp
    hxc_player_core_c_bridge.cpp
)

# 头文件
set(CORE_HEADERS
    ${CMAKE_SOURCE_DIR}/core/include/hxc_player_types.h
    ${CMAKE_SOURCE_DIR}/core/include/hxc_packet_queue.h
    ${CMAKE_SOURCE_DIR}/core/include/hxc_frame_queue.h
    ${CMAKE_SOURCE_DIR}/core/include/hxc_decoder.h
    ${CMAKE_SOURCE_DIR}/core/include/hxc_player_core.h
    ${CMAKE_SOURCE_DIR}/core/include/hxc_player_core_c_bridge.h
    ${CMAKE_SOURCE_DIR}/core/include/hxc_logger.h
)

# 创建静态库（包含源文件和头文件）
add_library(hxcplayer_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})

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
    target_include_directories(hxcplayer_core PUBLIC ${SOUNDTOUCH_INCLUDE_DIR})
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
    
    # Windows 平台编译选项
    target_compile_options(hxcplayer_core PUBLIC
        /utf-8      # 源文件和执行字符集都使用 UTF-8（PUBLIC 以便传递给依赖项目）
    )
    
    # Windows 平台预处理器定义
    target_compile_definitions(hxcplayer_core PUBLIC
        NOMINMAX    # 禁用 Windows 的 min/max 宏，避免与 std::min/std::max 冲突
        WIN32_LEAN_AND_MEAN  # 减少 Windows 头文件的包含，加快编译速度
    )
endif()
