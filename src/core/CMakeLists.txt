set(CORE_SOURCES
    yx_player_types.cpp
    yx_packet_queue.cpp
    yx_decoder.cpp
    yx_player_core.cpp
)

add_library(yxplayer_core STATIC ${CORE_SOURCES})

target_include_directories(yxplayer_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(yxplayer_core PUBLIC
    ${FFMPEG_LIBRARIES}
    ${SDL2_LIBRARIES}
)

# SoundTouch（倍速播放）
if(SOUNDTOUCH_FOUND)
    target_link_libraries(yxplayer_core PUBLIC ${SOUNDTOUCH_LIBRARIES})
    target_compile_definitions(yxplayer_core PUBLIC HAS_SOUNDTOUCH)
endif()

# 线程库
find_package(Threads REQUIRED)
target_link_libraries(yxplayer_core PUBLIC Threads::Threads)

# 平台特定链接
if(APPLE)
    target_link_libraries(yxplayer_core PUBLIC
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
endif()
