# 自定义数据源使用示例

## 示例 1：基础 HTTP Range 下载

```cpp
#include "hxc_player_core.h"
#include "hxc_custom_io.h"

// 1. 创建 HTTP Range 数据源
auto data_source = std::make_unique<hxcplayer::HttpRangeDataSource>();

// 2. 配置下载器（可选）
data_source->get_downloader()->set_timeout(30000);     // 30秒超时
data_source->get_downloader()->set_max_retries(3);     // 最多重试3次
data_source->set_cache_size(2 * 1024 * 1024);          // 2MB 缓存

// 3. 配置进度回调（可选）
data_source->get_downloader()->set_progress_callback(
    [](int64_t downloaded, int64_t total) {
        if (total > 0) {
            double percent = (double)downloaded / total * 100.0;
            printf("下载进度: %.1f%%\n", percent);
        }
    }
);

// 4. 创建 AVIOContext 包装器
auto custom_io = std::make_unique<hxcplayer::CustomAVIOContext>(
    std::move(data_source),
    64 * 1024  // 64KB AVIO 缓冲区
);

// 5. 打开数据源
int ret = custom_io->open("https://example.com/video.mp4");
if (ret < 0) {
    LOG_ERROR("打开失败");
    return -1;
}

// 6. 在 PlayerCore 中使用
hxcplayer::PlayerCore player;
player.open_with_custom_io(std::move(custom_io));
player.play();
```

---

## 示例 2：加密视频播放

```cpp
#include "hxc_custom_io.h"
#include <vector>

// 自定义加密视频数据源
class EncryptedVideoSource : public hxcplayer::ICustomDataSource {
public:
    EncryptedVideoSource(const std::string& decrypt_key) 
        : decrypt_key_(decrypt_key) {}
    
    int open(const std::string& url) override {
        // 打开加密视频文件或下载器
        downloader_ = std::make_unique<hxcplayer::RangeDownloader>();
        return downloader_->open(url);
    }
    
    int read(uint8_t* buffer, int size) override {
        // 读取加密数据
        std::vector<uint8_t> encrypted_data(size);
        int bytes = downloader_->read_range(encrypted_data.data(), 
                                            current_position_, 
                                            size);
        
        if (bytes <= 0) return bytes;
        
        // 解密数据（使用你的解密算法）
        decrypt(encrypted_data.data(), buffer, bytes);
        
        current_position_ += bytes;
        return bytes;
    }
    
    int64_t seek(int64_t offset, int whence) override {
        if (whence == SEEK_SET) {
            current_position_ = offset;
        } else if (whence == SEEK_CUR) {
            current_position_ += offset;
        } else if (whence == SEEK_END) {
            current_position_ = downloader_->get_size() + offset;
        } else if (whence == AVSEEK_SIZE) {
            return downloader_->get_size();
        }
        return current_position_;
    }
    
    int64_t size() override {
        return downloader_->get_size();
    }
    
    void close() override {
        if (downloader_) {
            downloader_->close();
        }
    }
    
    bool seekable() const override {
        return downloader_ && downloader_->support_range();
    }

private:
    std::string decrypt_key_;
    std::unique_ptr<hxcplayer::RangeDownloader> downloader_;
    int64_t current_position_ = 0;
    
    void decrypt(const uint8_t* input, uint8_t* output, int size) {
        // 你的解密实现（AES、自定义算法等）
        // 这里是简单的 XOR 示例
        for (int i = 0; i < size; i++) {
            output[i] = input[i] ^ decrypt_key_[i % decrypt_key_.size()];
        }
    }
};

// 使用加密视频数据源
void playEncryptedVideo(const std::string& url, const std::string& key) {
    auto encrypted_source = std::make_unique<EncryptedVideoSource>(key);
    auto custom_io = std::make_unique<hxcplayer::CustomAVIOContext>(
        std::move(encrypted_source)
    );
    
    if (custom_io->open(url) < 0) {
        printf("打开加密视频失败\n");
        return;
    }
    
    hxcplayer::PlayerCore player;
    player.open_with_custom_io(std::move(custom_io));
    player.play();
}
```

---

## 示例 3：本地文件 + 预处理

