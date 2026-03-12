# Windows Debug 运行指南

## ✅ 编译成功

恭喜！Debug 模式已经成功编译。

## 📦 运行时依赖

Windows 程序需要以下 DLL 文件：

### 必需的 DLL
- `SDL2.dll` ✅
- `Qt5Cored.dll` ✅
- `Qt5Guid.dll` ✅
- `Qt5Widgetsd.dll` ✅
- `platforms\qwindowsd.dll` ✅（Qt 平台插件）

### 可选的 DLL
- FFmpeg DLL（avcodec, avformat, avutil, swscale, swresample）

## 🚀 快速开始

### 方法 1：使用脚本自动复制 DLL（推荐）

```cmd
cd win-third
.\copy_dlls_debug.bat
```

这会自动复制所有必需的 DLL 到输出目录。

### 方法 2：在 Visual Studio 中运行

1. 确保已运行 `copy_dlls_debug.bat`
2. 在 VS 中按 `F5` 启动调试
3. 或按 `Ctrl+F5` 运行（不调试）

### 方法 3：直接运行可执行文件

```
build\vs2022_debug\bin\Debug\HXCVodPlayer.exe
```

## ❌ 常见问题

### 问题 1: "找不到 SDL2.dll"

**解决**:
```cmd
cd win-third
.\copy_dlls_debug.bat
```

### 问题 2: "无法启动此程序，因为计算机中丢失 MSVCP140D.dll"

**解决**: 安装 Visual C++ Redistributable
- 下载: https://aka.ms/vs/17/release/vc_redist.x64.exe
- 或在 Visual Studio Installer 中安装 "Visual C++ 运行时"

### 问题 3: 黑屏或崩溃

**可能原因**:
- Qt 平台插件未复制
- 缺少 FFmpeg DLL（如果播放视频）

**解决**:
1. 检查 `build\vs2022_debug\bin\Debug\platforms\qwindowsd.dll` 是否存在
2. 重新运行 `copy_dlls_debug.bat`

### 问题 4: "This application failed to start because no Qt platform plugin could be initialized"

**解决**:
```cmd
# 确保 platforms 目录和插件存在
mkdir build\vs2022_debug\bin\Debug\platforms
copy "C:\Qt\5.15.2\msvc2019_64\plugins\platforms\qwindowsd.dll" build\vs2022_debug\bin\Debug\platforms\
```

## 📝 自动化

### 将 DLL 复制集成到 Visual Studio

可以在 CMakeLists.txt 中添加后期构建事件，自动复制 DLL：

```cmake
if(WIN32)
    add_custom_command(TARGET HXCVodPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/vcpkg/installed/x64-windows/bin/SDL2.dll"
            $<TARGET_FILE_DIR:HXCVodPlayer>
        COMMENT "Copying SDL2.dll to output directory"
    )
endif()
```

## 🔄 切换到 Release 模式

Release 模式性能更好，适合实际使用：

1. 编译 Release 版本的 SoundTouch:
   ```cmd
   cd win-third
   .\build_soundtouch_multi.bat
   ```

2. 打开 Release 项目:
   ```cmd
   start build\vs2022_release\HXCVodPlayer.sln
   ```

3. 在 VS 中选择 Release 配置并编译

4. 复制 Release DLL（注意：不带 `d` 后缀）:
   ```cmd
   # 类似 copy_dlls_debug.bat，但使用:
   # - Qt5Core.dll (不是 Qt5Cored.dll)
   # - qwindows.dll (不是 qwindowsd.dll)
   ```

## 📚 相关文档

- `DEBUG_RELEASE_MISMATCH.md` - Debug/Release 库混用问题说明
- `MSVC_FIX.md` - SoundTouch MSVC 编译错误修复
- `README.md` - 项目主文档

---

**提示**: 每次重新编译后，如果 DLL 还没复制，需要重新运行 `copy_dlls_debug.bat`。
