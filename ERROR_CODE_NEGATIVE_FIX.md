# 错误码值统一为负数 - 修正记录

## 修正时间
2026-02-24

## 问题描述

之前的同步中，C 桥接层和 Objective-C 层使用的是**正数**错误码，但 C++ 层使用的是**负数**错误码，导致三层错误码值不一致。

### 之前的错误设计

```cpp
// C++ 层 (hxc_player_core.h)
ERROR_INVALID_URL = -1001

// C 桥接层 (hxc_player_core_c_bridge.h) - ❌ 错误
PLAYER_ERROR_INVALID_URL = 1

// ObjC 层 (HXCPlayerControl.h) - ❌ 错误
HXCPlayerErrorInvalidURL = 1
```

## 修正方案

**统一三层错误码值，全部使用负数**，与 C++ 层保持完全一致。

### 修正后的正确设计

```cpp
// C++ 层 (hxc_player_core.h)
ERROR_INVALID_URL = -1001

// C 桥接层 (hxc_player_core_c_bridge.h) - ✅ 正确
PLAYER_ERROR_INVALID_URL = -1001

// ObjC 层 (HXCPlayerControl.h) - ✅ 正确
HXCPlayerErrorInvalidURL = -1001
```

---

## 修正的文件

### 1. `/core/include/hxc_player_core_c_bridge.h`

**修改前**：
```c
typedef enum {
    PLAYER_ERROR_NONE = 0,
    PLAYER_ERROR_INVALID_URL = 1,                   // ❌ 正数
    PLAYER_ERROR_OPEN_INPUT_FAILED = 2,             // ❌ 正数
    // ...
    PLAYER_ERROR_NET_CONNECTION_TIMEOUT = 2001,     // ❌ 正数
    PLAYER_ERROR_HTTP_BAD_REQUEST = 3001,           // ❌ 正数
    PLAYER_ERROR_UNKNOWN = 999,                     // ❌ 正数
} PlayerErrorCodeC;
```

**修改后**：
```c
typedef enum {
    PLAYER_ERROR_NONE = 0,
    PLAYER_ERROR_INVALID_URL = -1001,               // ✅ 负数
    PLAYER_ERROR_OPEN_INPUT_FAILED = -1002,         // ✅ 负数
    // ...
    PLAYER_ERROR_NET_CONNECTION_TIMEOUT = -2001,    // ✅ 负数
    PLAYER_ERROR_HTTP_BAD_REQUEST = -3001,          // ✅ 负数
    PLAYER_ERROR_UNKNOWN = -1099,                   // ✅ 负数
} PlayerErrorCodeC;
```

### 2. `/apple/HXCPlayerControl.h`

**修改前**：
```objc
typedef NS_ENUM(NSInteger, HXCPlayerErrorCode) {
    HXCPlayerErrorNone = 0,
    HXCPlayerErrorInvalidURL = 1,                   // ❌ 正数
    HXCPlayerErrorOpenInputFailed = 2,              // ❌ 正数
    // ...
    HXCPlayerErrorNetConnectionTimeout = 2001,      // ❌ 正数
    HXCPlayerErrorHTTPBadRequest = 3001,            // ❌ 正数
    HXCPlayerErrorUnknown = 999,                    // ❌ 正数
};
```

**修改后**：
```objc
typedef NS_ENUM(NSInteger, HXCPlayerErrorCode) {
    HXCPlayerErrorNone = 0,
    HXCPlayerErrorInvalidURL = -1001,               // ✅ 负数
    HXCPlayerErrorOpenInputFailed = -1002,          // ✅ 负数
    // ...
    HXCPlayerErrorNetConnectionTimeout = -2001,     // ✅ 负数
    HXCPlayerErrorHTTPBadRequest = -3001,           // ✅ 负数
    HXCPlayerErrorUnknown = -1099,                  // ✅ 负数
};
```

---

## 修正的错误码值对照表

| 错误码名称 | 修改前（错误） | 修改后（正确） |
|-----------|--------------|--------------|
| 无效的 URL | `1` | `-1001` |
| 打开输入失败 | `2` | `-1002` |
| 查找流信息失败 | `3` | `-1003` |
| 没有视频流 | `4` | `-1004` |
| 没有音频流 | `5` | `-1005` |
| 找不到解码器 | `6` | `-1006` |
| 打开解码器失败 | `7` | `-1007` |
| 分配上下文失败 | `8` | `-1008` |
| SDL 初始化失败 | `9` | `-1009` |
| 音频设备打开失败 | `10` | `-1010` |
| Seek 操作失败 | `11` | `-1011` |
| 读取帧失败 | `12` | `-1012` |
| 解码失败 | `13` | `-1013` |
| 内存不足 | `14` | `-1014` |
| 无效数据 | `18` | `-1018` |
| 不支持的格式 | `19` | `-1019` |
| 未知错误 | `999` | `-1099` |
| 网络连接超时 | `2001` | `-2001` |
| 服务器拒绝连接 | `2002` | `-2002` |
| 网络不可达 | `2003` | `-2003` |
| HTTP 400 | `3001` | `-3001` |
| HTTP 404 | `3002` | `-3002` |
| HTTP 5xx | `3003` | `-3003` |
| HTTP 401 | `3004` | `-3004` |
| HTTP 403 | `3005` | `-3005` |

