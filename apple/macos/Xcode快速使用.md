# macOS Xcode 项目使用快速指南

## 🚀 快速开始

### 一键生成并打开 Xcode 项目

```bash
cd /Users/debug/project/YXVodPlayer/src/macos
./build.sh
```

执行后会自动：
1. ✅ 清理旧的构建目录
2. ✅ 生成 Xcode 项目
3. ✅ 打开 Xcode

### 在 Xcode 中运行

1. Xcode 打开后，选择 Scheme：`HXCPlayer-macOS`
2. 点击运行按钮（⌘R）或选择 `Product > Run`
3. 应用会启动，可以打开视频文件播放

## 📂 项目位置

- **Xcode 项目**: `src/macos/build/HXCPlayer-macOS.xcodeproj`
- **源代码**: `src/macos/*.mm` 和 `src/macos/*.h`
- **Core 库**: `src/core/` (C++ 解码核心)

## 🔧 脚本说明

### build.sh - 构建脚本
```bash
./build.sh
```
- 功能：生成 Xcode 项目并自动打开
- 用途：首次构建或重新生成项目

### open_xcode.sh - 快速打开
```bash
./open_xcode.sh
```
- 功能：快速打开已存在的 Xcode 项目
- 用途：项目已生成，想要再次打开

## 🐛 调试技巧

### 1. 设置断点
- 在代码行号左侧点击，添加蓝色断点标记
- 运行时程序会在断点处暂停

### 2. 查看变量
- 在断点处查看下方的 Variables View
- 鼠标悬停在变量上查看值

### 3. 单步执行
- **F6**: Step Over（单步跳过）
- **F7**: Step Into（单步进入）
- **F8**: Continue（继续执行）

### 4. 控制台输出
- 查看 Xcode 底部的 Console 区域
- 所有 `NSLog()` 输出都会显示在这里

### 5. 性能分析
- 选择 `Product > Profile` (⌘I)
- 选择 Time Profiler 或 Allocations

## 🎯 常用操作

### 编译项目
```
⌘B 或 Product > Build
```

### 清理构建
```
⇧⌘K 或 Product > Clean Build Folder
```

### 运行项目
```
⌘R 或 Product > Run
```

### 停止运行
```
⌘. 或 Product > Stop
```

## 📝 文件编辑

### 在 Xcode 中编辑代码
1. 在左侧 Project Navigator 中找到文件
2. 双击打开编辑
3. 修改后直接运行（⌘R）会自动重新编译

### 添加新文件
1. 右键点击项目文件夹
2. 选择 `New File...`
3. 选择文件类型（Objective-C++ 或 C++）
4. **重要**: 添加后需要更新 `CMakeLists.txt`

## 🔄 重新生成项目

如果遇到问题或添加了新文件：

```bash
cd /Users/debug/project/YXVodPlayer/src/macos
rm -rf build
./build.sh
```

## 📚 相关文档

- **详细指南**: `Xcode项目构建指南.md`
- **完整总结**: `Xcode项目配置完成总结.md`
- **项目说明**: `README.md`

## ⚠️ 注意事项

1. **首次生成可能需要 30 秒左右**
   - CMake 需要检测编译器和依赖库

2. **依赖库要求**
   ```bash
   brew install ffmpeg soundtouch sdl2
   ```

3. **不要提交 build 目录到 Git**
   - Xcode 项目是自动生成的
   - 每次需要时重新生成即可

4. **修改 CMakeLists.txt 后**
   - 需要重新运行 `./build.sh`
   - Xcode 项目才会更新

## 🎉 完成！

现在你可以在 Xcode 中愉快地开发和调试 macOS 播放器了！

---

**快速命令备忘**

```bash
# 首次生成项目
cd src/macos && ./build.sh

# 再次打开项目
cd src/macos && ./open_xcode.sh

# 重新生成（遇到问题时）
cd src/macos && rm -rf build && ./build.sh

# 命令行编译（可选）
cd src/macos/build
xcodebuild -project HXCPlayer-macOS.xcodeproj -scheme HXCPlayer-macOS
```
