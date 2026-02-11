# Xcode 优化总结

## 完成的优化

### 1. Xcode 项目生成 ✅

**位置**: `build/xcode/YXVodPlayer.xcodeproj`

**生成方式**:
```bash
# 方式 1: 使用脚本（推荐）
./scripts/open_xcode.sh

# 方式 2: 使用构建脚本
./build.sh xcode

# 方式 3: 手动生成
mkdir -p build/xcode && cd build/xcode
cmake ../.. -G Xcode -DBUILD_DESKTOP=ON
```

### 2. 调试功能增强 ✅

#### 日志系统改进

**文件**: `include/logger.h`

添加了更详细的日志输出：
```cpp
LOG_INFO("初始化 PlayerCore...");
LOG_INFO("SDL 初始化成功");
LOG_INFO("正在打开文件: ", filename);
LOG_INFO("视频流打开成功, 分辨率: ", width, "x", height);
```

#### 调试辅助工具

**文件**: `include/debug_helper.h`

新增功能：
- ✅ 时间格式化（HH:MM:SS.mmm）
- ✅ 文件大小格式化（B/KB/MB/GB）
- ✅ 比特率格式化（bps/kbps/Mbps）
- ✅ 编码器信息获取
- ✅ 详细媒体信息打印
- ✅ 帧信息打印
- ✅ 性能统计
- ✅ 性能计时器

**使用示例**:
```cpp
// 打印详细媒体信息
DebugHelper::print_media_info(format_ctx_);

// 性能计时
{
    PERF_TIMER("decode_video_frame");
    // 代码...
}

// 格式化时间
std::string time_str = DebugHelper::format_time(123.456);
// 输出: "02:03.456"
```

### 3. 文档完善 ✅

#### 新增文档

1. **XCODE_GUIDE.md** - 详细的 Xcode 调试指南
   - Scheme 配置
   - 断点设置
   - LLDB 命令
   - Instruments 使用
   - 性能分析
   - 故障排除

2. **XCODE_QUICKSTART.md** - 快速开始指南
   - 一键启动
   - 基本配置
   - 常用快捷键
   - 推荐断点

3. **XCODE_OPTIMIZATION_SUMMARY.md** - 本文档

### 4. 自动化脚本 ✅

#### open_xcode.sh

**位置**: `scripts/open_xcode.sh`

**功能**:
- 自动检查并生成 Xcode 项目
- 自动生成测试视频
- 自动打开 Xcode
- 显示使用提示

**使用**:
```bash
./scripts/open_xcode.sh
```

### 5. 代码质量改进 ✅

#### .clang-format

**位置**: `.clang-format`

**配置**:
- 基于 Google Style
- 缩进: 4 空格
- 列宽: 100
- 指针对齐: 左对齐

**使用**:
```bash
# 格式化单个文件
clang-format -i src/core/player_core.cpp

# 格式化所有文件
find src include -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

### 6. 构建系统增强 ✅

#### build.sh 扩展

新增 `xcode` 命令：
```bash
./build.sh xcode
```

自动生成 Xcode 项目。

## 调试工作流

### 标准调试流程

```
1. 生成 Xcode 项目
   ./scripts/open_xcode.sh

2. 在 Xcode 中设置断点
   - PlayerCore::open (文件打开)
   - PlayerCore::video_thread (视频解码)
   - PlayerCore::audio_callback_impl (音频回调)

3. 配置启动参数（可选）
   Edit Scheme → Arguments
   添加: $(SRCROOT)/../../test_videos/test1_pattern.mp4

4. 运行调试
   ⌘R

5. 查看日志
   ⌘⇧C 打开 Console
   查看 LOG_INFO 输出

6. 单步调试
   F6 (Step Over)
   F7 (Step Into)
   F8 (Continue)
```

### 性能分析流程

```
1. Profile 应用
   ⌘I

2. 选择工具
   - Time Profiler (CPU 使用)
   - Allocations (内存使用)
   - Leaks (内存泄漏)

3. 录制
   点击红色录制按钮

4. 使用应用
   正常操作播放器

5. 停止并分析
   点击停止，查看结果
