# 跨平台 libcurl 配置指南

## 概述

自定义数据源功能（`hxc_custom_io.cpp`）依赖 libcurl 进行 HTTP Range 请求。不同平台的 curl 配置方式不同。

---

## 各平台 libcurl 支持情况

| 平台 | 系统自带 | 配置方式 | 状态 |
|------|---------|---------|------|
| **iOS** | ✅ 是 | 自动 | ✅ 已配置 |
| **macOS** | ✅ 是 | 自动 | ✅ 已配置 |
| **Windows** | ❌ 否 | 手动 (vcpkg) | ⚠️  需配置 |
| **Android** | ❌ 否 | 手动 (NDK) | ⚠️  需配置 |
| **Linux** | ⚠️  可能 | apt/yum | ⚠️  需配置 |

---

## 平台详细配置

### 1. iOS / macOS

#### 自动配置（已完成）

iOS 和 macOS 系统自带 libcurl，已在 CMakeLists.txt 中配置：

**apple/ios/CMakeLists.txt**:
```cmake
# 包含目录
include_directories(
    ${CMAKE_OSX_SYSROOT}/usr/include  # 包含 curl 头文件
)

# 链接库
target_link_libraries(YXVodPlayer-iOS
    curl  # 系统自带
)
```

**apple/framework/CMakeLists.txt**:
```cmake
# 同样的配置
include_directories(${CMAKE_OSX_SYSROOT}/usr/include)
target_link_libraries(HXCPlayer PUBLIC "-lcurl")
```

**core/src/CMakeLists.txt**:
```cmake
if(APPLE)
    target_link_libraries(hxcplayer_core PUBLIC curl)
endif()
```

#### 验证
```bash
# 检查 curl 库
ls /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib/libcurl*

# 检查 curl 头文件
ls /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/curl/
```

#### 特性
- ✅ 支持 HTTP/HTTPS
- ✅ 支持 SSL/TLS (SecureTransport)
- ✅ 支持 HTTP Range 请求
- ✅ 线程安全
- ✅ iOS 7.0+ / macOS 10.9+

---

### 2. Windows

#### 方法1: 使用 vcpkg（推荐）

**步骤1: 安装 vcpkg**
```powershell
# 克隆 vcpkg
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg

# 引导安装
.\bootstrap-vcpkg.bat

# 集成到系统
.\vcpkg integrate install
```

**步骤2: 安装 curl**
```powershell
# 32位
vcpkg install curl:x86-windows

# 64位
vcpkg install curl:x64-windows

# 静态链接
vcpkg install curl:x64-windows-static
```

**步骤3: CMake 配置**
```powershell
# 使用 vcpkg 工具链
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
```

#### 方法2: 手动下载预编译库

**步骤1: 下载 curl**
- 官网: https://curl.se/windows/
- 选择版本: 例如 `curl-8.5.0-win64-mingw.zip`

**步骤2: 解压到项目**
```
YXVodPlayer/
├── windows-third/
│   └── curl/
│       ├── include/
│       │   └── curl/
│       │       └── curl.h
│       └── lib/
│           └── libcurl.dll.a (或 libcurl.lib)
```

**步骤3: 修改 CMakeLists.txt**
```cmake
# CMakeLists.txt
if(WIN32)
    set(CURL_ROOT "${CMAKE_SOURCE_DIR}/windows-third/curl")
    set(CURL_INCLUDE_DIRS "${CURL_ROOT}/include")
    set(CURL_LIBRARIES "${CURL_ROOT}/lib/libcurl.lib")
    
    target_include_directories(hxcplayer_core PUBLIC ${CURL_INCLUDE_DIRS})
    target_link_libraries(hxcplayer_core PUBLIC ${CURL_LIBRARIES})
endif()
```

#### 当前配置（自动查找）

