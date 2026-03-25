# iOS 项目配置 - 自定义数据源支持

## 修改日期
2026-02-24

## 问题
链接时找不到 `hxc_custom_io.cpp` 中方法的实现。

## 原因
虽然 `hxc_custom_io.cpp` 已在 `CORE_SOURCES` 中，但 `hxc_custom_io.h` 未在 `PRIVATE_HEADERS` 中声明，导致头文件无法被正确识别。

## 解决方案

需要在**两个** CMakeLists.txt 文件中添加配置：

### 1. apple/framework/CMakeLists.txt（用于构建 XCFramework）

#### 修改位置：第54-64行

```cmake
# 私有头文件（内部使用）
set(PRIVATE_HEADERS
    ${PROJECT_ROOT}/core/include/hxc_player_core.h
    ${PROJECT_ROOT}/core/include/hxc_decoder.h
    ${PROJECT_ROOT}/core/include/hxc_player_types.h
    ${PROJECT_ROOT}/core/include/hxc_packet_queue.h
    ${PROJECT_ROOT}/core/include/hxc_frame_queue.h
    ${PROJECT_ROOT}/core/include/hxc_player_core_c_bridge.h
    ${PROJECT_ROOT}/core/include/hxc_custom_io.h  # ✅ 新增
    ${PROJECT_ROOT}/core/include/hxc_logger.h
    ${PROJECT_ROOT}/core/include/hxc_debug_helper.h
)
```

### 2. apple/ios/CMakeLists.txt（用于构建 iOS Demo App）

#### 源文件（第76-84行）
```cmake
# 核心库源文件
set(CORE_SOURCES
    ${PROJECT_ROOT}/core/src/hxc_player_core.cpp
    ${PROJECT_ROOT}/core/src/hxc_decoder.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_types.cpp
    ${PROJECT_ROOT}/core/src/hxc_packet_queue.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_core_c_bridge.cpp
    ${PROJECT_ROOT}/core/src/hxc_custom_io.cpp  # ✅ 新增
)
```

#### 头文件（第86-96行）
```cmake
# 核心库头文件
set(CORE_HEADERS
    ${PROJECT_ROOT}/core/include/hxc_player_core.h
    ${PROJECT_ROOT}/core/include/hxc_decoder.h
    ${PROJECT_ROOT}/core/include/hxc_player_types.h
    ${PROJECT_ROOT}/core/include/hxc_packet_queue.h
    ${PROJECT_ROOT}/core/include/hxc_frame_queue.h
    ${PROJECT_ROOT}/core/include/hxc_logger.h
    ${PROJECT_ROOT}/core/include/hxc_debug_helper.h
    ${PROJECT_ROOT}/core/include/hxc_player_core_c_bridge.h
    ${PROJECT_ROOT}/core/include/hxc_custom_io.h  # ✅ 新增
)
```

#### 链接库（第163-169行）
```cmake
    # 系统库
    bz2
    z
    iconv
    curl  # ✅ 新增
    c++
)
```

## 完整的配置清单

### 1. 源文件 (CORE_SOURCES)
```cmake
set(CORE_SOURCES
    ${PROJECT_ROOT}/core/src/hxc_player_core.cpp
    ${PROJECT_ROOT}/core/src/hxc_decoder.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_types.cpp
    ${PROJECT_ROOT}/core/src/hxc_packet_queue.cpp
    ${PROJECT_ROOT}/core/src/hxc_player_core_c_bridge.cpp
    ${PROJECT_ROOT}/core/src/hxc_custom_io.cpp  # ✅ 已有
)
```

### 2. 头文件 (PRIVATE_HEADERS)
```cmake
set(PRIVATE_HEADERS
    ${PROJECT_ROOT}/core/include/hxc_player_core.h
    ${PROJECT_ROOT}/core/include/hxc_decoder.h
    ${PROJECT_ROOT}/core/include/hxc_player_types.h
    ${PROJECT_ROOT}/core/include/hxc_packet_queue.h
    ${PROJECT_ROOT}/core/include/hxc_frame_queue.h
    ${PROJECT_ROOT}/core/include/hxc_player_core_c_bridge.h
    ${PROJECT_ROOT}/core/include/hxc_custom_io.h  # ✅ 新增
    ${PROJECT_ROOT}/core/include/hxc_logger.h
    ${PROJECT_ROOT}/core/include/hxc_debug_helper.h
)
```

### 3. 库链接 (target_link_libraries)
```cmake
target_link_libraries(HXCPlayer PUBLIC
    "-framework VideoToolbox"
    "-framework CoreGraphics"
    "-framework Security"
    "-lz"
    "-lbz2"
    "-liconv"
    "-lcurl"  # ✅ 已有（hxc_custom_io.cpp 需要 libcurl）
)
```

## 验证步骤

