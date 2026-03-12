# Windows DLL/静态库构建脚本（需在 Windows 环境执行）

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = "$ProjectRoot\build-windows"
$OutputDir = "$ProjectRoot\output\windows"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "构建 HXCPlayer Windows 库" -ForegroundColor Cyan
Write-Host "项目根目录: $ProjectRoot" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 清理旧构建
Write-Host ""
Write-Host "🧹 清理旧构建..." -ForegroundColor Yellow
if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
if (Test-Path $OutputDir) { Remove-Item -Recurse -Force $OutputDir }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# ========== 构建静态库 ==========
Write-Host ""
Write-Host "💻 构建 Windows 静态库（x64）..." -ForegroundColor Green

cmake -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_DESKTOP=OFF `
    -DBUILD_TESTS=OFF `
    -S "$ProjectRoot" `
    -B "$BuildDir"

cmake --build "$BuildDir" --config Release

# ========== 复制静态库 ==========
Write-Host ""
Write-Host "📦 复制静态库..." -ForegroundColor Yellow

$StaticOutputDir = "$OutputDir\static"
New-Item -ItemType Directory -Force -Path $StaticOutputDir | Out-Null

Copy-Item "$BuildDir\core\src\Release\hxcplayer_core.lib" "$StaticOutputDir\"
Copy-Item -Recurse "$ProjectRoot\core\include" "$StaticOutputDir\"

# ========== 构建 DLL ==========
Write-Host ""
Write-Host "💻 构建 Windows DLL（x64）..." -ForegroundColor Green

# 修改 CMake 配置为动态库
$DllBuildDir = "$ProjectRoot\build-windows-dll"
New-Item -ItemType Directory -Force -Path $DllBuildDir | Out-Null

# 创建临时 CMakeLists.txt（生成 DLL）
$TempCMake = @"
cmake_minimum_required(VERSION 3.15)
project(HXCPlayerDLL VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec libavformat libavutil libswscale libswresample
)

find_package(SDL2 REQUIRED)

set(CORE_SOURCES
    core/src/hxc_player_core.cpp
    core/src/hxc_decoder.cpp
    core/src/hxc_player_types.cpp
    core/src/hxc_packet_queue.cpp
    core/src/hxc_player_core_c_bridge.cpp
)

add_library(HXCPlayer SHARED `${CORE_SOURCES})

target_include_directories(HXCPlayer PUBLIC
    core/include
    `${FFMPEG_INCLUDE_DIRS}
    `${SDL2_INCLUDE_DIRS}
)

target_link_libraries(HXCPlayer PUBLIC
    `${FFMPEG_LIBRARIES}
    `${SDL2_LIBRARIES}
)

find_package(Threads REQUIRED)
target_link_libraries(HXCPlayer PUBLIC Threads::Threads)
"@

Set-Content -Path "$DllBuildDir\CMakeLists.txt" -Value $TempCMake

cmake -G "Visual Studio 17 2022" `
    -A x64 `
    -S "$DllBuildDir" `
    -B "$DllBuildDir\build"

cmake --build "$DllBuildDir\build" --config Release

# ========== 复制 DLL ==========
Write-Host ""
Write-Host "📦 复制 DLL..." -ForegroundColor Yellow

$DllOutputDir = "$OutputDir\dll"
New-Item -ItemType Directory -Force -Path $DllOutputDir | Out-Null

Copy-Item "$DllBuildDir\build\Release\HXCPlayer.dll" "$DllOutputDir\"
Copy-Item "$DllBuildDir\build\Release\HXCPlayer.lib" "$DllOutputDir\"
Copy-Item -Recurse "$ProjectRoot\core\include" "$DllOutputDir\"

# ========== 创建使用说明 ==========
$ReadmeContent = @"
# HXCPlayer Windows 库

## 文件说明

### 静态库（static/）
- ``hxcplayer_core.lib`` - 静态库（x64）
- ``include/`` - 头文件

### 动态库（dll/）
- ``HXCPlayer.dll`` - 动态链接库（x64）
- ``HXCPlayer.lib`` - 导入库（x64）
- ``include/`` - 头文件

## 使用方法

### 静态库

``````cmake
# CMakeLists.txt
include_directories(path/to/static/include)
target_link_libraries(your_target
    path/to/static/hxcplayer_core.lib
    avcodec avformat avutil swscale swresample
)
``````

### 动态库（DLL）

``````cmake
# CMakeLists.txt
include_directories(path/to/dll/include)
target_link_libraries(your_target
    path/to/dll/HXCPlayer.lib
)

# 运行时需要将 HXCPlayer.dll 放在可执行文件同目录
``````

### Visual Studio 项目

1. 项目属性 -> C/C++ -> 常规 -> 附加包含目录
   - 添加 ``include`` 目录

2. 项目属性 -> 链接器 -> 常规 -> 附加库目录
   - 添加库文件所在目录

3. 项目属性 -> 链接器 -> 输入 -> 附加依赖项
   - 静态库：添加 ``hxcplayer_core.lib``
   - DLL：添加 ``HXCPlayer.lib``

4. 如果使用 DLL，将 ``HXCPlayer.dll`` 复制到 exe 同目录

## 依赖要求

- FFmpeg（通过 vcpkg 安装）
- SDL2（通过 vcpkg 安装）
- SoundTouch（可选）

``````powershell
vcpkg install ffmpeg:x64-windows
vcpkg install sdl2:x64-windows
vcpkg install soundtouch:x64-windows
``````

## 示例代码

``````cpp
#include "hxc_player_core_c_bridge.h"

int main() {
    PlayerCoreHandle* player = player_core_create();
    player_core_open(player, "video.mp4");
    player_core_play(player);
    // ...
    player_core_release(player);
    return 0;
}
``````
"@

Set-Content -Path "$OutputDir\README.md" -Value $ReadmeContent

Write-Host ""
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "✅ Windows 库构建成功！" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "输出路径: $OutputDir" -ForegroundColor White
Write-Host ""
Write-Host "📋 包含的文件：" -ForegroundColor Yellow
Get-ChildItem -Recurse $OutputDir | Format-Table Name, Length
Write-Host ""
Write-Host "📄 详细说明请查看：" -ForegroundColor Yellow
Write-Host "  $OutputDir\README.md" -ForegroundColor White
Write-Host ""
