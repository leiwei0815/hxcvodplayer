# ========================================
# HXCPlayer Windows SDK 模块
# 提供 C API 和 D3D11/OpenGL 渲染器
# ========================================

# 渲染器源文件
set(RENDERER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/renderers/d3d11_video_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/renderers/renderer_factory.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/renderers/opengl_video_renderer.cpp  # TODO
)

set(RENDERER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/renderers/video_renderer_interface.h
    ${CMAKE_CURRENT_SOURCE_DIR}/renderers/d3d11_video_renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/renderers/renderer_factory.h
    # ${CMAKE_CURRENT_SOURCE_DIR}/renderers/opengl_video_renderer.h  # TODO
)

# Windows 播放器视图管理
set(PLAYER_VIEW_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/windows_player_view.cpp
)

set(PLAYER_VIEW_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/windows_player_view.h
)

# SDK C API
set(SDK_API_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/hxcplayer_sdk_c_api.cpp
)

set(SDK_API_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/hxcplayer_sdk_c_api.h
)

# ========================================
# 确定库类型（支持 DLL 发布）
# ========================================
# 默认：静态库（用于 desktop 内部开发）
# 如果指定 -DBUILD_SHARED_LIBS=ON，则构建动态库（用于 SDK 发布）
if(BUILD_SHARED_LIBS)
    set(SDK_LIB_TYPE SHARED)
    message(STATUS "HXCPlayer SDK: 构建动态库 (DLL) 用于发布")
else()
    set(SDK_LIB_TYPE STATIC)
    message(STATUS "HXCPlayer SDK: 构建静态库用于内部开发")
endif()

# 创建 Windows SDK 库
add_library(hxcplayer_sdk ${SDK_LIB_TYPE}
    ${RENDERER_SOURCES}
    ${RENDERER_HEADERS}
    ${PLAYER_VIEW_SOURCES}
    ${PLAYER_VIEW_HEADERS}
    ${SDK_API_SOURCES}
    ${SDK_API_HEADERS}
)

# 头文件路径
target_include_directories(hxcplayer_sdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/renderers
    ${CMAKE_SOURCE_DIR}/core/include
)

# 链接 Core 库
target_link_libraries(hxcplayer_sdk PUBLIC
    hxcplayer_core
)

# Windows D3D11 库
if(WIN32)
    target_link_libraries(hxcplayer_sdk PUBLIC
        d3d11.lib
        dxgi.lib
        d3dcompiler.lib
    )
endif()

# C++17 支持
target_compile_features(hxcplayer_sdk PUBLIC cxx_std_17)

# Windows UTF-8 编译
if(WIN32)
    target_compile_options(hxcplayer_sdk PRIVATE /utf-8)
endif()

# ========================================
# 定义导出宏
# ========================================
if(BUILD_SHARED_LIBS)
    # 构建 DLL 时定义导出宏
    target_compile_definitions(hxcplayer_sdk PRIVATE HXCPLAYER_SDK_EXPORTS)
    message(STATUS "  SDK 导出宏: HXCPLAYER_SDK_EXPORTS")
else()
    # 静态库：定义 HXCPLAYER_SDK_STATIC 让头文件不使用 __declspec
    target_compile_definitions(hxcplayer_sdk PUBLIC HXCPLAYER_SDK_STATIC)
    message(STATUS "  SDK 静态库标志: HXCPLAYER_SDK_STATIC")
endif()

message(STATUS "✅ HXCPlayer SDK: D3D11 渲染器已配置")

