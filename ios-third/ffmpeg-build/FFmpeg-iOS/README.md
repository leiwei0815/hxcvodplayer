# FFmpeg for iOS

## 使用方法

### 1. 设置环境变量

```bash
export FFMPEG_IOS_DIR=/path/to/FFmpeg-iOS
```

### 2. 在 CMakeLists.txt 中配置

```cmake
set(FFMPEG_IOS_DIR $ENV{FFMPEG_IOS_DIR})

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    PATHS ${FFMPEG_IOS_DIR}/include
    NO_DEFAULT_PATH
)

find_library(AVCODEC_LIBRARY avcodec
    PATHS ${FFMPEG_IOS_DIR}/lib
    NO_DEFAULT_PATH
)

target_include_directories(YourTarget PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(YourTarget ${AVCODEC_LIBRARY})
```

### 3. 链接其他系统框架

FFmpeg 需要以下系统框架：

```cmake
target_link_libraries(YourTarget
    "-framework VideoToolbox"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework AVFoundation"
    "-framework AudioToolbox"
    "-framework CoreFoundation"
    "-framework Security"
    "z"
    "bz2"
    "iconv"
)
```

## 验证

```bash
# 检查架构
lipo -info lib/libavcodec.a

# 查看符号
nm lib/libavcodec.a | grep av_frame_alloc
```

## 库文件

- libavcodec.a - 编解码器
- libavformat.a - 封装/解封装
- libavutil.a - 工具函数
- libswresample.a - 音频重采样
- libswscale.a - 视频缩放/转换
- libavfilter.a - 音视频过滤器（如果启用）

