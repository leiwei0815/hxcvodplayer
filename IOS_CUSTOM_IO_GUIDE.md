# iOS 自定义数据源播放示例

## 概述

现在 iOS 项目已经集成了自定义数据源功能，可以使用自定义的 HTTP Range 下载器来下载数据并交给 FFmpeg 解码播放。

---

## 使用方法

### 方式 1：在 PlayerViewController 中切换

在 `PlayerViewController.mm` 的 `openTestVideo` 方法中，有一个开关可以选择使用自定义数据源：

```objc
- (void)openTestVideo {
    NSString *urlString = @"https://vod-volcengine.cskziwl.cn/P6N8MWsjc58A5Rb3/K7XpsqzzPY1dGv5f.mp4";
    
    // ✨ 控制是否使用自定义数据源
    BOOL useCustomDataSource = YES;  // 设置为 YES 使用自定义下载器
    
    BOOL success;
    if (useCustomDataSource) {
        success = [self openWithCustomDataSource:urlString];  // 自定义下载器
    } else {
        success = [_player openURL:urlString];                // 直接播放
    }
    
    if (success) {
        [_player play];
    }
}
```

### 方式 2：直接调用 openWithCustomDataSource

```objc
// 使用自定义数据源播放
NSString *url = @"https://example.com/video.mp4";
BOOL success = [self openWithCustomDataSource:url];
if (success) {
    [_player play];
}
```

---

## 实现细节

### PlayerViewController 中的实现

```objc
- (BOOL)openWithCustomDataSource:(NSString *)urlString {
    NSLog(@"========================================");
    NSLog(@"🔥 使用自定义数据源播放");
    NSLog(@"========================================");
    NSLog(@"URL: %@", urlString);
    
    // 1. 创建 HTTP Range 数据源
    auto dataSource = std::make_unique<hxcplayer::HttpRangeDataSource>();
    
    // 2. 配置下载器
    dataSource->get_downloader()->set_timeout(30000);     // 30秒超时
    dataSource->get_downloader()->set_max_retries(3);     // 最多重试3次
    dataSource->set_cache_size(2 * 1024 * 1024);          // 2MB 缓存
    
    // 3. 设置下载进度回调（可选）
    __weak typeof(self) weakSelf = self;
    dataSource->get_downloader()->set_progress_callback(
        [weakSelf](int64_t downloaded, int64_t total) {
            if (total > 0) {
                double percent = (double)downloaded / total * 100.0;
                dispatch_async(dispatch_get_main_queue(), ^{
                    NSLog(@"📥 下载进度: %.1f%% (%lld / %lld 字节)", 
                          percent, downloaded, total);
                });
            }
        }
    );
    
    // 4. 创建 AVIOContext 包装器
    auto customIO = std::make_unique<hxcplayer::CustomAVIOContext>(
        std::move(dataSource),
        64 * 1024  // 64KB AVIO 缓冲区
    );
    
    // 5. 打开数据源
    int ret = customIO->open([urlString UTF8String]);
    if (ret < 0) {
        NSLog(@"❌ 自定义数据源打开失败: %d", ret);
        return NO;
    }
    
    NSLog(@"✅ 自定义数据源打开成功");
    
    // 6. 使用自定义 IO 打开播放器
    return [_player openWithCustomIO:std::move(customIO)];
}
```

### HXCPlayerControl 中的实现

```objc
- (BOOL)openWithCustomIO:(std::unique_ptr<hxcplayer::CustomAVIOContext>)customIO {
    NSLog(@"🔥 HXCPlayerControl: 使用自定义数据源打开");
    
    [self stop];
    _playerUrl = @"custom_io_stream";
    
    // 调用 PlayerCore 的 open_with_custom_io 方法
    hxcplayer::PlayerCore* core = static_cast<hxcplayer::PlayerCore*>(_wrapper->handle());
    int ret = core->open_with_custom_io(std::move(customIO));
    
    if (ret != 0) {
        return NO;
    }
    
    // 获取媒体信息并初始化音频引擎
    _duration = player_core_get_duration(_wrapper->handle());
    _videoWidth = player_core_get_video_width(_wrapper->handle());
    _videoHeight = player_core_get_video_height(_wrapper->handle());
    
    int sampleRate = player_core_get_audio_sample_rate(_wrapper->handle());
    int channels = player_core_get_audio_channels(_wrapper->handle());
    
    [self initAudioEngine:sampleRate channels:channels];
    
    return YES;
}
```

---

## 日志输出示例

### 成功播放时的日志