`core/src/CMakeLists.txt` 已配置自动查找：
```cmake
elseif(WIN32)
    # Windows 需要手动配置 curl（通过 vcpkg 或预编译库）
    find_package(CURL)
    if(CURL_FOUND)
        target_include_directories(hxcplayer_core PUBLIC ${CURL_INCLUDE_DIRS})
        target_link_libraries(hxcplayer_core PUBLIC ${CURL_LIBRARIES})
        message(STATUS "✅ 找到 libcurl: ${CURL_LIBRARIES}")
    else()
        message(WARNING "⚠️  未找到 libcurl，自定义数据源功能将不可用")
        message(WARNING "    Windows 平台请使用 vcpkg 安装: vcpkg install curl")
    endif()
endif()
```

#### 验证
```powershell
# 检查 curl 是否安装
vcpkg list | findstr curl

# CMake 配置时应该看到
-- ✅ 找到 libcurl: C:/vcpkg/installed/x64-windows/lib/libcurl.lib
```

---

### 3. Android

#### 方法1: 使用预编译的 curl 库

**步骤1: 下载 Android curl 库**
```bash
# 使用开源的预编译库
# https://github.com/leenjewel/openssl_for_ios_and_android
```

**步骤2: 集成到 Android 项目**
```
YXVodPlayer/
├── android-third/
│   └── curl/
│       ├── arm64-v8a/
│       │   └── libcurl.a
│       ├── armeabi-v7a/
│       │   └── libcurl.a
│       └── include/
│           └── curl/
```

**步骤3: CMakeLists.txt 配置**
```cmake
if(ANDROID)
    set(CURL_ROOT "${CMAKE_SOURCE_DIR}/android-third/curl")
    set(CURL_INCLUDE_DIRS "${CURL_ROOT}/include")
    
    # 根据架构选择库
    if(${ANDROID_ABI} STREQUAL "arm64-v8a")
        set(CURL_LIBRARIES "${CURL_ROOT}/arm64-v8a/libcurl.a")
    elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
        set(CURL_LIBRARIES "${CURL_ROOT}/armeabi-v7a/libcurl.a")
    endif()
    
    target_include_directories(hxcplayer_core PUBLIC ${CURL_INCLUDE_DIRS})
    target_link_libraries(hxcplayer_core PUBLIC ${CURL_LIBRARIES})
endif()
```

#### 方法2: 使用 conan 包管理器

```bash
# 安装 conan
pip install conan

# 安装 curl
conan install curl/7.87.0@ -s os=Android -s arch=armv8
```

#### 当前配置

`core/src/CMakeLists.txt` 已添加提示：
```cmake
elseif(ANDROID)
    # Android 需要手动配置 curl
    message(WARNING "⚠️  Android 平台需要手动配置 libcurl")
endif()
```

---

### 4. Linux

#### 安装系统库（推荐）

**Debian/Ubuntu**:
```bash
sudo apt-get update
sudo apt-get install libcurl4-openssl-dev
```

**CentOS/RHEL**:
```bash
sudo yum install libcurl-devel
```

**Arch Linux**:
```bash
sudo pacman -S curl
```

#### 当前配置（自动查找）

`core/src/CMakeLists.txt` 已配置：
```cmake
else()
    # Linux 等其他平台，尝试查找系统 curl
    find_package(CURL)
    if(CURL_FOUND)
        target_include_directories(hxcplayer_core PUBLIC ${CURL_INCLUDE_DIRS})
        target_link_libraries(hxcplayer_core PUBLIC ${CURL_LIBRARIES})
    else()
        message(WARNING "⚠️  未找到 libcurl，请安装: sudo apt-get install libcurl4-openssl-dev")
    endif()
endif()
```

#### 验证
```bash
# 检查 curl 开发库
pkg-config --modversion libcurl
pkg-config --libs libcurl
pkg-config --cflags libcurl
```

---

## 编译验证

### 成功标志

编译时应该看到：

```
-- ✅ 找到 libcurl: /path/to/libcurl
[ XX%] Building CXX object core/src/CMakeFiles/hxcplayer_core.dir/hxc_custom_io.cpp.o
[ XX%] Linking CXX static library libhxcplayer_core.a
```

