# ✅ 准备就绪！可以在 Xcode 中调试了

## 🎉 编译成功！

```
** BUILD SUCCEEDED **
```

可执行文件：`build/xcode/bin/Debug/YXVodPlayer.app`

## 🚀 立即开始（3 步）

### 1️⃣ 打开 Xcode

```bash
cd /Users/debug/project/YXVodPlayer
open build/xcode/YXVodPlayer.xcodeproj
```

### 2️⃣ 设置断点

**最简单的断点位置**（保证会触发）：

打开文件：`src/desktop/main.cpp`

在这些行设置断点（点击行号左侧）：

```cpp
第 17 行: std::cout << "YXVodPlayer 启动中..." << std::endl;
         ☝️ 在这里点击设置断点（蓝色圆点）

第 36 行: MainWindow window;
         ☝️ 主窗口创建

第 54 行: window.show();
         ☝️ 窗口显示
```

### 3️⃣ 运行调试

按 **⌘R** 或点击播放按钮 ▶️

**预期行为**：
- ✅ 程序在第 17 行暂停
- ✅ 行号左侧显示绿色箭头 →
- ✅ 底部 Variables View 显示 `argc`, `argv`
- ✅ Console 还没有任何输出（因为还没执行到 cout）

**如果暂停了**：
- 按 **F8** 继续到下一个断点
- 或按 **F6** 单步执行

**如果没有暂停**：
- 查看下面的"断点故障排除"

## 🔧 断点故障排除

### 检查 1：确认 Debug 配置

1. 点击顶部 Scheme: **YXVodPlayer** → **Edit Scheme...**
2. Run → Info
3. 确认 **Build Configuration** 是 **Debug**
4. 点击 Close

### 检查 2：确认断点已启用

- 断点应该是**蓝色实心圆点**（不是灰色虚线）
- 按 **⌘Y** 切换断点启用/禁用
- 确保没有禁用

### 检查 3：清理重建

```
在 Xcode 中：
1. Product → Clean Build Folder (⌘⇧K)
2. Product → Build (⌘B)  
3. Product → Run (⌘R)
```

### 检查 4：查看 Console 日志

按 **⌘⇧C** 打开 Console

即使断点不暂停，你也应该看到详细的日志：
```
=========================================
YXVodPlayer 启动中...
参数数量: 1
参数[0]: /path/to/YXVodPlayer
=========================================
FFmpeg 初始化完成
Qt 应用创建完成
正在创建主窗口...
主窗口创建完成
显示主窗口...
窗口已显示，进入事件循环
=========================================
```

如果看到日志，说明程序在运行，只是断点配置需要调整。

## 🎯 备用方案：使用符号断点

如果普通断点不工作，使用符号断点：

1. Debug → Breakpoints → **Create Symbolic Breakpoint**
2. Symbol 输入：`main`
3. 点击 Done
4. 按 ⌘R 运行

符号断点更可靠，应该会暂停在 main 函数入口。

## 📊 验证编译产物

检查调试符号是否存在：

```bash
# 查看可执行文件
ls -lh build/xcode/bin/Debug/YXVodPlayer.app/Contents/MacOS/YXVodPlayer
# 应该显示约 400KB

# 查看调试符号
ls -lh build/xcode/bin/Debug/YXVodPlayer.app.dSYM 2>/dev/null || echo "dSYM 未生成"

# 检查是否有调试信息
dwarfdump build/xcode/bin/Debug/YXVodPlayer.app/Contents/MacOS/YXVodPlayer 2>&1 | head -5
```

## 🎬 测试视频

测试视频位置：
```
test_videos/
├── test1_pattern.mp4  ← 推荐使用这个
├── test2_simple.mp4
├── test3_video_only.mp4
└── test4_4k.mp4
```

在播放器中点击"打开"按钮选择。

## 📚 相关文档

- **XCODE_BREAKPOINT_FIX.md** - 断点问题解决方案
- **XCODE_QUICKSTART.md** - Xcode 快速开始
- **XCODE_GUIDE.md** - 完整的 Xcode 调试指南
- **HOW_TO_DEBUG_IN_XCODE.md** - 详细调试教程

## 🆘 仍然有问题？

### 方法 1：使用日志调试

代码中已经添加了详细的日志，通过日志也能很好地调试：

```cpp
std::cout << "到达这里，变量值: " << value << std::endl;
```

### 方法 2：使用 LLDB 命令行

```bash
# 直接用 lldb 运行
lldb build/xcode/bin/Debug/YXVodPlayer.app/Contents/MacOS/YXVodPlayer

# 在 lldb 中
(lldb) b main
(lldb) run
(lldb) n  # 单步
(lldb) c  # 继续
```

### 方法 3：检查 Build Settings

在 Xcode 中：

1. 选择项目 **YXVodPlayer**
2. 选择 Target **YXVodPlayer**
3. Build Settings
4. 搜索并检查：
   - **Optimization Level (Debug)**: None [-O0] ✅
   - **Debug Information Format**: DWARF with dSYM File ✅
   - **Generate Debug Symbols**: Yes ✅
   - **Strip Debug Symbols During Copy**: No ✅

## ✨ 额外提示

### 调试 Qt 信号和槽

Qt 对象可以用 `po` 命令查看：

```lldb
(lldb) po window
(lldb) po player_
```

### 查看 std::string

```lldb
(lldb) p filename
(lldb) po filename  # 更好的格式
```

### 查看指针

```lldb
(lldb) p *format_ctx_
(lldb) p format_ctx_->duration
```

---

## 🎊 总结

✅ **Xcode 项目配置完成**
✅ **Debug 符号正确配置**
✅ **详细日志已添加**
✅ **编译成功**

**现在运行**：

```bash
open build/xcode/YXVodPlayer.xcodeproj
# 然后在 Xcode 中按 ⌘R
```

**在 main.cpp 第 17 行设置断点，应该会暂停！**

如果还有问题，查看 **XCODE_BREAKPOINT_FIX.md** 获取更多解决方案。

**祝调试顺利！** 🎉
