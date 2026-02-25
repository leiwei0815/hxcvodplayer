set(PLATFORM_SOURCES
    platform_factory.cpp
)

add_library(hxcplayer_platform STATIC ${PLATFORM_SOURCES})

target_include_directories(hxcplayer_platform PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(hxcplayer_platform PUBLIC
    hxcplayer_core
)
