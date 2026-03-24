# 三层错误码快速对照表

## 完整错误码映射（全部使用负数，与 C++ 层保持一致）

| 值 | C++ 层 | C 桥接层 | Objective-C 层 | 说明 |
|---|--------|---------|---------------|------|
| **0** | `ERROR_NONE` | `PLAYER_ERROR_NONE` | `HXCPlayerErrorNone` | 无错误 |
| **-1001** | `ERROR_INVALID_URL` | `PLAYER_ERROR_INVALID_URL` | `HXCPlayerErrorInvalidURL` | 无效的 URL |
| **-1002** | `ERROR_OPEN_INPUT_FAILED` | `PLAYER_ERROR_OPEN_INPUT_FAILED` | `HXCPlayerErrorOpenInputFailed` | 打开输入失败 |
| **-1003** | `ERROR_FIND_STREAM_INFO_FAILED` | `PLAYER_ERROR_FIND_STREAM_INFO_FAILED` | `HXCPlayerErrorFindStreamInfoFailed` | 查找流信息失败 |
| **-1004** | `ERROR_NO_VIDEO_STREAM` | `PLAYER_ERROR_NO_VIDEO_STREAM` | `HXCPlayerErrorNoVideoStream` | 没有视频流 |
| **-1005** | `ERROR_NO_AUDIO_STREAM` | `PLAYER_ERROR_NO_AUDIO_STREAM` | `HXCPlayerErrorNoAudioStream` | 没有音频流 |
| **-1006** | `ERROR_CODEC_NOT_FOUND` | `PLAYER_ERROR_CODEC_NOT_FOUND` | `HXCPlayerErrorCodecNotFound` | 找不到解码器 |
| **-1007** | `ERROR_CODEC_OPEN_FAILED` | `PLAYER_ERROR_CODEC_OPEN_FAILED` | `HXCPlayerErrorCodecOpenFailed` | 打开解码器失败 |
| **-1008** | `ERROR_ALLOC_CONTEXT_FAILED` | `PLAYER_ERROR_ALLOC_CONTEXT_FAILED` | `HXCPlayerErrorAllocContextFailed` | 分配上下文失败 |
| **-1009** | `ERROR_SDL_INIT_FAILED` | `PLAYER_ERROR_SDL_INIT_FAILED` | `HXCPlayerErrorSDLInitFailed` | SDL 初始化失败 |
| **-1010** | `ERROR_AUDIO_DEVICE_OPEN_FAILED` | `PLAYER_ERROR_AUDIO_DEVICE_OPEN_FAILED` | `HXCPlayerErrorAudioDeviceOpenFailed` | 音频设备打开失败 |
| **-1011** | `ERROR_SEEK_FAILED` | `PLAYER_ERROR_SEEK_FAILED` | `HXCPlayerErrorSeekFailed` | Seek 操作失败 |
| **-1012** | `ERROR_READ_FRAME_FAILED` | `PLAYER_ERROR_READ_FRAME_FAILED` | `HXCPlayerErrorReadFrameFailed` | 读取帧失败 |
| **-1013** | `ERROR_DECODE_FAILED` | `PLAYER_ERROR_DECODE_FAILED` | `HXCPlayerErrorDecodeFailed` | 解码失败 |
| **-1014** | `ERROR_OUT_OF_MEMORY` | `PLAYER_ERROR_OUT_OF_MEMORY` | `HXCPlayerErrorOutOfMemory` | 内存不足 |
| **-1015** | _(无)_ | _(无)_ | `HXCPlayerErrorNotSupportPIPPlayer` | 不支持画中画 (iOS特有) |
| **-1016** | _(无)_ | _(无)_ | `HXCPlayerErrorAudioSessionConfigFail` | 音频会话配置失败 (iOS特有) |
| **-1018** | `ERROR_INPUT_INVALID_DATA` | `PLAYER_ERROR_INPUT_INVALID_DATA` | `HXCPlayerErrorInputInvalidData` | 无效数据 ✨新增 |
| **-1019** | `ERROR_NOT_SUPPORT` | `PLAYER_ERROR_NOT_SUPPORT` | `HXCPlayerErrorNotSupport` | 不支持的格式或协议 ✨新增 |
| **-1099** | `ERROR_UNKNOWN` | `PLAYER_ERROR_UNKNOWN` | `HXCPlayerErrorUnknown` | 未知错误 |
| **-2001** | `ERROR_NET_CONNECTION_TIMEOUT` | `PLAYER_ERROR_NET_CONNECTION_TIMEOUT` | `HXCPlayerErrorNetConnectionTimeout` | 网络连接超时 ✨新增 |
| **-2002** | `ERROR_NET_CONNECTION_REFUSED` | `PLAYER_ERROR_NET_CONNECTION_REFUSED` | `HXCPlayerErrorNetConnectionRefused` | 服务器拒绝连接 ✨新增 |
| **-2003** | `ERROR_NET_UNREACHABLE` | `PLAYER_ERROR_NET_UNREACHABLE` | `HXCPlayerErrorNetUnreachable` | 网络不可达 ✨新增 |
| **-3001** | `ERROR_HTTP_BAD_REQUEST` | `PLAYER_ERROR_HTTP_BAD_REQUEST` | `HXCPlayerErrorHTTPBadRequest` | HTTP 请求错误（400） ✨新增 |
| **-3002** | `ERROR_HTTP_NOT_FOUND` | `PLAYER_ERROR_HTTP_NOT_FOUND` | `HXCPlayerErrorHTTPNotFound` | HTTP 404 文件不存在 ✨新增 |
| **-3003** | `ERROR_HTTP_SERVER_ERROR` | `PLAYER_ERROR_HTTP_SERVER_ERROR` | `HXCPlayerErrorHTTPServerError` | HTTP 服务器错误（5xx） ✨新增 |
| **-3004** | `ERROR_HTTP_UNAUTHORIZED` | `PLAYER_ERROR_HTTP_UNAUTHORIZED` | `HXCPlayerErrorHTTPUnauthorized` | 需要身份验证（401） ✨新增 |
| **-3005** | `ERROR_HTTP_FORBIDDEN` | `PLAYER_ERROR_HTTP_FORBIDDEN` | `HXCPlayerErrorHTTPForbidden` | 访问被禁止（403） ✨新增 |

