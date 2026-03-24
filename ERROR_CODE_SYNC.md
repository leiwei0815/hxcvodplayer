# 错误码同步记录

## 同步日期
2026-02-24

## 同步内容

从 `hxc_player_core.h` 的 `PlayerErrorCode` 同步到：
1. `hxc_player_core_c_bridge.h` 的 `PlayerErrorCodeC`
2. `apple/HXCPlayerControl.h` 的 `HXCPlayerErrorCode`

---

## 新增的错误码

### 1. 通用错误 (1-999)

| 错误码值 | C++ 名称 | C 名称 | Objective-C 名称 | 说明 |
|---------|---------|--------|-----------------|------|
| 18 | `ERROR_INPUT_INVALID_DATA` | `PLAYER_ERROR_INPUT_INVALID_DATA` | `HXCPlayerErrorInputInvalidData` | 无效数据 |
| 19 | `ERROR_NOT_SUPPORT` | `PLAYER_ERROR_NOT_SUPPORT` | `HXCPlayerErrorNotSupport` | 不支持的格式或协议 |

### 2. 网络相关错误 (2001-2999)

| 错误码值 | C++ 名称 | C 名称 | Objective-C 名称 | 说明 |
|---------|---------|--------|-----------------|------|
| 2001 | `ERROR_NET_CONNECTION_TIMEOUT` | `PLAYER_ERROR_NET_CONNECTION_TIMEOUT` | `HXCPlayerErrorNetConnectionTimeout` | 网络连接超时 |
| 2002 | `ERROR_NET_CONNECTION_REFUSED` | `PLAYER_ERROR_NET_CONNECTION_REFUSED` | `HXCPlayerErrorNetConnectionRefused` | 服务器拒绝连接 |
| 2003 | `ERROR_NET_UNREACHABLE` | `PLAYER_ERROR_NET_UNREACHABLE` | `HXCPlayerErrorNetUnreachable` | 网络不可达 |

### 3. HTTP 相关错误 (3001-3999)

| 错误码值 | C++ 名称 | C 名称 | Objective-C 名称 | 说明 |
|---------|---------|--------|-----------------|------|
| 3001 | `ERROR_HTTP_BAD_REQUEST` | `PLAYER_ERROR_HTTP_BAD_REQUEST` | `HXCPlayerErrorHTTPBadRequest` | HTTP 请求错误（400） |
| 3002 | `ERROR_HTTP_NOT_FOUND` | `PLAYER_ERROR_HTTP_NOT_FOUND` | `HXCPlayerErrorHTTPNotFound` | HTTP 404 文件不存在 |
| 3003 | `ERROR_HTTP_SERVER_ERROR` | `PLAYER_ERROR_HTTP_SERVER_ERROR` | `HXCPlayerErrorHTTPServerError` | HTTP 服务器错误（5xx） |
| 3004 | `ERROR_HTTP_UNAUTHORIZED` | `PLAYER_ERROR_HTTP_UNAUTHORIZED` | `HXCPlayerErrorHTTPUnauthorized` | 需要身份验证（401） |
| 3005 | `ERROR_HTTP_FORBIDDEN` | `PLAYER_ERROR_HTTP_FORBIDDEN` | `HXCPlayerErrorHTTPForbidden` | 访问被禁止（403） |

---

## 错误码映射关系

### C++ → C 桥接层映射规则

```cpp
// C++ (hxc_player_core.h)
enum PlayerErrorCode {
    ERROR_XXX = -1xxx,  // 负数（对应 C++ 枚举）
};

// C (hxc_player_core_c_bridge.h)
typedef enum {
    PLAYER_ERROR_XXX = xxx,  // 正数（方便 C 使用）
} PlayerErrorCodeC;
```

**注意**：C++ 层使用负数错误码（与 FFmpeg 保持一致），C 桥接层使用正数错误码（更符合 C 惯例）。

### C 桥接层 → Objective-C 映射规则

```c
// C (hxc_player_core_c_bridge.h)
typedef enum {
    PLAYER_ERROR_XXX_YYY = value,
} PlayerErrorCodeC;

// Objective-C (HXCPlayerControl.h)
typedef NS_ENUM(NSInteger, HXCPlayerErrorCode) {
    HXCPlayerErrorXxxYyy = value,  // 驼峰命名
};
```

**命名规则**：
- C 层：`PLAYER_ERROR_` + 大写下划线命名
- ObjC 层：`HXCPlayerError` + 驼峰命名

---

## 错误码范围规划

| 范围 | 用途 | 说明 |
|------|------|------|
| 0 | 无错误 | `ERROR_NONE` / `PLAYER_ERROR_NONE` / `HXCPlayerErrorNone` |
| 1-999 | 通用错误 | 播放器通用错误（打开失败、解码失败等） |
| 1000-1999 | 保留 | 预留给未来的通用错误 |
| 2000-2999 | 网络错误 | 网络连接相关错误 |
| 3000-3999 | HTTP 错误 | HTTP 协议相关错误 |
| 4000-4999 | 保留 | 预留给未来的协议错误（如 RTSP、RTMP） |
| 5000-9999 | 保留 | 预留 |
| 负数 | FFmpeg 错误 | FFmpeg 原始错误码（通过 `av_strerror()` 解析） |

---

## 使用示例

### C++ 层使用

```cpp
// 返回错误码
int open(const std::string& filename) {
    if (/* 超时 */) {
        return ERROR_NET_CONNECTION_TIMEOUT;
    }
    if (/* 404 */) {
        return ERROR_HTTP_NOT_FOUND;
    }
    return ERROR_NONE;
}
```

