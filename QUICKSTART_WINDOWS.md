# Windows 平台快速开始指南

## ⚠️ 重要提示：中文显示问题

如果在 PowerShell 中运行脚本时出现中文乱码，请先执行：

```powershell
chcp 65001
```

或者使用命令提示符（CMD）代替 PowerShell。详细解决方案请查看 [ENCODING_FIX.md](ENCODING_FIX.md)

---

## 新手入门（3 步搞定）

### 第 1 步：检查环境

运行环境检查脚本：
```cmd
check_windows_env.bat
```

这个脚本会检查：
- Git
- CMake
- Visual Studio
- vcpkg
- 所有依赖库（FFmpeg, SDL2, Qt5, SoundTouch）

### 第 2 步：安装依赖（如果需要）

如果环境检查发现缺少依赖，运行：
```cmd
powershell -ExecutionPolicy Bypass -File setup_windows_deps.ps1
```

**注意**: 
- 需要管理员权限以设置环境变量
- Qt5 的安装可能需要 30-60 分钟
- 确保有足够的磁盘空间（至少 10GB）

### 第 3 步：生成并打开 Visual Studio 项目

**方法 A: 一键启动（推荐新手）**
```cmd
quickstart_windows.bat
```
这个脚本会：
1. 询问是否安装依赖
2. 生成 Visual Studio 项目
3. 询问是否打开项目

**方法 B: 手动控制**
```cmd
# 生成 VS 2022 项目
build_windows.bat vs2022

# 打开项目
start build\vs2022_release\YXVodPlayer.sln
```

## 在 Visual Studio 中开发

### 第一次打开项目

1. 双击打开 `YXVodPlayer.sln`
2. 在解决方案资源管理器中找到 `YXVodPlayer` 项目
3. 右键点击 -> "设为启动项目"

### 编译项目

**Release 模式（推荐）:**
- 工具栏选择 "Release" 和 "x64"
- 按 Ctrl+Shift+B 或 菜单：生成 -> 生成解决方案

**Debug 模式（开发调试）:**
- 工具栏选择 "Debug" 和 "x64"
- 按 Ctrl+Shift+B 编译

### 运行和调试

**运行（不调试）:**
- 按 Ctrl+F5
- 或菜单：调试 -> 开始执行（不调试）

**调试运行:**
- 按 F5
- 或菜单：调试 -> 开始调试

### 设置命令行参数

如果想在启动时打开视频文件：

1. 右键点击 `YXVodPlayer` 项目 -> 属性
2. 配置属性 -> 调试
3. 命令参数：输入视频文件路径，例如：
   ```
   C:\Videos\test.mp4
   ```
   或相对路径：
   ```
   ..\..\..\test_video.mp4
   ```

### 设置断点调试

1. 在代码行号左侧点击设置断点（或按 F9）
2. 按 F5 开始调试
3. 当程序运行到断点时会暂停
4. 调试快捷键：
   - F5: 继续
   - F10: 单步跳过
   - F11: 单步进入
   - Shift+F11: 单步跳出
   - Shift+F5: 停止调试

### 查看变量

- **局部变量窗口**: 调试 -> 窗口 -> 局部变量
- **监视窗口**: 调试 -> 窗口 -> 监视
- **即时窗口**: 调试 -> 窗口 -> 即时窗口（Ctrl+Alt+I）

## 项目结构

```
build/vs2022_release/
├── YXVodPlayer.sln          # 解决方案文件（双击打开）
├── hxcplayer_core.vcxproj   # 核心库项目
├── YXVodPlayer.vcxproj      # 主程序项目
└── bin/
    └── Release/
        └── YXVodPlayer.exe  # 编译输出
```

## 常见开发任务

### 重新生成项目

如果 CMakeLists.txt 有更改：
```cmd
# 删除旧的构建目录
rmdir /s /q build\vs2022_release

# 重新生成
build_windows.bat vs2022
```

### 清理构建

```cmd
build_windows.bat clean
```

### 切换构建类型

**Debug 模式:**
```cmd
build_windows.bat vs2022 debug
```

**Release 模式:**
```cmd
build_windows.bat vs2022 release
```

### 更新依赖

如果需要更新某个依赖：
```cmd
cd %VCPKG_ROOT%
vcpkg update
vcpkg upgrade ffmpeg:x64-windows
```

## 性能优化建议

### 编译优化

1. 使用 Release 模式
2. 启用多核编译：
   - 工具 -> 选项 -> 项目和解决方案 -> 生成和运行
   - 最大并行项目生成数：设为 CPU 核心数

### 代码优化

1. 使用性能探查器：
   - 调试 -> 性能探查器
   - 选择 CPU 使用情况、内存使用情况等

2. 检查内存泄漏：
   - 调试 -> Windows -> 显示诊断工具

## 常见问题

### 编译很慢

**解决方法:**
- 确保使用 SSD
- 增加 Visual Studio 的并行编译数
- 关闭不必要的杀毒软件实时扫描
- 使用预编译头

### 调试时断点不生效

**解决方法:**
- 确认使用 Debug 配置
- 清理解决方案后重新生成
- 项目属性 -> C/C++ -> 优化 -> 已禁用

### 运行时找不到 DLL

**解决方法:**
- 使用 vcpkg 的自动复制功能
- 或手动复制所需 DLL 到可执行文件目录

详细的故障排除请参考：[TROUBLESHOOTING_WINDOWS.md](TROUBLESHOOTING_WINDOWS.md)

## VS Code 用户

如果你更喜欢使用 VS Code：

1. 安装扩展：
   - C/C++
   - CMake Tools

2. 打开项目文件夹

3. 使用 CMake 工具栏配置和构建

4. 按 F5 调试（使用 `.vscode/launch.json` 配置）

## 更多资源

- **完整文档**: [README_WINDOWS.md](README_WINDOWS.md)
- **故障排除**: [TROUBLESHOOTING_WINDOWS.md](TROUBLESHOOTING_WINDOWS.md)
- **主 README**: [README.md](README.md)

## 获取帮助

如果遇到问题：

1. 运行 `check_windows_env.bat` 检查环境
2. 查看 [TROUBLESHOOTING_WINDOWS.md](TROUBLESHOOTING_WINDOWS.md)
3. 检查 Visual Studio 输出窗口的错误信息
4. 提交 Issue 时提供：
   - 操作系统版本
   - Visual Studio 版本
   - 完整的错误信息
   - 重现步骤

---

祝开发顺利！🚀
