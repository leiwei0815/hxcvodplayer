# YXVodPlayer 项目总结

## 🎯 项目概述

YXVodPlayer 是一个基于 FFmpeg 和 ffplay 架构设计的跨平台视频播放器，使用 C++ 编写核心逻辑，支持以下平台：

- ✅ **Mac** - Qt5 + SDL2
- ✅ **Windows** - Qt5 + SDL2
- 🚧 **Android** - 原生 UI + JNI
- 🚧 **iOS** - 原生 UI + Objective-C++ Bridge

## 📁 项目结构

```
YXVodPlayer/
├── README.md                    # 项目说明
├── CMakeLists.txt              # 主 CMake 配置
├── build.sh                    # 构建脚本 ✅
├── .gitignore                  # Git 忽略文件
│
├── docs/                       # 文档目录
│   ├── QUICKSTART.md          # 快速入门 ✅
│   ├── ARCHITECTURE.md        # 架构设计 ✅
│   ├── BUILD.md               # 构建指南 ✅
│   ├── ANDROID.md             # Android 开发指南 ✅
│   └── IOS.md                 # iOS 开发指南 ✅
│
├── include/                    # 公共头文件
│   ├── player_types.h         # 核心类型定义 ✅
│   ├── player_core.h          # 播放器核心 ✅
│   ├── decoder.h              # 解码器 ✅
│   ├── packet_queue.h         # 数据包队列 ✅
│   ├── frame_queue.h          # 帧队列 ✅
│   └── platform_interface.h   # 平台抽象接口 ✅
│
├── src/
│   ├── core/                   # 核心播放器 (C++)
│   │   ├── CMakeLists.txt     ✅
│   │   ├── player_types.cpp   ✅
│   │   ├── decoder.cpp        ✅
│   │   ├── packet_queue.cpp   ✅
│   │   └── player_core.cpp    ✅ (基础框架)
│   │
│   ├── platform/               # 平台抽象层
│   │   ├── CMakeLists.txt     ✅
│   │   └── platform_factory.cpp ✅
│   │
│   ├── desktop/                # 桌面版 (Qt5)
│   │   ├── CMakeLists.txt     ✅
│   │   ├── main.cpp           ✅
│   │   ├── main_window.h/cpp  ✅
│   │   ├── main_window.ui     ✅
│   │   ├── video_widget.h/cpp ✅
│   │   ├── sdl_renderer.h/cpp ✅
│   │   └── qt_platform_factory.h/cpp ✅
│   │
│   ├── android/                # Android 版 (待实现)
│   │   └── (结构已设计，代码待创建)
│   │
│   └── ios/                    # iOS 版 (待实现)
│       └── (结构已设计，代码待创建)
│
├── scripts/                    # 脚本工具
│   └── setup_dev.sh           # 开发环境设置 ✅
│
└── TODO.md                     # 任务清单 ✅
```

## 🎨 架构设计

### 三层架构

```
┌─────────────────────────────────────────┐
│         UI Layer (平台相关)              │
│  Qt5 (Desktop) | Android | iOS          │
└─────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────┐
│    Platform Abstraction Layer           │
│  IVideoRenderer | IAudioRenderer | ...  │
└─────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────┐
│       Core Player Engine (C++)          │
│  PlayerCore | Decoder | Queue | Clock   │
└─────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────┐
│        FFmpeg | SDL2 | 系统框架         │
└─────────────────────────────────────────┘
```

### 核心组件

1. **PlayerCore** - 播放器核心控制
   - 媒体文件打开/关闭
   - 播放控制（播放/暂停/停止/跳转）
   - 音视频同步
   - 状态管理

2. **Decoder** - 解码器
   - VideoDecoder: 视频解码
   - AudioDecoder: 音频解码
   - 多线程解码

3. **PacketQueue** - 数据包队列
   - 线程安全
   - 自动内存管理
   - 阻塞/非阻塞操作

4. **FrameQueue** - 帧队列
   - 参照 ffplay 实现
   - 环形缓冲区
   - 高效的帧管理

5. **Clock** - 时钟同步
   - AudioMaster 模式
   - VideoMaster 模式
   - ExternalClock 模式

## 🛠️ 技术栈

### 核心依赖
- **FFmpeg 8.x** - 音视频编解码
- **SDL2** - 音频输出 (桌面版)
- **C++17** - 编程语言

### 桌面端
- **Qt5** - UI 框架
- **CMake** - 构建系统

### Android (计划)
- **Java/Kotlin** - UI 层
- **JNI** - 桥接层
- **MediaCodec** - 硬件解码
- **SurfaceView** - 视频渲染
- **AudioTrack** - 音频播放