### 失败标志

如果看到：

```
⚠️  未找到 libcurl，自定义数据源功能将不可用
```

说明需要手动配置 curl。

---

## 功能降级策略

如果某个平台无法配置 curl，可以禁用自定义数据源功能：

### 方法1: 条件编译

在 `hxc_custom_io.h` 中：
```cpp
#ifndef HAS_CURL
#warning "libcurl not found, custom data source disabled"
// 禁用相关功能
#endif
```

### 方法2: 运行时检查

在 `PlayerCore::open_with_mode()` 中：
```cpp
case DataSourceMode::CustomHTTP:
#ifdef HAS_CURL
    // 使用自定义数据源
#else
    LOG_ERROR("自定义数据源功能未启用（缺少 libcurl）");
    return ERROR_NOT_SUPPORT;
#endif
```

---

## 推荐的配置方式

| 平台 | 推荐方式 | 理由 |
|------|---------|------|
| **iOS/macOS** | 系统自带 | 无需配置，直接使用 |
| **Windows** | vcpkg | 简单、自动化、版本管理 |
| **Android** | 预编译库 | 减少编译时间，多架构支持 |
| **Linux** | 系统包管理器 | 简单、维护方便 |

---

## Windows vcpkg 详细步骤

### 1. 安装 vcpkg

```powershell
# 1. 克隆仓库
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg

# 2. 引导安装
.\bootstrap-vcpkg.bat

# 3. 集成到 Visual Studio
.\vcpkg integrate install
```

### 2. 安装 curl

```powershell
# 64位动态库（推荐）
vcpkg install curl:x64-windows

# 64位静态库
vcpkg install curl:x64-windows-static

# 32位动态库
vcpkg install curl:x86-windows
```

### 3. CMake 配置

```powershell
# 方法1: 使用 CMake 工具链文件
cmake -B build -S . ^
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# 方法2: 设置环境变量
set CMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake -B build -S .
```

### 4. 构建

```powershell
cmake --build build --config Release
```

---

## 故障排除

### Windows: 找不到 curl

**问题**:
```
⚠️  未找到 libcurl
```

**解决**:
```powershell
# 1. 确认 vcpkg 已安装 curl
vcpkg list | findstr curl

# 2. 使用工具链文件
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# 3. 手动指定路径
cmake -B build -DCURL_INCLUDE_DIR=C:/vcpkg/installed/x64-windows/include ^
               -DCURL_LIBRARY=C:/vcpkg/installed/x64-windows/lib/libcurl.lib
```

### Linux: 找不到 curl

**问题**:
```
⚠️  未找到 libcurl
```

**解决**:
```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev

# CentOS/RHEL
sudo yum install libcurl-devel

# 验证
pkg-config --modversion libcurl
```

### Android: 链接错误

**问题**:
```
undefined reference to `curl_easy_init'
```

**解决**:
- 确保为所有架构（arm64-v8a, armeabi-v7a）提供了 libcurl.a
- 检查 CMakeLists.txt 中的架构判断逻辑
- 确保链接了 libcurl 的依赖（openssl, zlib）

---

## 总结

### 已配置平台

- ✅ **iOS**: 完全配置，可直接使用
- ✅ **macOS**: 完全配置，可直接使用

### 需要配置平台

- ⚠️  **Windows**: 需要安装 vcpkg + curl
- ⚠️  **Android**: 需要集成预编译 curl 库
- ⚠️  **Linux**: 需要安装系统 curl 开发库

### 配置优先级

1. **立即可用**: iOS/macOS（系统自带）
2. **简单配置**: Windows（vcpkg 10分钟）、Linux（apt-get 5分钟）
3. **需要准备**: Android（需要下载/编译 curl 库）

### 下一步

1. Windows 开发者: 安装 vcpkg 和 curl
2. Android 开发者: 准备 curl 预编译库或使用 conan
3. Linux 开发者: 安装 libcurl4-openssl-dev
