# 自定义数据源使用指南

## 概述

本模块提供了 FFmpeg 自定义数据输入的完整解决方案，支持：

1. **自定义数据源** - 通过实现 `ICustomDataSource` 接口提供任意数据源
2. **HTTP Range 下载** - 内置的 `RangeDownloader` 支持范围下载
3. **AVIOContext 集成** - 无缝集成到 FFmpeg 解码流程

---

## 使用场景

### 1. 加密视频
```cpp
// 解密数据源
class DecryptedDataSource : public ICustomDataSource {
    int read(uint8_t* buffer, int size) override {
        // 读取加密数据
        int bytes = encrypted_file_->read(temp_buffer, size);
        
        // 解密
        decrypt(temp_buffer, buffer, bytes);
        
        return bytes;
    }
};
```

### 2. 私有协议
```cpp
// 自定义协议数据源
class CustomProtocolDataSource : public ICustomDataSource {
    int open(const std::string& url) override {
        // 解析私有协议URL: myprotocol://server/video
        // 建立连接
        return connect_to_server(url);
    }
};
```

### 3. 需要特殊处理的 HTTP(S)
```cpp
// 使用内置的 HttpRangeDataSource
auto data_source = std::make_unique<HttpRangeDataSource>();
data_source->get_downloader()->set_timeout(30000);
data_source->get_downloader()->set_max_retries(5);
```

---

## 快速开始

### 方式 1：使用内置的 HTTP Range 下载器

```cpp
#include "hxc_custom_io.h"
#include "hxc_player_core.h"

// 1. 创建 HTTP Range 数据源
auto data_source = std::make_unique<HttpRangeDataSource>();

// 2. 配置下载器
data_source->get_downloader()->set_timeout(30000);  // 30秒超时
data_source->get_downloader()->set_max_retries(3);  // 最多重试3次
data_source->set_cache_size(2 * 1024 * 1024);       // 2MB 缓存

// 3. 创建 AVIOContext 包装器
auto custom_io = std::make_unique<CustomAVIOContext>(
    std::move(data_source),
    64 * 1024  // 64KB AVIO 缓冲区
);

// 4. 打开数据源
int ret = custom_io->open("https://example.com/video.mp4");
if (ret < 0) {
    LOG_ERROR("打开失败");
    return -1;
}

// 5. 在 PlayerCore 中使用自定义 AVIOContext
AVFormatContext* format_ctx = avformat_alloc_context();
format_ctx->pb = custom_io->get_avio_context();

// 6. 使用 NULL URL 打开（因为数据通过 pb 提供）
ret = avformat_open_input(&format_ctx, nullptr, nullptr, nullptr);
```

### 方式 2：实现自定义数据源

```cpp
// 1. 实现 ICustomDataSource 接口
class MyCustomDataSource : public ICustomDataSource {
public:
    int open(const std::string& url) override {
        // 你的打开逻辑
        file_ = fopen(url.c_str(), "rb");
        return file_ ? 0 : -1;
    }
    
    int read(uint8_t* buffer, int size) override {
        // 你的读取逻辑
        return fread(buffer, 1, size, file_);
    }
    
    int64_t seek(int64_t offset, int whence) override {
        // 你的 seek 逻辑
        fseek(file_, offset, whence);
        return ftell(file_);
    }
    
    int64_t size() override {
        // 返回文件大小
        fseek(file_, 0, SEEK_END);
        int64_t size = ftell(file_);
        fseek(file_, 0, SEEK_SET);
        return size;
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
};

// 2. 使用自定义数据源
auto data_source = std::make_unique<MyCustomDataSource>();
auto custom_io = std::make_unique<CustomAVIOContext>(std::move(data_source));
custom_io->open("/path/to/video.mp4");
```

---

## 在 PlayerCore 中集成

### 修改 `player_core.h`

```cpp
class PlayerCore {
public:
    // 新增：使用自定义数据源打开
    int open_with_custom_io(std::unique_ptr<CustomAVIOContext> custom_io);
    
private:
    std::unique_ptr<CustomAVIOContext> custom_io_;  // 保存自定义 IO
};
```

### 修改 `player_core.cpp`

```cpp
int PlayerCore::open_with_custom_io(std::unique_ptr<CustomAVIOContext> custom_io) {
    custom_io_ = std::move(custom_io);
    
    // 分配 AVFormatContext
    format_ctx_ = avformat_alloc_context();
    
    // 使用自定义 AVIOContext
    format_ctx_->pb = custom_io_->get_avio_context();
    
    // 设置中断回调
    format_ctx_->interrupt_callback.callback = [](void* ctx) -> int {
        PlayerCore* player = static_cast<PlayerCore*>(ctx);
        return player->abort_request_.load() ? 1 : 0;
    };
    format_ctx_->interrupt_callback.opaque = this;
    
    // 打开输入（URL 传 nullptr）
    int ret = avformat_open_input(&format_ctx_, nullptr, nullptr, nullptr);
    if (ret < 0) {
        LOG_ERROR("avformat_open_input 失败: ", ret);
        return ret;
    }
    
    // 后续流程与普通 open() 相同
    // ...
}

// 清理时释放自定义 IO
void PlayerCore::close() {
    // ... 其他清理代码
    
    if (custom_io_) {
        custom_io_->close();
        custom_io_.reset();
    }
}
```

---

## 完整示例

### 示例 1：加密视频播放