---

## 为什么使用负数？

### 1. 与 C++ 层保持一致
C++ 层从一开始就使用负数，这样三层的错误码值完全一致，不需要任何转换。

### 2. 与 FFmpeg 风格一致
FFmpeg 的错误码都是负数，例如：
```c
AVERROR_EOF = -541478725
AVERROR(ENOMEM) = -12
AVERROR(EINVAL) = -22
```

使用负数可以与 FFmpeg 错误码风格保持一致。

### 3. 避免与成功返回值混淆
在很多 C/C++ API 中：
- 成功返回 `0` 或**正数**
- 失败返回**负数**

使用负数错误码可以避免与成功返回值混淆。

### 4. 简化错误处理逻辑
```c
int result = player_core_open(handle, url);
if (result < 0) {
    // 错误处理（任何负数都是错误）
    handle_error(result);
}
```

---

## 影响的代码

### 如果之前有代码依赖正数错误码

**需要修改的代码示例**：

#### 修改前（错误）
```objc
- (void)player:(HXCPlayerControl *)player didFailWithError:(NSError *)error {
    if (error.code == 1) {  // ❌ 使用正数
        NSLog(@"无效的 URL");
    }
}
```

#### 修改后（正确）
```objc
- (void)player:(HXCPlayerControl *)player didFailWithError:(NSError *)error {
    if (error.code == HXCPlayerErrorInvalidURL) {  // ✅ 使用枚举（-1001）
        NSLog(@"无效的 URL");
    }
    
    // 或者
    if (error.code == -1001) {  // ✅ 使用负数
        NSLog(@"无效的 URL");
    }
}
```

**建议**：始终使用枚举名称而不是硬编码的数字。

---

## iOS 特有错误码也已修正

iOS 层有两个特有的错误码，也已修正为负数：

| 错误码名称 | 修改前 | 修改后 |
|-----------|--------|--------|
| 不支持画中画 | `15` | `-1015` |
| 音频会话配置失败 | `16` | `-1016` |

---

## 错误码范围规划（使用负数）

| 范围 | 用途 | 说明 |
|------|------|------|
| `0` | 无错误 | 成功 |
| `-1001 ~ -1099` | 通用错误 | 播放器基础错误 |
| `-2001 ~ -2999` | 网络错误 | 网络连接相关 |
| `-3001 ~ -3999` | HTTP 错误 | HTTP 协议相关 |
| `-4001 ~ -4999` | 保留 | 预留给其他协议（RTSP、RTMP） |
| 其他负数 | FFmpeg 错误 | FFmpeg 原始错误码 |

---

## 验证

### 编译验证
✅ 已通过 linter 检查，无编译错误

### 代码示例验证

#### C++ 层
```cpp
int PlayerCore::open(const std::string& filename) {
    if (/* 超时 */) {
        return ERROR_NET_CONNECTION_TIMEOUT;  // -2001
    }
    return ERROR_NONE;  // 0
}
```

#### C 桥接层
```c
int player_core_open(PlayerCoreHandle* handle, const char* url) {
    int result = cpp_player->open(url);
    // result 是负数（如 -2001），直接返回即可
    return result;
}
```

#### Objective-C 层
```objc
- (BOOL)open:(NSString *)url {
    int result = player_core_open(_handle, [url UTF8String]);
    if (result < 0) {
        NSError *error = [NSError errorWithDomain:@"HXCPlayer"
                                             code:result  // -2001
                                         userInfo:@{NSLocalizedDescriptionKey: @"网络连接超时"}];
        [self.delegate player:self didFailWithError:error];
        return NO;
    }
    return YES;
}
```

---

## 总结

### 修正前的问题
- ❌ C++ 层使用负数（`-1001`）
- ❌ C 层使用正数（`1`）
- ❌ ObjC 层使用正数（`1`）
- ❌ 错误码值不一致，需要转换

### 修正后的优点
- ✅ 三层全部使用负数（`-1001`）
- ✅ 错误码值完全一致
- ✅ 无需转换，直接传递
- ✅ 与 FFmpeg 风格一致
- ✅ 符合 C/C++ 错误处理惯例

### 修正的文件数量
- 修正文件：2 个
- 更新文档：2 个
- 错误码数量：27 个（全部修正为负数）

---

**状态**: ✅ 已完成  
**验证**: ✅ 无编译错误  
**文档**: ✅ 已更新  
**测试**: 建议重新测试错误处理逻辑

---

## 相关文档

- `ERROR_CODE_SYNC.md` - 错误码同步详细说明
- `ERROR_CODE_QUICK_REF.md` - 快速对照表（已更新为负数）
- `ERROR_CALLBACK_SUMMARY.md` - 错误回调总结
