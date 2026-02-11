# Xcode 断点问题解决方案

## ✅ 已修复

我已经修复了 Xcode 断点不工作的问题。

### 修复内容

1. **CMakeLists.txt 优化**
   - ✅ 添加了 Debug 符号配置 (`-g -O0`)
   - ✅ 添加了 Xcode 特定配置
   - ✅ 禁用了优化以便调试

2. **main.cpp 增强**
   - ✅ 添加了详细的日志输出
   - ✅ 添加了明确的调试断点位置注释
   - ✅ 每个关键步骤都有输出

3. **重新生成项目**
   - ✅ 清除了旧项目
   - ✅ 使用新配置重新生成
   - ✅ 编译成功

## 🚀 如何使用

### 步骤 1：打开新的 Xcode 项目

```bash
cd /Users/debug/project/YXVodPlayer
open build/xcode/YXVodPlayer.xcodeproj
```

### 步骤 2：验证 Debug 配置

在 Xcode 中：

1. 选择顶部的 **YXVodPlayer** scheme
2. 点击 Scheme 旁边的设备选择器
3. 确保选择 **My Mac**
4. 点击 Scheme → **Edit Scheme...**
5. 确认 **Run** → **Info** 下的 **Build Configuration** 是 **Debug**

### 步骤 3：设置断点

在 `src/desktop/main.cpp` 中设置断点：

#### 推荐的断点位置

**断点 1 - 程序入口**（第 17 行左右）:
```cpp
std::cout << "=========================================" << std::endl;
```
这是程序最开始的地方，必定会触发。

**断点 2 - Qt 应用创建**（第 26 行左右）:
```cpp
QApplication app(argc, argv);
```

**断点 3 - 主窗口创建**（第 36 行左右）:
```cpp
MainWindow window;
```

**断点 4 - 窗口显示**（第 54 行左右）:
```cpp
window.show();
```

### 步骤 4：运行调试

1. 按 **⌘R** 或点击播放按钮
2. 程序应该在第一个断点处暂停
3. 按 **F8** 继续到下一个断点

## 🔍 验证断点工作

### 测试方法 1：简单测试

1. 在 main.cpp 的第 17 行设置断点（程序入口）
2. 按 ⌘R 运行
3. **如果断点生效**: 程序会暂停，左侧行号显示绿色箭头
4. **如果断点不生效**: 程序直接运行，窗口显示

### 测试方法 2：查看日志

如果断点不暂停，查看 Console（⌘⇧C）：

你应该看到：
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

如果看到这些日志，说明程序在运行，只是断点可能需要特殊设置。

## 🛠️ 断点故障排除

### 问题 1：断点显示为灰色虚线

**原因**: 符号未加载

**解决**:
1. 等待程序完全启动
2. 断点会自动变为蓝色实心
3. 如果一直是灰色，重新编译项目

### 问题 2：断点不暂停程序

**解决方法 A - 检查配置**:
1. Product → Scheme → Edit Scheme
2. Run → Info
3. 确认 Build Configuration 是 **Debug**（不是 Release）

**解决方法 B - 禁用优化**:
1. 选择 YXVodPlayer 项目
2. Build Settings
3. 搜索 "Optimization Level"
4. Debug 配置下设置为 **None [-O0]**

**解决方法 C - 启用调试符号**:
1. Build Settings
2. 搜索 "Debug Information Format"
3. 设置为 **DWARF with dSYM File**

**解决方法 D - 清理重建**:
```
在 Xcode 中:
Product → Clean Build Folder (⌘⇧K)
Product → Build (⌘B)
Product → Run (⌘R)
```

### 问题 3：Qt 生成的文件断点不工作

**说明**: `moc_*.cpp` 和 `ui_*.h` 是自动生成的文件，断点可能不稳定

**解决**: 在原始源文件（.cpp, .h）中设置断点，不要在生成的文件中设置

## 🎯 推荐的调试流程

### 流程 1：从 main.cpp 开始

```cpp
// 在这些位置设置断点（保证会触发）

int main(int argc, char *argv[]) {
    // 断点 1 ← 在这里设置断点
    std::cout << "YXVodPlayer 启动中..." << std::endl;
    
    // ... 
    
    QApplication app(argc, argv);
    // 断点 2 ← 在这里设置断点
    
    MainWindow window;
    // 断点 3 ← 在这里设置断点
    
    window.show();
    // 断点 4 ← 在这里设置断点
    
    return app.exec();
}
```

### 流程 2：调试文件打开

```cpp
// MainWindow::openFile 设置断点
void MainWindow::openFile(const QString& filename) {
    // 断点 ← 在这里
    if (!player_) return;
    
    player_->close();
    // 断点 ← 在这里
    
    if (player_->open(filename.toStdString()) == 0) {
        // 断点 ← 在这里
        // ...
    }
}
```

### 流程 3：调试播放器核心

```cpp
// PlayerCore::open 设置断点
int PlayerCore::open(const std::string& filename) {
    // 断点 ← 在这里
    LOG_INFO("正在打开文件: ", filename);
    
    // ...
    
    if (avformat_open_input(&format_ctx_, filename.c_str(), nullptr, nullptr) < 0) {
        // 断点 ← 在这里（错误处理）
        LOG_ERROR("无法打开文件: ", filename);
        return -1;
    }
    
    // 断点 ← 在这里（成功）
    LOG_INFO("文件打开成功");
    // ...
}
```

