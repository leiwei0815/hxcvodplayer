# YXVodPlayer 开发任务清单

## 核心功能 (Core)

### 已完成 ✅
- [x] 项目架构设计
- [x] 基础类型定义
- [x] PacketQueue 实现
- [x] FrameQueue 实现
- [x] Decoder 基类
- [x] PlayerCore 框架
- [x] Clock 时钟同步基础

### 待完成 🚧

#### 高优先级
- [ ] 完善 PlayerCore 实现
  - [ ] 音视频同步逻辑
  - [ ] Seek 功能实现
  - [ ] 播放控制完善
- [ ] 音频解码和播放
  - [ ] SDL 音频回调实现
  - [ ] 音频重采样
  - [ ] 音量控制
- [ ] 视频解码和显示
  - [ ] 视频解码线程
  - [ ] 帧时间戳计算
  - [ ] 丢帧策略
- [ ] 错误处理
  - [ ] 异常捕获
  - [ ] 错误恢复
  - [ ] 日志系统

#### 中优先级
- [ ] 性能优化
  - [ ] 内存池
  - [ ] 零拷贝优化
  - [ ] 多线程优化
- [ ] 功能扩展
  - [ ] 字幕支持
  - [ ] 多音轨支持
  - [ ] 播放速度控制
- [ ] 硬件加速
  - [ ] VAAPI (Linux)
  - [ ] VideoToolbox (macOS)
  - [ ] D3D11 (Windows)

#### 低优先级
- [ ] 高级功能
  - [ ] 截图功能
  - [ ] 录制功能
  - [ ] 滤镜支持
  - [ ] 播放列表

## 桌面版 (Desktop)

### 已完成 ✅
- [x] Qt5 项目结构
- [x] MainWindow UI 设计
- [x] VideoWidget 基础框架
- [x] SDL 渲染器框架
- [x] CMake 构建配置

### 待完成 🚧

#### 高优先级
- [ ] UI 完善
  - [ ] 控制条实现
  - [ ] 进度条拖动
  - [ ] 音量滑块
  - [ ] 全屏支持
- [ ] 渲染优化
  - [ ] 帧率控制
  - [ ] 缩放和裁剪
  - [ ] 色彩空间转换
- [ ] 功能实现
  - [ ] 文件打开对话框
  - [ ] 最近播放列表
  - [ ] 配置保存/加载

#### 中优先级
- [ ] 用户体验
  - [ ] 快捷键支持
  - [ ] 拖放文件
  - [ ] 播放历史
  - [ ] 收藏夹
- [ ] 界面优化
  - [ ] 主题支持
  - [ ] 布局自定义
  - [ ] 多语言支持

## Android 版

### 待完成 🚧

#### 高优先级
- [ ] 项目搭建
  - [ ] Android Studio 项目
  - [ ] Gradle 配置
  - [ ] JNI 集成
  - [ ] FFmpeg 库集成
- [ ] 核心功能
  - [ ] NativePlayer JNI 接口
  - [ ] PlayerActivity 实现
  - [ ] PlayerView (SurfaceView)
  - [ ] 播放控制器
- [ ] 平台集成
  - [ ] AndroidRenderer 实现
  - [ ] 音频播放 (AudioTrack)
  - [ ] 权限管理

#### 中优先级
- [ ] 硬件加速
  - [ ] MediaCodec 集成
  - [ ] OpenGL ES 渲染
- [ ] 用户体验
  - [ ] 手势控制
  - [ ] 后台播放
  - [ ] 画中画模式
  - [ ] 通知栏控制

#### 低优先级
- [ ] 高级功能
  - [ ] 离线下载
  - [ ] 弹幕支持
  - [ ] 倍速播放

## iOS 版

### 待完成 🚧

#### 高优先级
- [ ] 项目搭建
  - [ ] Xcode 项目
  - [ ] FFmpeg 库集成
  - [ ] Bridge 层实现
- [ ] 核心功能
  - [ ] PlayerBridge 实现
  - [ ] PlayerViewController
  - [ ] PlayerView (Metal)
  - [ ] 控制器 UI
- [ ] 平台集成
  - [ ] IOSRenderer 实现
  - [ ] 音频播放 (AVAudioEngine)
  - [ ] 后台播放

#### 中优先级
- [ ] 硬件加速
  - [ ] VideoToolbox 集成
  - [ ] Metal 渲染优化
- [ ] 用户体验
  - [ ] 手势控制
  - [ ] AirPlay 支持
  - [ ] 画中画模式
  - [ ] CarPlay 支持

## 测试

### 单元测试
- [ ] PacketQueue 测试
- [ ] FrameQueue 测试
- [ ] Decoder 测试
- [ ] Clock 测试
- [ ] PlayerCore 测试

### 集成测试
- [ ] 端到端播放测试
- [ ] 多格式兼容性测试
- [ ] 性能基准测试
- [ ] 内存泄漏测试

### 平台测试
- [ ] macOS 测试
- [ ] Windows 测试
- [ ] Linux 测试
- [ ] Android 测试
- [ ] iOS 测试

## 文档

### 已完成 ✅
- [x] README.md
- [x] ARCHITECTURE.md
- [x] BUILD.md
- [x] ANDROID.md
- [x] IOS.md
- [x] QUICKSTART.md

### 待完成 🚧
- [ ] API 文档
  - [ ] 核心 API 文档
  - [ ] 平台 API 文档
  - [ ] JNI 接口文档
- [ ] 开发指南
  - [ ] 贡献指南
  - [ ] 代码规范
  - [ ] 调试指南
- [ ] 用户手册
  - [ ] 使用教程
  - [ ] 常见问题
  - [ ] 故障排除

## 构建和发布

### 待完成 🚧
- [ ] CI/CD
  - [ ] GitHub Actions
  - [ ] 自动化测试
  - [ ] 自动化构建
- [ ] 打包
  - [ ] macOS DMG
  - [ ] Windows Installer
  - [ ] Linux AppImage/Snap
  - [ ] Android APK 签名
  - [ ] iOS IPA
- [ ] 发布
  - [ ] 版本管理
  - [ ] 更新日志
  - [ ] 发布说明

## 性能优化

- [ ] 启动时间优化
- [ ] 内存使用优化
- [ ] CPU 使用优化
- [ ] 电量消耗优化
- [ ] 网络流播放优化

## 已知问题

1. PlayerCore 音视频同步未完全实现
2. VideoWidget 渲染性能待优化
3. Android 和 iOS 项目尚未创建
4. 缺少完整的错误处理
5. 需要添加日志系统

## 里程碑

### v0.1.0 (MVP) - 预计 2 周
- [ ] 桌面版基本播放功能
- [ ] 支持常见格式 (MP4, MKV, AVI)
- [ ] 基本控制功能

### v0.2.0 - 预计 4 周
- [ ] 完善桌面版功能
- [ ] Android 基础版本
- [ ] iOS 基础版本

### v0.3.0 - 预计 6 周
- [ ] 硬件加速支持
- [ ] 性能优化
- [ ] 移动端功能完善

### v1.0.0 - 预计 8 周
- [ ] 所有核心功能完成
- [ ] 全平台支持
- [ ] 文档完善
- [ ] 正式发布

## 备注

- 优先完成桌面版，验证架构设计
- 移动端开发依赖桌面版核心稳定
- 持续关注性能和用户体验
- 定期更新文档