```

## 推荐的 Xcode 设置

### Build Settings

**Debug 配置**:
```
Optimization Level: None [-O0]
Debug Information Format: DWARF with dSYM File
Enable Testability: Yes
```

**Release 配置**:
```
Optimization Level: Fastest, Smallest [-Os]
Strip Debug Symbols: Yes
Dead Code Stripping: Yes
```

### Behaviors

**Build Succeeds**:
- Show Navigator: Project Navigator
- Play Sound: Glass

**Build Fails**:
- Show Navigator: Issue Navigator
- Play Sound: Basso

### Font & Colors

推荐主题: Default (Dark) 或 Xcode Default

## 常见调试场景

### 场景 1: 文件打开失败

**断点位置**:
```
src/core/player_core.cpp:59
avformat_open_input() 之后
```

**检查变量**:
```lldb
p filename.c_str()
p format_ctx_
```

### 场景 2: 音视频不同步

**断点位置**:
```
src/core/player_core.cpp:622
get_master_clock()
```

**检查时钟**:
```lldb
p audio_clock_.get_clock()
p video_clock_.get_clock()
p audio_clock_.pts
p video_clock_.pts
```

### 场景 3: 内存泄漏

**使用 Instruments**:
```
1. Product → Profile (⌘I)
2. 选择 "Leaks"
3. 录制一段时间
4. 查看 Leaks 列表
5. 定位泄漏的调用栈
```

### 场景 4: 性能瓶颈

**使用 Time Profiler**:
```
1. Product → Profile (⌘I)
2. 选择 "Time Profiler"
3. 录制
4. 查看 Heaviest Stack Trace
5. 双击进入热点函数
6. 优化代码
```

## 性能优化建议

### 1. 使用 Release 构建测试性能

```bash
# 生成 Release Xcode 项目
cd build/xcode
cmake ../.. -G Xcode -DCMAKE_BUILD_TYPE=Release -DBUILD_DESKTOP=ON
```

### 2. 启用编译器优化

在 Build Settings 中：
```
Optimization Level: Fastest [-O3]
Link-Time Optimization: Yes
```

### 3. 使用 Instruments 定位瓶颈

重点关注：
- `av_read_frame` 调用次数
- `sws_scale` 执行时间
- `swr_convert` 执行时间
- 内存分配次数

### 4. 优化关键路径

**视频解码线程**:
- 减少格式转换
- 考虑硬件加速
- 优化帧队列管理

**音频回调**:
- 减少重采样开销
- 优化缓冲区管理
- 避免阻塞操作

## 测试检查清单

使用 Xcode 调试时的测试项目：

- [ ] 文件打开正常
- [ ] 视频正常显示
- [ ] 音频正常播放
- [ ] 音视频同步正确
- [ ] 进度条工作正常
- [ ] Seek 跳转正常
- [ ] 音量控制有效
- [ ] 播放/暂停流畅
- [ ] 无内存泄漏
- [ ] CPU 使用率合理
- [ ] 日志输出正常
- [ ] 错误处理正确

## 下一步改进

### 短期
- [ ] 添加单元测试
- [ ] 添加 UI 测试
- [ ] 集成 CI/CD

### 中期
- [ ] 性能优化
- [ ] 硬件加速支持
- [ ] 更多调试工具

### 长期
- [ ] 完整的测试套件
- [ ] 自动化性能测试
- [ ] 覆盖率报告

## 资源链接

### Apple 官方
- [Xcode Help](https://developer.apple.com/documentation/xcode)
- [LLDB Debugging Guide](https://lldb.llvm.org/)
- [Instruments User Guide](https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/InstrumentsUserGuide/)

### 第三方
- [Clang-Format Style Options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
- [CMake Generators](https://cmake.org/cmake/help/latest/manual/cmake-generators.7.html)

## 总结

✅ **Xcode 项目已完全配置并优化**

现在你可以：

1. **快速启动**: `./scripts/open_xcode.sh`
2. **设置断点**: 在关键函数位置
3. **运行调试**: ⌘R
4. **查看日志**: ⌘⇧C
5. **性能分析**: ⌘I
6. **代码格式化**: clang-format

所有工具和文档都已就绪，可以开始高效的 Xcode 调试！

---

**开始使用**: `./scripts/open_xcode.sh` 🚀
