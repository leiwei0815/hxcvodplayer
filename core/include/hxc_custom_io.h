/**
 * @file hxc_custom_io.h
 * @brief 自定义数据源和 AVIOContext 支持
 * 
 * 用于处理需要自定义数据输入的场景：
 * - 加密视频
 * - 私有协议
 * - 需要特殊处理的数据源
 */

#ifndef YXVODPLAYER_CUSTOM_IO_H
#define YXVODPLAYER_CUSTOM_IO_H

#include <string>
#include <memory>
#include <functional>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
}

namespace hxcplayer {

// 前向声明
class RangeDownloader;

/**
 * @brief 自定义数据源回调接口
 * 
 * 用户可以实现这个接口来提供自定义的数据读取逻辑
 */
class ICustomDataSource {
public:
    virtual ~ICustomDataSource() = default;
    
    /**
     * @brief 打开数据源
     * @param url 资源 URL（可以是任何自定义格式）
     * @return 0 成功，负数失败
     */
    virtual int open(const std::string& url) = 0;
    
    /**
     * @brief 读取数据
     * @param buffer 输出缓冲区
     * @param size 请求读取的字节数
     * @return 实际读取的字节数，0 表示 EOF，负数表示错误
     */
    virtual int read(uint8_t* buffer, int size) = 0;
    
    /**
     * @brief 定位到指定位置
     * @param offset 目标偏移量
     * @param whence SEEK_SET, SEEK_CUR, SEEK_END
     * @return 新的位置，负数表示错误
     */
    virtual int64_t seek(int64_t offset, int whence) = 0;
    
    /**
     * @brief 获取数据源大小
     * @return 数据源总大小（字节），-1 表示未知
     */
    virtual int64_t size() = 0;
    
    /**
     * @brief 关闭数据源
     */
    virtual void close() = 0;
    
    /**
     * @brief 是否支持 seek 操作
     */
    virtual bool seekable() const { return true; }
};

/**
 * @brief HTTP 范围下载器
 * 
 * 支持 HTTP Range 请求的下载器，用于实现自定义数据源
 */
class RangeDownloader {
public:
    /**
     * @brief 下载进度回调
     * @param downloaded 已下载字节数
     * @param total 总字节数（-1 表示未知）
     */
    using ProgressCallback = std::function<void(int64_t downloaded, int64_t total)>;
    
    RangeDownloader();
    ~RangeDownloader();
    
    /**
     * @brief 打开 HTTP 资源
     * @param url HTTP(S) URL
     * @return 0 成功，负数失败
     */
    int open(const std::string& url);
    
    /**
     * @brief 读取指定范围的数据
     * @param buffer 输出缓冲区
     * @param offset 起始偏移量
     * @param size 读取字节数
     * @return 实际读取的字节数，负数表示错误
     */
    int read_range(uint8_t* buffer, int64_t offset, int size);
    
    /**
     * @brief 获取资源总大小
     * @return 资源大小（字节），-1 表示未知
     */
    int64_t get_size() const { return content_length_; }
    
    /**
     * @brief 检查服务器是否支持 Range 请求
     */
    bool support_range() const { return support_range_; }
    
    /**
     * @brief 关闭下载器
     */
    void close();
    
    /**
     * @brief 设置超时时间（毫秒）
     */
    void set_timeout(int timeout_ms) { timeout_ms_ = timeout_ms; }
    
    /**
     * @brief 设置进度回调
     */
    void set_progress_callback(ProgressCallback callback) { 
        progress_callback_ = callback; 
    }
    
    /**
     * @brief 设置最大重试次数
     */
    void set_max_retries(int retries) { max_retries_ = retries; }

private:
    std::string url_;
    int64_t content_length_ = -1;
    bool support_range_ = false;
    int timeout_ms_ = 30000;
    int max_retries_ = 3;
    ProgressCallback progress_callback_;
    std::atomic<bool> abort_request_{false};
    std::atomic<int64_t> total_downloaded_{0};
    
    // 获取 HTTP 头信息
    int fetch_http_headers();
    
    // 执行 HTTP Range 请求
    int do_range_request(uint8_t* buffer, int64_t offset, int size);
};

/**
 * @brief 基于 RangeDownloader 的自定义数据源实现
 */
class HttpRangeDataSource : public ICustomDataSource {
public:
    HttpRangeDataSource();
    ~HttpRangeDataSource() override;
    
    int open(const std::string& url) override;
    int read(uint8_t* buffer, int size) override;
    int64_t seek(int64_t offset, int whence) override;
    int64_t size() override;
    void close() override;
    bool seekable() const override;
    
    /**
     * @brief 设置缓存大小
     * @param cache_size 缓存大小（字节），默认 1MB
     */
    void set_cache_size(size_t cache_size) { cache_size_ = cache_size; }
    
    /**
     * @brief 获取下载器（用于配置）
     */
    RangeDownloader* get_downloader() { return downloader_.get(); }

private:
    std::unique_ptr<RangeDownloader> downloader_;
    int64_t current_position_ = 0;
    int64_t file_size_ = -1;
    size_t cache_size_ = 1024 * 1024;  // 1MB 缓存
    
    // 简单的缓存管理
    std::unique_ptr<uint8_t[]> cache_buffer_;
    int64_t cache_start_ = -1;
    int64_t cache_end_ = -1;
};

/**
 * @brief AVIOContext 包装器
 * 
 * 将 ICustomDataSource 包装为 FFmpeg 的 AVIOContext
 */
class CustomAVIOContext {
public:
    /**
     * @brief 创建自定义 AVIOContext
     * @param data_source 自定义数据源（所有权转移）
     * @param buffer_size AVIO 缓冲区大小，默认 32KB
     */
    CustomAVIOContext(std::unique_ptr<ICustomDataSource> data_source, 
                      size_t buffer_size = 32768);
    ~CustomAVIOContext();
    
    /**
     * @brief 打开数据源
     * @param url 资源 URL
     * @return 0 成功，负数失败
     */
    int open(const std::string& url);
    
    /**
     * @brief 获取 AVIOContext 指针
     * @return AVIOContext 指针，供 avformat_open_input 使用
     */
    AVIOContext* get_avio_context() { return avio_ctx_; }
    
    /**
     * @brief 关闭并释放资源
     */
    void close();

private:
    std::unique_ptr<ICustomDataSource> data_source_;
    AVIOContext* avio_ctx_ = nullptr;
    uint8_t* avio_buffer_ = nullptr;
    size_t buffer_size_;
    
    // FFmpeg 回调函数
    static int read_packet(void* opaque, uint8_t* buf, int buf_size);
    static int64_t seek(void* opaque, int64_t offset, int whence);
};

} // namespace hxcplayer

#endif // YXVODPLAYER_CUSTOM_IO_H
