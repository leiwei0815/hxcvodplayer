set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(DESKTOP_SOURCES
    main.cpp
    main_window.cpp
    video_widget.cpp
    sdl_renderer.cpp
    qt_platform_factory.cpp
)

set(DESKTOP_HEADERS
    main_window.h
    video_widget.h
    sdl_renderer.h
    qt_platform_factory.h
)

# UI 文件
set(DESKTOP_UI
    main_window.ui
)

add_executable(HXCVodPlayer
    ${DESKTOP_SOURCES}
    ${DESKTOP_HEADERS}
    ${DESKTOP_UI}
)

target_include_directories(HXCVodPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(HXCVodPlayer PRIVATE
    hxcplayer_core
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    ${SDL2_LIBRARIES}
)

# SoundTouch（倍速播放）
if(SOUNDTOUCH_FOUND)
    target_link_libraries(HXCVodPlayer PRIVATE ${SOUNDTOUCH_LIBRARIES})
endif()

# 设置输出目录
set_target_properties(HXCVodPlayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# macOS 特定设置
if(APPLE)
    set_target_properties(HXCVodPlayer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    )
endif()

# Windows 特定设置
if(WIN32)
    set_target_properties(HXCVodPlayer PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    
    # Windows 平台编译选项
    target_compile_options(HXCVodPlayer PRIVATE
        /utf-8           # 源文件和执行字符集都使用 UTF-8
    )
    
    # Windows 平台预处理器定义（从 core 继承了 PUBLIC 的定义，这里可选）
    # target_compile_definitions(HXCVodPlayer PRIVATE
    #     NOMINMAX
    #     WIN32_LEAN_AND_MEAN
    # )
endif()
