# iOS libcurl 头文件配置

## 问题
编译 `hxc_custom_io.cpp` 时报错：
```
'curl/curl.h' file not found
```

## 原因
虽然 iOS 系统自带 libcurl 库，但 CMake 需要显式指定头文件路径才能找到。

curl 头文件位置：
```
${CMAKE_OSX_SYSROOT}/usr/include/curl/
```

其中 `CMAKE_OSX_SYSROOT` 指向 iOS SDK，例如：
```
/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk
```

## 解决方案

在两个 CMakeLists.txt 中添加系统头文件路径。

### 1. apple/ios/CMakeLists.txt

**修改位置**：第55-60行

```cmake
# 包含目录
include_directories(
    ${PROJECT_ROOT}/core/include
    ${FFMPEG_INCLUDE_DIR}
    # curl 头文件（iOS SDK 自带）
    ${CMAKE_OSX_SYSROOT}/usr/include
)
```

### 2. apple/framework/CMakeLists.txt

**修改位置**：第69-73行

```cmake
# 包含目录
include_directories(
    ${PROJECT_ROOT}/core/include
    # curl 头文件（iOS/macOS SDK 自带）
    ${CMAKE_OSX_SYSROOT}/usr/include
)
```

## 为什么使用 CMAKE_OSX_SYSROOT

`CMAKE_OSX_SYSROOT` 是 CMake 自动设置的变量，指向当前目标平台的 SDK：

| 平台 | CMAKE_OSX_SYSROOT 值 |
|------|---------------------|
| iOS 真机 | `.../Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk` |
| iOS 模拟器 | `.../Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk` |
| macOS | `.../SDKs/MacOSX.sdk` |

使用这个变量的好处：
- ✅ 自动适配不同平台（iOS 真机/模拟器/macOS）
- ✅ 自动适配不同 SDK 版本
- ✅ 不需要硬编码路径

## 系统头文件路径包含的内容

`${CMAKE_OSX_SYSROOT}/usr/include/` 包含许多系统库的头文件：

```
${CMAKE_OSX_SYSROOT}/usr/include/
├── curl/           # ✅ libcurl 头文件
│   ├── curl.h
│   ├── easy.h
│   └── ...
├── zlib.h          # zlib 压缩库
├── bzlib.h         # bzip2 压缩库
├── iconv.h         # 字符编码转换
├── sqlite3.h       # SQLite 数据库
└── ...
```

## 完整的头文件搜索路径

编译时，编译器会按以下顺序搜索头文件：

1. **项目头文件**
   - `${PROJECT_ROOT}/core/include`
   - 包含我们的自定义头文件（`hxc_*.h`）

2. **FFmpeg 头文件**
   - `${FFMPEG_INCLUDE_DIR}`
   - 包含 FFmpeg 的头文件（`libavformat/*`, `libavcodec/*` 等）

3. **系统头文件**
   - `${CMAKE_OSX_SYSROOT}/usr/include`
   - 包含系统库的头文件（`curl/*`, `zlib.h`, `iconv.h` 等）

4. **SoundTouch 头文件**（如果启用）
   - `${SOUNDTOUCH_INCLUDE_DIR}`

## 验证

### 1. 检查 curl 头文件是否存在

```bash
# iOS SDK
ls /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk/usr/include/curl/

# 应该看到
curl.h  easy.h  multi.h  ...
```

### 2. 编译时输出

成功配置后，编译输出应该包含：

```
[ XX%] Building CXX object CMakeFiles/YXVodPlayer-iOS.dir/core/src/hxc_custom_io.cpp.o
```

如果看到这行且没有错误，说明头文件找到了。

### 3. 测试代码

在 `hxc_custom_io.cpp` 中可以正常使用：

```cpp
#include <curl/curl.h>

// 使用 curl 函数
CURL* curl = curl_easy_init();
```

## 其他系统库的使用

同样的方式，可以使用其他系统库：

```cpp
#include <zlib.h>       // 压缩
#include <bzlib.h>      // bzip2 压缩
#include <iconv.h>      // 字符编码
#include <sqlite3.h>    // SQLite 数据库
```

都无需额外配置，因为它们都在 `${CMAKE_OSX_SYSROOT}/usr/include` 中。

## 注意事项

### 1. 不要使用绝对路径

❌ **错误**：硬编码路径
```cmake
include_directories(/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS17.2.sdk/usr/include)
```

✅ **正确**：使用变量
```cmake
include_directories(${CMAKE_OSX_SYSROOT}/usr/include)
```

**原因**：
- SDK 路径会随 Xcode 版本变化
- 模拟器和真机使用不同的 SDK
- 使用变量可以自动适配

### 2. 头文件和库要匹配

虽然头文件在 `${CMAKE_OSX_SYSROOT}/usr/include`，但链接库时不需要指定路径：

```cmake
# ✅ 正确：直接链接库名
target_link_libraries(YXVodPlayer-iOS
    curl    # 系统会自动找到 libcurl
)

# ❌ 错误：不要指定完整路径
target_link_libraries(YXVodPlayer-iOS
    ${CMAKE_OSX_SYSROOT}/usr/lib/libcurl.tbd  # 不需要这样
)
```

### 3. 编译选项

如果遇到 curl 相关的编译警告，可以添加：

```cmake
target_compile_options(YXVodPlayer-iOS PRIVATE
    -Wno-deprecated-declarations  # 已有
    # 如果需要，可以添加 curl 相关选项
)
```

## 故障排除

### 问题1：curl.h not found

**检查**：
```bash
echo $SDKROOT
ls $SDKROOT/usr/include/curl/
```

**解决**：
- 确保 Xcode 已安装
- 确保 Command Line Tools 已安装
- 运行 `xcode-select --install`

### 问题2：Undefined symbols for curl_*

**原因**：头文件找到了，但库没有链接

**解决**：
```cmake
target_link_libraries(YXVodPlayer-iOS
    curl  # 确保添加了这一行
)
```

### 问题3：Architecture mismatch

**原因**：curl 库的架构与目标不匹配

**解决**：
```cmake
# 确保设置了正确的架构
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")  # iOS 真机
# 或
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")  # iOS 模拟器
```

## 总结

### 修改内容

| 文件 | 修改位置 | 添加内容 |
|------|---------|---------|
| `apple/ios/CMakeLists.txt` | 第58行 | `${CMAKE_OSX_SYSROOT}/usr/include` |
| `apple/framework/CMakeLists.txt` | 第72行 | `${CMAKE_OSX_SYSROOT}/usr/include` |

### 完整配置

**源文件**：✅ `hxc_custom_io.cpp` 已添加  
**头文件**：✅ `hxc_custom_io.h` 已添加  
**包含目录**：✅ `${CMAKE_OSX_SYSROOT}/usr/include` 已添加  
**链接库**：✅ `curl` 已添加  

现在可以正常编译和使用 libcurl 了！