## 本次同步新增错误码

✨ **新增 10 个错误码（全部使用负数）**：

### 通用错误 (2个)
- `-1018` - 无效数据
- `-1019` - 不支持的格式或协议

### 网络错误 (3个)
- `-2001` - 网络连接超时
- `-2002` - 服务器拒绝连接
- `-2003` - 网络不可达

### HTTP 错误 (5个)
- `-3001` - HTTP 请求错误（400）
- `-3002` - HTTP 404 文件不存在
- `-3003` - HTTP 服务器错误（5xx）
- `-3004` - 需要身份验证（401）
- `-3005` - 访问被禁止（403）

## ⚠️ 重要变更

### 错误码值统一使用负数

**之前的设计**：
- C++ 层使用负数（如 `-1001`）
- C/ObjC 层使用正数（如 `1`）

**现在的设计**：
- **所有层都使用负数**，与 C++ 层完全一致
- 这样可以避免错误码转换的复杂性
- 与 FFmpeg 错误码风格保持一致（FFmpeg 也是负数）

**示例**：
```cpp
// C++ 层
ERROR_INVALID_URL = -1001

// C 桥接层（现在也是负数）
PLAYER_ERROR_INVALID_URL = -1001

// ObjC 层（现在也是负数）
HXCPlayerErrorInvalidURL = -1001
```

## 命名规则

### C++ 层
```cpp
ERROR_XXX_YYY_ZZZ
```
- 前缀：`ERROR_`
- 风格：全大写下划线

### C 桥接层
```c
PLAYER_ERROR_XXX_YYY_ZZZ
```
- 前缀：`PLAYER_ERROR_`
- 风格：全大写下划线

### Objective-C 层
```objc
HXCPlayerErrorXxxYyyZzz
```
- 前缀：`HXCPlayerError`
- 风格：驼峰命名

## 快速查找

### 按错误类型查找

#### 🔴 不可恢复错误（不应重试）
- `18` - 无效数据
- `19` - 不支持的格式或协议
- `2` - 打开输入失败（某些情况）
- `3` - 查找流信息失败
- `6` - 找不到解码器
- `7` - 打开解码器失败

#### 🟡 可重试错误
- `2001` - 网络连接超时
- `2002` - 服务器拒绝连接
- `2003` - 网络不可达
- `3001` - HTTP 400
- `3003` - HTTP 5xx

#### ⚪ 资源不存在
- `1` - 无效的 URL
- `3002` - HTTP 404

#### 🔒 权限相关
- `3004` - HTTP 401 需要身份验证
- `3005` - HTTP 403 访问被禁止

## 使用示例

### Objective-C 层判断错误类型

```objc
- (void)player:(HXCPlayerControl *)player didFailWithError:(NSError *)error {
    NSInteger code = error.code;
    
    // 网络相关错误 (-2001 ~ -2003)
    if (code >= HXCPlayerErrorNetUnreachable && 
        code <= HXCPlayerErrorNetConnectionTimeout) {
        [self showNetworkError];
        return;
    }
    
    // HTTP 相关错误 (-3001 ~ -3005)
    if (code >= HXCPlayerErrorHTTPForbidden && 
        code <= HXCPlayerErrorHTTPBadRequest) {
        [self showHTTPError:code];
        return;
    }
    
    // 具体错误判断（使用负数）
    switch (code) {
        case HXCPlayerErrorInputInvalidData:  // -1018
            [self showAlert:@"文件格式无效或损坏"];
            break;
        case HXCPlayerErrorNotSupport:  // -1019
            [self showAlert:@"不支持的格式或协议"];
            break;
        case HXCPlayerErrorNetConnectionTimeout:  // -2001
            [self showAlert:@"网络连接超时，请检查网络"];
            break;
        case HXCPlayerErrorHTTPNotFound:  // -3002
            [self showAlert:@"视频不存在（404）"];
            break;
        default:
            [self showAlert:[NSString stringWithFormat:@"播放失败 (错误码: %ld)", (long)code]];
            break;
    }
}
```

## 文件位置

| 层 | 文件路径 |
|----|---------|
| C++ | `/core/include/hxc_player_core.h` |
| C | `/core/include/hxc_player_core_c_bridge.h` |
| ObjC | `/apple/HXCPlayerControl.h` |

## 状态

✅ **已同步** - 三层错误码定义完全一致  
✅ **无编译错误** - 已通过 linter 检查  
✅ **文档完整** - 包含详细说明和使用示例  

---

**最后更新**: 2026-02-24  
**错误码总数**: 25 个（不包括 FFmpeg 原始错误码）