### iOS (计划)
- **Objective-C/Swift** - UI 层
- **Objective-C++** - 桥接层
- **VideoToolbox** - 硬件解码
- **Metal** - 视频渲染
- **AVAudioEngine** - 音频播放

## 🚀 快速开始

### 1. 克隆项目

```bash
cd /Users/debug/project/YXVodPlayer
```

### 2. 安装依赖 (macOS)

```bash
# 使用自动化脚本
./scripts/setup_dev.sh

# 或手动安装
brew install cmake ffmpeg sdl2 qt@5
export Qt5_DIR=/usr/local/opt/qt@5/lib/cmake/Qt5
```

### 3. 构建

```bash
# 构建桌面版
./build.sh desktop release

# 运行
./build/desktop_release/bin/YXVodPlayer
```

## 📚 文档

### 入门文档
- [快速入门](docs/QUICKSTART.md) - 5 分钟快速体验
- [构建指南](docs/BUILD.md) - 详细的构建说明

### 架构文档
- [架构设计](docs/ARCHITECTURE.md) - 系统架构和设计思路

### 平台文档
- [Android 开发](docs/ANDROID.md) - Android 版本开发指南
- [iOS 开发](docs/IOS.md) - iOS 版本开发指南

## ✅ 已完成功能

### 核心层
- ✅ 项目架构设计
- ✅ 基础类型定义
- ✅ PacketQueue 完整实现
- ✅ FrameQueue 完整实现
- ✅ Decoder 基类和框架
- ✅ PlayerCore 基础框架
- ✅ Clock 时钟同步基础

### 桌面版
- ✅ Qt5 项目结构
- ✅ MainWindow UI 设计
- ✅ VideoWidget 框架
- ✅ SDL 渲染器框架
- ✅ 播放控制 UI
- ✅ CMake 构建系统

### 文档和工具
- ✅ 完整的架构文档
- ✅ 详细的构建指南
- ✅ 平台开发指南
- ✅ 快速入门教程
- ✅ 构建脚本
- ✅ 开发环境设置脚本

## 🚧 待实现功能

### 高优先级
1. **PlayerCore 核心逻辑**
   - 音视频同步实现
   - Seek 功能
   - 错误处理

2. **音频播放**
   - SDL 音频回调
   - 音频重采样
   - 音量控制

3. **视频渲染**
   - 视频解码线程
   - 帧显示同步
   - 渲染优化

### 中优先级
1. **桌面版完善**
   - 全屏支持
   - 快捷键
   - 播放列表

2. **Android 版本**
   - 创建项目
   - JNI 集成
   - 基础播放功能

3. **iOS 版本**
   - 创建项目
   - Bridge 实现
   - 基础播放功能

### 低优先级
1. 硬件加速
2. 字幕支持
3. 滤镜效果
4. 录制功能

详见 [TODO.md](TODO.md)

## 🎯 开发路线图

### Phase 1: 桌面版 MVP (当前)
- 核心播放功能
- 基本 UI 控制
- 支持常见格式

**预计时间**: 2 周

### Phase 2: 功能完善
- 音视频同步优化
- 性能优化
- 用户体验提升

**预计时间**: 2 周

### Phase 3: 移动端支持
- Android 基础版本
- iOS 基础版本
- 跨平台验证

**预计时间**: 4 周

### Phase 4: 正式发布
- 所有平台稳定
- 完整文档
- v1.0.0 发布

**预计时间**: 2 周

## 📝 代码规范

### C++ 代码风格
- 使用 C++17 标准
- 智能指针管理内存
- RAII 原则
- 命名规范:
  - 类名: `PascalCase`
  - 函数: `snake_case`
  - 成员变量: `snake_case_` (带下划线后缀)
  - 常量: `UPPER_CASE`

### 注释规范
- 文件头部使用 Doxygen 格式
- 公共 API 必须有注释
- 复杂逻辑添加说明

### Git 提交规范
- feat: 新功能
- fix: 修复 bug
- docs: 文档更新
- refactor: 代码重构
- test: 测试相关
- chore: 构建/工具更新

## 🤝 贡献指南

1. Fork 项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'feat: Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

## 📄 许可证

MIT License

## 🔗 相关链接

- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [ffplay 源代码](https://github.com/FFmpeg/FFmpeg/blob/master/fftools/ffplay.c)
- [SDL2 文档](https://wiki.libsdl.org/)
- [Qt 文档](https://doc.qt.io/)

## 📧 联系方式

项目地址: `/Users/debug/project/YXVodPlayer`

---

**注意**: 这是一个正在积极开发中的项目，核心功能已经搭建完成，但仍需要完善实现细节和移动端支持。欢迎贡献代码和提出建议！
