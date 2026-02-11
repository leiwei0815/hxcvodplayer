set(PLATFORM_SOURCES
    platform_factory.cpp
)

add_library(yxplayer_platform STATIC ${PLATFORM_SOURCES})

target_include_directories(yxplayer_platform PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(yxplayer_platform PUBLIC
    yxplayer_core
)
