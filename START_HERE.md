# 🎬 从这里开始！

## 桌面端播放器已完成并优化

### ✅ 当前状态

- ✅ 核心播放引擎完成
- ✅ Qt5 桌面版完成
- ✅ **Xcode 调试环境配置完成**
- ✅ 完整的文档系统
- ✅ 测试工具就绪

## 🚀 快速开始（选择一种方式）

### 方式 1: 使用 Xcode 调试（推荐）

```bash
cd /Users/debug/project/YXVodPlayer
./scripts/open_xcode.sh
```

然后在 Xcode 中按 **⌘R** 运行

📖 详细指南：[HOW_TO_DEBUG_IN_XCODE.md](HOW_TO_DEBUG_IN_XCODE.md)

### 方式 2: 命令行运行

```bash
# 构建（如果还没构建）
./build.sh desktop release

# 运行
open build/desktop_release/bin/YXVodPlayer.app
```

### 方式 3: 快速测试

```bash
./scripts/quick_test.sh
```

## 📚 文档导航

### 新手必读
1. **[HOW_TO_DEBUG_IN_XCODE.md](HOW_TO_DEBUG_IN_XCODE.md)** ⭐⭐⭐
   - 最详细的 Xcode 调试教程
   - 30 秒快速开始
   - 常见问题解答

2. **[USER_GUIDE.md](USER_GUIDE.md)**
   - 用户使用手册
   - 基本操作说明

3. **[TEST_GUIDE.md](TEST_GUIDE.md)**
   - 测试指南
   - 如何验证功能

### Xcode 相关
1. **[XCODE_QUICKSTART.md](XCODE_QUICKSTART.md)**
   - Xcode 快速入门
   - 基本配置

2. **[XCODE_GUIDE.md](XCODE_GUIDE.md)**
   - 完整的 Xcode 使用指南
   - 高级调试技巧

3. **[XCODE_OPTIMIZATION_SUMMARY.md](XCODE_OPTIMIZATION_SUMMARY.md)**
   - 优化总结
   - 调试工作流

### 技术文档
1. **[QUICKSTART.md](docs/QUICKSTART.md)**
   - 5 分钟快速入门

2. **[ARCHITECTURE.md](docs/ARCHITECTURE.md)**
   - 架构设计文档

3. **[BUILD.md](docs/BUILD.md)**
   - 详细构建指南

### 项目状态
1. **[IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md)**
   - 详细实现状态

2. **[COMPLETION_REPORT.md](COMPLETION_REPORT.md)**
   - 项目完成报告

3. **[PROJECT_SUMMARY.md](PROJECT_SUMMARY.md)**
   - 项目总结

## 🎯 推荐学习路径

### 第一天：熟悉基本操作
```bash
# 1. 启动 Xcode
./scripts/open_xcode.sh

# 2. 阅读文档
open HOW_TO_DEBUG_IN_XCODE.md

# 3. 运行测试
# 在 Xcode 中按 ⌘R
```

### 第二天：深入调试
- 设置断点
- 查看日志
- 单步执行
- 查看变量

### 第三天：性能分析
- 使用 Instruments
- CPU Profiler
- 内存泄漏检测

## 🛠️ 常用命令

```bash
# Xcode 调试
./scripts/open_xcode.sh

# 命令行构建
./build.sh desktop release

# 生成测试视频
./scripts/generate_test_video.sh

# 快速测试
./scripts/quick_test.sh

# 代码格式化
find src include -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

## 📁 项目结构

```
YXVodPlayer/
├── src/              # 源代码
│   ├── core/         # 核心引擎
│   ├── desktop/      # 桌面版
│   └── platform/     # 平台抽象
├── include/          # 头文件
├── docs/             # 技术文档
├── scripts/          # 脚本工具
├── build/            # 构建输出
│   ├── xcode/        # Xcode 项目
│   └── desktop_*/    # 桌面版构建
└── test_videos/      # 测试视频
```

## 🎬 视频教程（文字版）

### 如何用 Xcode 调试

**步骤 1**: 启动
```bash
./scripts/open_xcode.sh
```

**步骤 2**: 设置断点
- 打开 `src/core/player_core.cpp`
- 在第 57 行点击左侧设置断点
- 在第 435 行设置断点
- 在第 530 行设置断点

**步骤 3**: 配置参数（可选）
- 点击顶部 "YXVodPlayer" → Edit Scheme
- Run → Arguments
- 添加: `$(SRCROOT)/../../test_videos/test1_pattern.mp4`

**步骤 4**: 运行
- 按 ⌘R 或点击播放按钮

**步骤 5**: 调试
- 按 F6 单步执行
- 按 F7 进入函数
- 按 F8 继续
- 按 ⌘⇧C 查看日志

## ❓ 常见问题

### Q: 如何开始？
A: 运行 `./scripts/open_xcode.sh`，然后在 Xcode 中按 ⌘R

### Q: 没有测试视频怎么办？
A: 运行 `./scripts/generate_test_video.sh`

### Q: 如何查看日志？
A: 在 Xcode 中按 ⌘⇧C 打开 Console

### Q: 如何设置断点？
A: 在代码行号左侧点击即可

### Q: 更多问题？
A: 查看 [HOW_TO_DEBUG_IN_XCODE.md](HOW_TO_DEBUG_IN_XCODE.md)

## 🎁 特色功能

- ✅ 一键启动 Xcode
- ✅ 详细的日志输出
- ✅ 媒体信息自动打印
- ✅ 性能计时器
- ✅ 完整的调试工具
- ✅ 30+ 页文档

## 🚀 立即开始

```bash
./scripts/open_xcode.sh
```

然后在 Xcode 中按 **⌘R**，开始你的调试之旅！

---

**需要帮助？**
1. 查看 [HOW_TO_DEBUG_IN_XCODE.md](HOW_TO_DEBUG_IN_XCODE.md)
2. 查看 [XCODE_GUIDE.md](XCODE_GUIDE.md)
3. 查看 [USER_GUIDE.md](USER_GUIDE.md)

**祝你调试愉快！** 🎉
