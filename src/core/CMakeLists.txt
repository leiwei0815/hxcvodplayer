set(CORE_SOURCES
    hxc_player_types.cpp
    hxc_packet_queue.cpp
    hxc_decoder.cpp
    hxc_player_core.cpp
)

add_library(hxcplayer_core STATIC ${CORE_SOURCES})

target_include_directories(hxcplayer_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
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
endif()
