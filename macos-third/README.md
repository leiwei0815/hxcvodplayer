# macOS 第三方库编译说明

本目录包含 macOS 平台 FFmpeg 和 SoundTouch 静态库的编译脚本。

## 📋 前置要求

- macOS 系统（Apple Silicon 推荐）
- Xcode Command Line Tools
- autoconf, automake, libtool (SoundTouch 需要)

安装前置工具：
```bash
xcode-select --install
brew install autoconf automake libtool
```

## 🚀 快速开始

### 方式 1: 一键编译所有库
```bash
cd macos-third
./build_all.sh
```

### 方式 2: 分别编译

#### 编译 FFmpeg
```bash
cd macos-third
./build_ffmpeg_macos.sh
```

#### 编译 SoundTouch
```bash
cd macos-third
./build_soundtouch_macos.sh
```

## 📦 输出目录

编译完成后，静态库将位于：

- **FFmpeg**: `macos-third/ffmpeg-build-macos/FFmpeg-macOS/`
  - 库文件: `lib/libavcodec.a`, `lib/libavformat.a` 等
  - 头文件: `include/libavcodec/`, `include/libavformat/` 等

- **SoundTouch**: `macos-third/soundtouch-build-macos/SoundTouch-macOS/`
  - 库文件: `lib/libSoundTouch.a`
  - 头文件: `include/soundtouch/`

## 🔨 构建 XCFramework

编译完成后，运行：
```bash
cd ../apple
./build_xcframework_simple.sh
```

CMakeLists.txt 会自动检测并使用编译的静态库。

## ⚙️ FFmpeg 配置说明

当前 FFmpeg 编译配置为**最小化配置**，仅包含：

### 支持的编解码器
- **视频解码**: H.264, HEVC, MPEG-4
- **音频解码**: AAC, MP3, PCM
- **图片解码**: PNG, MJPEG

### 支持的格式
- **容器**: MP4, MOV, MKV, AVI, FLV, TS
- **音频**: AAC, MP3, WAV

### 硬件加速
- VideoToolbox 硬件解码（H.264, HEVC）

### 不包含的功能
- 编码器（所有）
- 网络协议（除 HTTP/HTTPS）
- 设备输入/输出
- 滤镜（除基本的 scale/format）
- 第三方编解码器库（x264, x265 等）

## 📝 自定义配置

如果需要修改 FFmpeg 配置（例如添加更多解码器），编辑 `build_ffmpeg_macos.sh` 中的 `./configure` 参数。

常用选项：
- 添加 VP9 解码: `--enable-decoder=vp9`
- 添加 Opus 音频: `--enable-decoder=opus --enable-demuxer=ogg`
- 启用所有解码器: 移除 `--disable-decoders` 并删除各个 `--enable-decoder`

## 🐛 故障排查

### 下载失败
如果网络问题导致下载失败，可以手动下载：
- FFmpeg: https://ffmpeg.org/releases/
- SoundTouch: https://codeberg.org/soundtouch/soundtouch/releases

下载后放到对应目录并解压。

### 编译错误
- 确保已安装 Xcode Command Line Tools
- 确保已安装 autoconf、automake、libtool
- 检查磁盘空间（FFmpeg 编译需要约 2GB）

### 清理重新编译
```bash
rm -rf ffmpeg-build-macos soundtouch-build-macos
./build_all.sh
```

## 📊 预期编译时间

- FFmpeg: 约 5-15 分钟（取决于 CPU）
- SoundTouch: 约 1-2 分钟

## 📈 库文件大小

编译后的静态库大小（大约）：
- `libavcodec.a`: ~15-20 MB
- `libavformat.a`: ~3-5 MB
- `libavutil.a`: ~1-2 MB
- `libswscale.a`: ~500 KB
- `libswresample.a`: ~200 KB
- `libSoundTouch.a`: ~200 KB

**总计**: 约 20-30 MB（最小化配置）

最终的 HXCPlayer.xcframework (macOS) 大小约为 **25-35 MB**。

## 🔗 相关文档

- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [SoundTouch 文档](https://www.surina.net/soundtouch/)
- [FFmpeg 编译选项](https://ffmpeg.org/ffmpeg-all.html#Options)