# ========================================
# SDK 打包目标（仅在构建 DLL 时启用）
# ========================================
if(BUILD_SHARED_LIBS)
    # 确定配置类型
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(LOCAL_BUILD_TYPE "debug")
    else()
        set(LOCAL_BUILD_TYPE "release")
    endif()
    
    # 本地库目录
    set(WIN_THIRD_DIR "${CMAKE_SOURCE_DIR}/win-third")
    set(FFMPEG_BIN_DIR "${WIN_THIRD_DIR}/ffmpeg-install/bin")
    set(SDL2_BIN_DIR "${WIN_THIRD_DIR}/sdl2-install/${LOCAL_BUILD_TYPE}/bin")
    set(SOUNDTOUCH_BIN_DIR "${WIN_THIRD_DIR}/soundtouch-install/${LOCAL_BUILD_TYPE}/bin")
    
    # SDK 输出目录
    set(SDK_OUTPUT_DIR "${CMAKE_BINARY_DIR}/HXCPlayerSDK")
    
    add_custom_target(package_sdk
        # 1. 创建目录结构
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SDK_OUTPUT_DIR}/include
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SDK_OUTPUT_DIR}/lib
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SDK_OUTPUT_DIR}/bin
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SDK_OUTPUT_DIR}/docs
        
        # 2. 复制头文件
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_CURRENT_SOURCE_DIR}/hxcplayer_sdk_c_api.h
            ${SDK_OUTPUT_DIR}/include/
        
        # 3. 复制 SDK DLL 和导入库
        COMMAND ${CMAKE_COMMAND} -E copy 
            $<TARGET_FILE:hxcplayer_sdk>
            ${SDK_OUTPUT_DIR}/bin/
        COMMAND ${CMAKE_COMMAND} -E copy 
            $<TARGET_LINKER_FILE:hxcplayer_sdk>
            ${SDK_OUTPUT_DIR}/lib/
        
        # 4. 复制 Core DLL（如果是共享库）
        COMMAND ${CMAKE_COMMAND} -E copy 
            $<TARGET_FILE:hxcplayer_core>
            ${SDK_OUTPUT_DIR}/bin/
        
        # 5. 复制本地编译的第三方 DLL
        COMMAND ${CMAKE_COMMAND} -E echo "正在复制本地编译的第三方依赖 DLL..."
        
        # FFmpeg DLL（从 win-third/ffmpeg-install/bin）
        COMMAND ${CMAKE_COMMAND} -E echo "  - FFmpeg DLLs..."
        COMMAND powershell -Command "if (Test-Path '${FFMPEG_BIN_DIR}') { Get-ChildItem '${FFMPEG_BIN_DIR}' -Filter '*.dll' | Copy-Item -Destination '${SDK_OUTPUT_DIR}/bin' -Force }"
        
        # SDL2 DLL（从 win-third/sdl2-install/{debug|release}/bin）
        COMMAND ${CMAKE_COMMAND} -E echo "  - SDL2 DLL..."
        COMMAND powershell -Command "if (Test-Path '${SDL2_BIN_DIR}') { Get-ChildItem '${SDL2_BIN_DIR}' -Filter 'SDL2.dll' | Copy-Item -Destination '${SDK_OUTPUT_DIR}/bin' -Force }"
        
        # SoundTouch DLL（从 win-third/soundtouch-install/{debug|release}/bin，可选）
        COMMAND ${CMAKE_COMMAND} -E echo "  - SoundTouch DLL (if exists)..."
        COMMAND powershell -Command "if (Test-Path '${SOUNDTOUCH_BIN_DIR}') { Get-ChildItem '${SOUNDTOUCH_BIN_DIR}' -Filter '*.dll' | Copy-Item -Destination '${SDK_OUTPUT_DIR}/bin' -Force -ErrorAction SilentlyContinue }"
        
        # 6. 复制文档和示例（如果存在）
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/README.md
            ${SDK_OUTPUT_DIR}/
        
        # 7. 生成使用说明
        COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
        COMMAND ${CMAKE_COMMAND} -E echo "SDK 打包完成！"
        COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
        COMMAND ${CMAKE_COMMAND} -E echo "输出目录: ${SDK_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "目录结构:"
        COMMAND ${CMAKE_COMMAND} -E echo "  HXCPlayerSDK/"
        COMMAND ${CMAKE_COMMAND} -E echo "    ├── include/       (头文件)"
        COMMAND ${CMAKE_COMMAND} -E echo "    ├── lib/           (导入库)"
        COMMAND ${CMAKE_COMMAND} -E echo "    ├── bin/           (DLL 文件)"
        COMMAND ${CMAKE_COMMAND} -E echo "    └── README.md"
        COMMAND ${CMAKE_COMMAND} -E echo ""
        
        DEPENDS hxcplayer_sdk hxcplayer_core
        COMMENT "打包 HXCPlayer SDK..."
    )
    
    message(STATUS "✅ SDK 打包目标已配置")
    message(STATUS "  使用: cmake --build . --config Release --target package_sdk")
    message(STATUS "  第三方库路径:")
    message(STATUS "    - FFmpeg: ${FFMPEG_BIN_DIR}")
    message(STATUS "    - SDL2:   ${SDL2_BIN_DIR}")
    message(STATUS "    - SoundTouch: ${SOUNDTOUCH_BIN_DIR}")
endif()