```cpp
class PreprocessedFileSource : public hxcplayer::ICustomDataSource {
public:
    int open(const std::string& url) override {
        file_ = fopen(url.c_str(), "rb");
        if (!file_) return -1;
        
        // 获取文件大小
        fseek(file_, 0, SEEK_END);
        file_size_ = ftell(file_);
        fseek(file_, 0, SEEK_SET);
        
        return 0;
    }
    
    int read(uint8_t* buffer, int size) override {
        int bytes_read = fread(buffer, 1, size, file_);
        
        // 在这里可以对数据进行预处理
        preprocess(buffer, bytes_read);
        
        return bytes_read;
    }
    
    int64_t seek(int64_t offset, int whence) override {
        fseek(file_, offset, whence);
        return ftell(file_);
    }
    
    int64_t size() override {
        return file_size_;
    }
    
    void close() override {
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }
    }
    
    bool seekable() const override { return true; }

private:
    FILE* file_ = nullptr;
    int64_t file_size_ = 0;
    
    void preprocess(uint8_t* data, int size) {
        // 你的预处理逻辑
        // 例如：数据修正、格式转换等
    }
};
```

---

## 示例 4：iOS/macOS 使用示例

```objc
// Objective-C++ 封装

#import "HXCPlayerControl.h"
#include "hxc_custom_io.h"

@interface HXCPlayerControl ()
@property (nonatomic, assign) std::unique_ptr<hxcplayer::CustomAVIOContext> customIO;
@end

@implementation HXCPlayerControl

- (BOOL)openWithCustomURL:(NSString *)url decryptKey:(NSString *)key {
    // 创建加密数据源
    auto encrypted_source = std::make_unique<EncryptedVideoSource>(
        [key UTF8String]
    );
    
    // 创建 AVIOContext
    self.customIO = std::make_unique<hxcplayer::CustomAVIOContext>(
        std::move(encrypted_source),
        64 * 1024  // 64KB 缓冲
    );
    
    // 打开数据源
    if (self.customIO->open([url UTF8String]) < 0) {
        NSLog(@"打开自定义数据源失败");
        return NO;
    }
    
    // 使用自定义 IO 打开播放器
    int ret = self.playerCore->open_with_custom_io(std::move(self.customIO));
    if (ret < 0) {
        NSLog(@"播放器打开失败");
        return NO;
    }
    
    return YES;
}

@end
```

---

## 常见使用场景

### 1. 需要特殊 Header 的 HTTP 请求

```cpp
// TODO: 需要扩展 RangeDownloader 支持自定义 Header
class AuthenticatedHttpSource : public hxcplayer::HttpRangeDataSource {
    // 添加 Authorization header
    // 添加自定义 Cookie
};
```

### 2. 断点续传下载

```cpp
class CachedDownloadSource : public hxcplayer::ICustomDataSource {
    // 下载到本地缓存
    // 支持断点续传
    // 边下边播
};
```

### 3. P2P 数据源

```cpp
class P2PDataSource : public hxcplayer::ICustomDataSource {
    // 从 P2P 网络获取数据
    // 多个 peer 并发下载
};
```

---

## 性能优化建议

### 1. 调整缓存大小

```cpp
// 增大缓存可以减少网络请求次数
data_source->set_cache_size(5 * 1024 * 1024);  // 5MB

// 增大 AVIO 缓冲区
auto custom_io = std::make_unique<CustomAVIOContext>(
    std::move(data_source),
    128 * 1024  // 128KB
);
```

### 2. 预读策略

```cpp
class PrefetchDataSource : public HttpRangeDataSource {
    int read(uint8_t* buffer, int size) override {
        // 预读更多数据
        int prefetch_size = size * 4;
        // 实现预读逻辑...
        return HttpRangeDataSource::read(buffer, size);
    }
};
```

### 3. 并发下载

```cpp
// 实现多线程并发下载不同的数据段
class ParallelDownloader {
    // 分段下载
    // 合并数据
};
```

---

## 注意事项

1. **线程安全**: `read()` 和 `seek()` 可能在不同线程被调用，需要加锁保护
2. **内存管理**: 确保在 `close()` 中正确释放所有资源
3. **错误处理**: 返回负数表示错误，0 表示 EOF
4. **性能**: 避免在 `read()` 中执行耗时操作
5. **HTTPS**: macOS/iOS 需要配置 ATS 或使用 HTTPS

---

## 调试技巧

### 1. 添加日志

```cpp
int read(uint8_t* buffer, int size) override {
    LOG_INFO("read: offset=", current_position_, ", size=", size);
    // ...
}
```

### 2. 监控性能

```cpp
auto start = std::chrono::high_resolution_clock::now();
int bytes = downloader_->read_range(...);
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
LOG_INFO("下载耗时: ", duration.count(), "ms");
```

---

## 总结

✅ **灵活** - 支持任意自定义数据源  
✅ **强大** - 内置 HTTP Range 下载器  
✅ **易用** - 简单的 API 设计  
✅ **高效** - 缓存和预读优化  

现在你可以轻松处理各种特殊视频源了！
