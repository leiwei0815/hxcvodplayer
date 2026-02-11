set(CORE_SOURCES
    player_types.cpp
    packet_queue.cpp
    decoder.cpp
    player_core.cpp
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