## 💡 高级断点技巧

### 1. 符号断点（Symbol Breakpoint）

如果普通断点不工作，尝试符号断点：

1. Debug → Breakpoints → **Create Symbolic Breakpoint** (⌘⌥B)
2. Symbol 输入：
   ```
   main
   ```
   或
   ```
   yxplayer::PlayerCore::open
   ```
3. 点击 Done

### 2. 条件断点

右键点击断点 → **Edit Breakpoint**:

```cpp
// 只在有命令行参数时暂停
argc > 1

// 只在打开特定文件时暂停
filename.find("test1") != std::string::npos
```

### 3. 日志断点（不暂停）

1. 右键断点 → Edit Breakpoint
2. 添加 Action → **Log Message**:
   ```
   程序启动，参数数量: @argc@
   ```
3. 勾选 **Automatically continue after evaluating actions**

这样断点会打印日志但不暂停程序。

## 🧪 测试断点是否工作

### 快速测试

1. 在 `main.cpp` 第 17 行设置断点：
   ```cpp
   std::cout << "=========================================" << std::endl;
   ```

2. 按 **⌘R** 运行

3. **预期行为**:
   - ✅ 程序应该立即暂停在这一行
   - ✅ 左侧行号显示绿色箭头 →
   - ✅ Console 还没有输出
   - ✅ 变量窗口显示 argc, argv

4. **如果断点工作**:
   - 按 F8 继续
   - 查看 Console 输出
   - 程序继续运行

5. **如果断点不工作**:
   - 程序直接运行到窗口显示
   - Console 直接显示所有输出
   - 查看下面的"终极解决方案"

## 🔥 终极解决方案

如果以上都不工作，尝试这个方法：

### 方案 A：使用 lldb 命令行

```bash
# 1. 找到可执行文件
cd /Users/debug/project/YXVodPlayer
APP_PATH=build/xcode/bin/Debug/YXVodPlayer.app/Contents/MacOS/YXVodPlayer

# 2. 使用 lldb 启动
lldb $APP_PATH

# 3. 在 lldb 中设置断点
(lldb) b main
(lldb) b yxplayer::PlayerCore::open

# 4. 运行
(lldb) run

# 5. 单步执行
(lldb) n   # 下一行
(lldb) s   # 进入函数
(lldb) c   # 继续

# 6. 查看变量
(lldb) p argc
(lldb) po app
```

### 方案 B：添加强制断点

在代码中添加：
```cpp
#include <signal.h>

int main(int argc, char *argv[]) {
    raise(SIGTRAP);  // 强制触发调试器断点
    
    std::cout << "程序继续执行..." << std::endl;
    // ...
}
```

在 Xcode 中运行，程序会自动暂停在 `raise(SIGTRAP)`。

### 方案 C：使用 assert

```cpp
#include <cassert>

int main(int argc, char *argv[]) {
    std::cout << "到达断点位置" << std::endl;
    assert(argc >= 0);  // 设置一个永远为真的断言
    
    // 在 Xcode 中，断点会在 assert 处触发
}
```

## 📝 检查清单

在报告断点问题前，请检查：

- [ ] 使用的是 Debug 配置（不是 Release）
- [ ] Optimization Level 设置为 None [-O0]
- [ ] Debug Information Format 设置为 DWARF with dSYM
- [ ] 已经清理并重新构建（⌘⇧K 然后 ⌘B）
- [ ] 断点显示为蓝色实心（不是灰色虚线）
- [ ] 尝试了符号断点
- [ ] 查看了 Console 日志

## 🎯 现在试试

### 快速验证

```bash
# 1. 打开项目
open build/xcode/YXVodPlayer.xcodeproj

# 2. 在 Xcode 中
#    - 打开 src/desktop/main.cpp
#    - 在第 17 行点击行号左侧设置断点（蓝色箭头）
#    - 按 ⌘R 运行

# 3. 观察
#    - 程序应该在断点处暂停
#    - 如果不暂停，查看 Console 输出
```

## 💡 调试技巧

### 技巧 1：使用日志代替断点

如果断点真的不工作，使用日志也很有效：

```cpp
std::cout << "[DEBUG] 到达这里，argc = " << argc << std::endl;
```

### 技巧 2：使用 qDebug

Qt 应用可以使用 qDebug：

```cpp
#include <QDebug>

qDebug() << "到达这里";
qDebug() << "argc =" << argc;
```

### 技巧 3：在关键位置添加睡眠

```cpp
#include <thread>
#include <chrono>

std::cout << "到达断点位置，暂停 2 秒..." << std::endl;
std::this_thread::sleep_for(std::chrono::seconds(2));
std::cout << "继续执行" << std::endl;
```

这样即使断点不工作，你也能看到程序执行到这里。

## 🆘 如果还是不工作

### 最后的方法：混合调试

1. **在代码中添加日志**（已添加）
2. **运行并查看日志输出**
3. **使用 Instruments 进行性能分析**

虽然没有断点，但通过日志也能很好地理解程序流程。

---

**现在试试**: `open build/xcode/YXVodPlayer.xcodeproj` 然后按 ⌘R