```cpp
// 加密视频数据源
class EncryptedVideoSource : public ICustomDataSource {
public:
    EncryptedVideoSource(const std::string& key) : decrypt_key_(key) {}
    
    int open(const std::string& url) override {
        // 1. 下载加密视频
        downloader_ = std::make_unique<RangeDownloader>();
        return downloader_->open(url);
    }
    
    int read(uint8_t* buffer, int size) override {
        // 2. 读取加密数据
        std::vector<uint8_t> encrypted_data(size);
        int bytes = downloader_->read_range(encrypted_data.data(), position_, size);
        
        if (bytes <= 0) return bytes;
        
        // 3. 解密
        decrypt_data(encrypted_data.data(), buffer, bytes, decrypt_key_);
        
        position_ += bytes;
        return bytes;
    }
    
    int64_t seek(int64_t offset, int whence) override {
        // 计算新位置
        if (whence == SEEK_SET) position_ = offset;
        else if (whence == SEEK_CUR) position_ += offset;
        else if (whence == SEEK_END) position_ = downloader_->get_size() + offset;
        
        return position_;
    }
    
    int64_t size() override {
        return downloader_->get_size();
    }
    
    void close() override {
        downloader_->close();
    }
    
    bool seekable() const override {
        return downloader_->support_range();
    }

private:
    std::string decrypt_key_;
    std::unique_ptr<RangeDownloader> downloader_;
    int64_t position_ = 0;
    
    void decrypt_data(const uint8_t* input, uint8_t* output, int size, const std::string& key) {
        // 你的解密算法（AES、自定义算法等）
        for (int i = 0; i < size; i++) {
            output[i] = input[i] ^ key[i % key.size()];  // 简单异或示例
        }
    }
};

// 使用
auto encrypted_source = std::make_unique<EncryptedVideoSource>("my_secret_key");
auto custom_io = std::make_unique<CustomAVIOContext>(std::move(encrypted_source));

player->open_with_custom_io(std::move(custom_io));
```

### 示例 2：需要特殊认证的 HTTP

```cpp
class AuthenticatedHttpSource : public HttpRangeDataSource {
public:
    AuthenticatedHttpSource(const std::string& auth_token) 
        : auth_token_(auth_token) {}
    
    int open(const std::string& url) override {
        // 添加认证头
        auto downloader = get_downloader();
        
        // 配置 CURL 添加自定义 header
        // （需要扩展 RangeDownloader 支持自定义 header）
        
        return HttpRangeDataSource::open(url);
    }

private:
    std::string auth_token_;
};
```

---

## API 参考

### ICustomDataSource

| 方法 | 说明 | 返回值 |
|------|------|--------|
| `open(url)` | 打开数据源 | 0=成功，负数=失败 |
| `read(buffer, size)` | 读取数据 | 实际读取字节数，0=EOF，负数=错误 |
| `seek(offset, whence)` | 定位 | 新位置，负数=失败 |
| `size()` | 获取大小 | 字节数，-1=未知 |
| `close()` | 关闭数据源 | 无 |
| `seekable()` | 是否支持 seek | true/false |

### RangeDownloader

| 方法 | 说明 |
|------|------|
| `open(url)` | 打开 HTTP 资源 |
| `read_range(buffer, offset, size)` | 读取指定范围 |
| `get_size()` | 获取资源总大小 |
| `support_range()` | 是否支持 Range 请求 |
| `set_timeout(ms)` | 设置超时时间 |
| `set_max_retries(n)` | 设置最大重试次数 |
| `set_progress_callback(callback)` | 设置进度回调 |

### CustomAVIOContext

| 方法 | 说明 |
|------|------|
| `CustomAVIOContext(data_source, buffer_size)` | 构造函数 |
| `open(url)` | 打开数据源 |
| `get_avio_context()` | 获取 AVIOContext 指针 |
| `close()` | 关闭并释放资源 |

---

## 性能优化建议

### 1. 缓存策略
```cpp
// 适当增加缓存大小
data_source->set_cache_size(5 * 1024 * 1024);  // 5MB

// 增加 AVIO 缓冲区
auto custom_io = std::make_unique<CustomAVIOContext>(
    std::move(data_source),
    128 * 1024  // 128KB
);
```

### 2. 预读优化
```cpp
class PrefetchDataSource : public HttpRangeDataSource {
    int read(uint8_t* buffer, int size) override {
        // 预读更多数据到缓存
        int prefetch_size = size * 4;  // 预读 4 倍
        // ...
    }
};
```

### 3. 并发下载
```cpp
// 实现分段并发下载
class ParallelDownloadSource : public ICustomDataSource {
    // 使用多个 RangeDownloader 并发下载不同段
};
```

---

## 常见问题

### Q: seek 操作很慢怎么办？
A: 增加缓存大小，或实现更智能的缓存策略（LRU 缓存）。

### Q: 如何处理网络断开？
A: `RangeDownloader` 已内置重试机制，可通过 `set_max_retries()` 配置。

### Q: 能否用于 HLS/DASH 流？
A: 可以，但需要为每个分片创建独立的数据源。

### Q: 如何监控下载进度？
A: 使用 `set_progress_callback()` 设置进度回调。

---

## 依赖库

- **libcurl** - HTTP 下载（已包含在系统中）

### 编译选项

```cmake
# CMakeLists.txt
find_package(CURL REQUIRED)
target_link_libraries(YourTarget PRIVATE CURL::libcurl)
```

---

## 总结

✅ **灵活** - 支持任意自定义数据源  
✅ **高效** - 内置缓存和范围下载  
✅ **可靠** - 自动重试和错误处理  
✅ **易用** - 简单的 API 设计  

现在你可以轻松处理加密视频、私有协议等特殊场景了！