### C 桥接层使用

```c
// 转换错误码
PlayerErrorCodeC convert_error(int cpp_error) {
    switch (cpp_error) {
        case -2001: return PLAYER_ERROR_NET_CONNECTION_TIMEOUT;
        case -3002: return PLAYER_ERROR_HTTP_NOT_FOUND;
        default: return PLAYER_ERROR_UNKNOWN;
    }
}
```

### Objective-C 层使用

```objc
- (void)player:(HXCPlayerControl *)player didFailWithError:(NSError *)error {
    HXCPlayerErrorCode errorCode = error.code;
    
    switch (errorCode) {
        case HXCPlayerErrorNetConnectionTimeout:
            NSLog(@"网络连接超时");
            break;
        case HXCPlayerErrorHTTPNotFound:
            NSLog(@"视频不存在（404）");
            break;
        case HXCPlayerErrorInputInvalidData:
            NSLog(@"无效的视频格式");
            break;
        case HXCPlayerErrorNotSupport:
            NSLog(@"不支持的格式或协议");
            break;
        default:
            NSLog(@"未知错误: %ld", (long)errorCode);
            break;
    }
}
```

---

## 相关文件

| 文件路径 | 错误码枚举名称 | 语言 |
|---------|---------------|------|
| `/core/include/hxc_player_core.h` | `PlayerErrorCode` | C++ |
| `/core/include/hxc_player_core_c_bridge.h` | `PlayerErrorCodeC` | C |
| `/apple/HXCPlayerControl.h` | `HXCPlayerErrorCode` | Objective-C |
| `/apple/macos/HXCPlayerControl.h` | _(没有错误码定义)_ | Objective-C |

**注意**：`apple/macos/HXCPlayerControl.h` 没有定义错误码枚举，它使用的是 `apple/HXCPlayerControl.h` 中的定义。

---

## 同步检查清单

为了确保三层错误码保持同步，每次新增错误码时需要检查：

- [x] C++ 层 `PlayerErrorCode` 已定义
- [x] C 桥接层 `PlayerErrorCodeC` 已同步
- [x] Objective-C 层 `HXCPlayerErrorCode` 已同步
- [x] 错误码值一致（除了正负号差异）
- [x] 错误码注释清晰
- [x] 命名规范符合各层惯例

---

## 注意事项

### 1. 错误码值的正负号

- **C++ 层**：使用**负数**（如 `-1001`, `-2001`），与 FFmpeg 错误码风格一致
- **C 层**：使用**正数**（如 `1`, `2001`），更符合 C 语言惯例
- **ObjC 层**：使用**正数**（与 C 层相同）

### 2. FFmpeg 错误码

三个层都保留了 FFmpeg 原始错误码的注释说明：
```
// FFmpeg 错误码范围 (负数)
// 例如：AVERROR_EOF, AVERROR(ENOMEM), AVERROR(EINVAL) 等
```

这些错误码不在枚举中定义，而是直接使用 FFmpeg 的值。

### 3. 命名规范

| 层 | 前缀 | 命名风格 | 示例 |
|----|------|---------|------|
| C++ | `ERROR_` | 大写下划线 | `ERROR_NET_CONNECTION_TIMEOUT` |
| C | `PLAYER_ERROR_` | 大写下划线 | `PLAYER_ERROR_NET_CONNECTION_TIMEOUT` |
| ObjC | `HXCPlayerError` | 驼峰 | `HXCPlayerErrorNetConnectionTimeout` |

### 4. 特殊错误码

Objective-C 层有两个特殊错误码（iOS 特有）：
- `HXCPlayerErrorNotSupportPIPPlayer = 15` - 不支持画中画
- `HXCPlayerErrorAudioSessionConfigFail = 16` - 音频会话配置失败

这两个错误码**没有**在 C++ 和 C 层定义，因为它们是 iOS 平台特有的。

---

## 历史记录

| 日期 | 操作 | 说明 |
|------|------|------|
| 2026-02-24 | 同步错误码 | 从 C++ 层新增 10 个错误码同步到 C 和 ObjC 层 |

---

## 未来扩展建议

### 1. 增加协议错误码范围 (4000-4999)

```cpp
// RTSP 相关错误
ERROR_RTSP_CONNECTION_FAILED = -4001,
ERROR_RTSP_TIMEOUT = -4002,

// RTMP 相关错误  
ERROR_RTMP_CONNECTION_FAILED = -4101,
ERROR_RTMP_HANDSHAKE_FAILED = -4102,
```

### 2. 增加硬件解码错误 (5000-5999)

```cpp
ERROR_HW_DECODER_NOT_AVAILABLE = -5001,
ERROR_HW_DECODER_INIT_FAILED = -5002,
ERROR_HW_DECODER_NOT_SUPPORT_FORMAT = -5003,
```

### 3. 增加 DRM 错误 (6000-6999)

```cpp
ERROR_DRM_NOT_SUPPORT = -6001,
ERROR_DRM_LICENSE_EXPIRED = -6002,
ERROR_DRM_DECRYPT_FAILED = -6003,
```

---

## 总结

✅ **已完成**：三层错误码完全同步  
✅ **错误码数量**：从 15 个增加到 25 个  
✅ **新增类别**：网络错误（3个）+ HTTP 错误（5个）+ 通用错误（2个）  
✅ **命名一致**：符合各层命名规范  
✅ **注释清晰**：每个错误码都有中文说明  

现在三个层的错误码定义已经完全同步，可以正常使用了！