```
========================================
🔥 使用自定义数据源播放
========================================
URL: https://vod-volcengine.cskziwl.cn/P6N8MWsjc58A5Rb3/K7XpsqzzPY1dGv5f.mp4
✅ 自定义数据源打开成功
🔥 HXCPlayerControl: 使用自定义数据源打开
✅ 自定义数据源打开成功
   时长: 120.50 秒
   分辨率: 1920 x 1080
   音频: 44100 Hz, 2 通道
📥 下载进度: 0.5% (131072 / 25000000 字节)
📥 下载进度: 1.0% (262144 / 25000000 字节)
📥 下载进度: 1.5% (393216 / 25000000 字节)
...
```

---

## 配置参数

### 下载器配置

```objc
// 超时时间（毫秒）
dataSource->get_downloader()->set_timeout(30000);  // 30秒

// 最大重试次数
dataSource->get_downloader()->set_max_retries(3);  // 重试3次

// 缓存大小（字节）
dataSource->set_cache_size(2 * 1024 * 1024);  // 2MB

// AVIO 缓冲区大小（字节）
auto customIO = std::make_unique<hxcplayer::CustomAVIOContext>(
    std::move(dataSource),
    64 * 1024  // 64KB
);
```

### 推荐配置

| 网络环境 | 超时时间 | 重试次数 | 缓存大小 | AVIO 缓冲 |
|---------|---------|---------|---------|----------|
| WiFi | 30秒 | 3次 | 2MB | 64KB |
| 4G | 20秒 | 5次 | 1MB | 32KB |
| 3G | 15秒 | 5次 | 512KB | 32KB |

---

## 优势

### 使用自定义数据源的优势

1. **更好的控制** - 完全控制数据下载过程
2. **进度监控** - 可以实时监控下载进度
3. **自动重试** - 内置重试机制，提高稳定性
4. **缓存优化** - 减少重复下载
5. **扩展性强** - 可以轻松添加解密、预处理等功能

### 与直接播放的对比

| 特性 | 直接播放 | 自定义数据源 |
|------|---------|------------|
| 实现复杂度 | ✅ 简单 | ⚠️ 中等 |
| 下载控制 | ❌ 无 | ✅ 完全控制 |
| 进度监控 | ❌ 无 | ✅ 支持 |
| 重试机制 | ⚠️ FFmpeg 内置 | ✅ 可配置 |
| 缓存优化 | ❌ 无 | ✅ 支持 |
| 加密支持 | ❌ 无 | ✅ 可扩展 |

---

## 测试步骤

### 1. 编译项目

```bash
cd /Users/debug/project/YXVodPlayer/apple/ios
./build_ios.sh
```

### 2. 在 Xcode 中打开项目

```bash
open YXVodPlayer.xcodeproj
```

### 3. 运行到真机或模拟器

- 选择目标设备（iPhone/iPad 或模拟器）
- 点击运行按钮（⌘ + R）

### 4. 观察日志

在 Xcode 控制台观察日志输出，确认是否使用了自定义数据源：

```
========================================
🔥 使用自定义数据源播放
========================================
```

### 5. 切换播放方式

修改 `PlayerViewController.mm` 中的开关：

```objc
BOOL useCustomDataSource = YES;  // 改为 NO 使用直接播放
```

重新运行，对比两种方式的播放效果。

---

## 故障排查

### 问题 1：编译错误 "Use of undeclared identifier"

**原因**：缺少头文件引用

**解决**：确保文件顶部包含：
```objc
#include "hxc_custom_io.h"
```

### 问题 2：链接错误 "Undefined symbols for libcurl"

**原因**：缺少 curl 库链接

**解决**：检查 `apple/framework/CMakeLists.txt` 是否包含：
```cmake
target_link_libraries(HXCPlayer PUBLIC "-lcurl")
```

### 问题 3：运行时崩溃 "Bad access"

**原因**：unique_ptr 所有权转移问题

**解决**：确保使用 `std::move` 转移所有权：
```objc
return [_player openWithCustomIO:std::move(customIO)];
```

### 问题 4：下载进度回调不显示

**原因**：服务器不支持 Range 请求或未返回 Content-Length

**解决**：检查日志中的 "支持 Range" 信息

---

## 下一步扩展

### 1. 添加加密支持

参考 `CUSTOM_IO_EXAMPLES.md` 中的加密视频示例，实现自定义解密逻辑。

### 2. 添加本地缓存

```objc
// 实现边下边播 + 本地缓存
class CachedDownloadSource : public hxcplayer::HttpRangeDataSource {
    // 缓存到本地
    // 支持离线播放
};
```

### 3. 添加 P2P 支持

```objc
// 从 P2P 网络获取数据
class P2PDataSource : public hxcplayer::ICustomDataSource {
    // 实现 P2P 逻辑
};
```

---

## 总结

✅ **已完成**：iOS 项目集成自定义数据源  
✅ **可切换**：支持直接播放和自定义下载器两种方式  
✅ **可配置**：超时、重试、缓存等参数可调整  
✅ **可扩展**：可以轻松添加加密、缓存等功能  

现在你可以运行项目，观察使用自定义下载器播放视频的效果了！