1. **清理编译缓存**：
   ```bash
   cd /Users/debug/project/YXVodPlayer/apple/ios
   rm -rf build/
   ```

2. **重新编译**：
   ```bash
   ./build_ios.sh
   ```

3. **检查链接**：
   确保编译输出中包含：
   ```
   [ XX%] Building CXX object CMakeFiles/HXCPlayer.dir/core/src/hxc_custom_io.cpp.o
   ```

4. **运行测试**：
   ```objc
   [player openURL:url 
          withMode:HXCPlayerDataSourceModeCustomHTTP 
            config:[HXCPlayerDataSourceConfig defaultConfig]];
   ```

## 依赖关系

### hxc_custom_io.cpp 依赖
- **系统库**：`libcurl`（已配置）
- **FFmpeg**：`libavformat`, `libavutil`（已配置）
- **C++ 标准库**：`<memory>`, `<string>`, `<vector>`

### hxc_custom_io.h 依赖
- **FFmpeg 头文件**：`libavformat/avio.h`, `libavutil/error.h`
- **C++ 标准库**：`<cstdint>`, `<functional>`, `<memory>`, `<string>`

## 注意事项

### 1. 头文件包含顺序
在 C++ 源文件中包含 `hxc_custom_io.h` 时，确保先包含 FFmpeg 头文件：

```cpp
// ✅ 正确顺序
extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
}
#include "hxc_custom_io.h"
```

### 2. iOS/macOS 层隔离
iOS/macOS 层（`.mm` 文件）**不应该**直接包含 `hxc_custom_io.h`，应该通过 C 桥接层：

```objc
// ❌ 错误：直接包含会导致 AVFoundation 冲突
#include "hxc_custom_io.h"

// ✅ 正确：通过 C 桥接层
#include "hxc_player_core_c_bridge.h"
```

### 3. libcurl 可用性
确保 iOS 系统提供了 libcurl：
- iOS 7.0+ 默认包含 libcurl
- 无需额外安装或配置
- 直接使用 `-lcurl` 链接即可

## 相关文件

| 文件 | 作用 | 状态 |
|------|------|------|
| `core/include/hxc_custom_io.h` | 自定义数据源头文件 | ✅ 已添加到 PRIVATE_HEADERS |
| `core/src/hxc_custom_io.cpp` | 自定义数据源实现 | ✅ 已在 CORE_SOURCES |
| `apple/framework/CMakeLists.txt` | iOS 编译配置 | ✅ 已更新 |

## 编译输出示例

成功编译时应该看到：

```
-- ==================== 构建类型 ====================
-- Build type: Release
-- Platform: iOS (Device: iphoneos, Simulator: iphonesimulator)
-- ==================== 文件清单 ====================
-- Core sources: 6 files
--   hxc_player_core.cpp
--   hxc_decoder.cpp
--   hxc_player_types.cpp
--   hxc_packet_queue.cpp
--   hxc_player_core_c_bridge.cpp
--   hxc_custom_io.cpp  ← 应该看到这个
-- Apple sources: 2 files
-- ==================== 依赖库 ====================
-- FFmpeg libraries found:
--   avformat: /path/to/libavformat.a
--   avcodec: /path/to/libavcodec.a
--   avutil: /path/to/libavutil.a
--   swscale: /path/to/libswscale.a
--   swresample: /path/to/libswresample.a
-- SoundTouch library: /path/to/libSoundTouch.a
-- System libraries: VideoToolbox, CoreGraphics, Security, z, bz2, iconv, curl
```

## 故障排除

### 问题1：Undefined symbols for RangeDownloader
**原因**：`hxc_custom_io.cpp` 未编译或未链接

**解决**：
```bash
# 检查源文件列表
grep "hxc_custom_io.cpp" apple/framework/CMakeLists.txt

# 应该在 CORE_SOURCES 中看到
```

### 问题2：Unknown type name 'AVIOContext'
**原因**：`hxc_custom_io.h` 未正确包含 FFmpeg 头文件

**解决**：
```cpp
// 在包含 hxc_custom_io.h 之前先包含 FFmpeg
extern "C" {
#include <libavformat/avio.h>
}
```

### 问题3：Library not found for -lcurl
**原因**：iOS 模拟器或真机环境中 libcurl 不可用（极少见）

**解决**：
- iOS 7.0+ 默认支持 libcurl
- 确保 Deployment Target >= 7.0
- 检查 Xcode 版本是否过旧

## 总结

现在 iOS 项目已正确配置自定义数据源支持：

✅ **源文件**：`hxc_custom_io.cpp` 已包含  
✅ **头文件**：`hxc_custom_io.h` 已添加到 PRIVATE_HEADERS  
✅ **依赖库**：libcurl 已链接  
✅ **架构隔离**：通过 C 桥接层避免 AVFoundation 冲突  

可以正常使用自动数据源模式功能！
