/**
 * @file hxc_custom_io.cpp
 * @brief 自定义数据源和 AVIOContext 实现
 */

#include "hxc_custom_io.h"
#include "hxc_logger.h"
#include <cstring>
#include <algorithm>
#include <cerrno>
#include <vector>
#include <chrono>

// 平台相关的 HTTP 实现
#if defined(__APPLE__)
    // iOS/macOS 使用 CFNetwork（纯 C 接口，不依赖 curl）
    #include <CFNetwork/CFNetwork.h>
    #include <CoreFoundation/CoreFoundation.h>
    #define USE_CFNETWORK 1
#elif defined(_WIN32)
    // Windows 使用 WinHTTP（系统原生）
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
    #define USE_WINHTTP 1
#else
    // Android/Linux 使用 curl
    #include <curl/curl.h>
    #define USE_CURL 1
#endif

namespace hxcplayer {

#ifdef USE_CFNETWORK
static void cfurl_to_utf8_string(CFURLRef u, std::string& out) {
    if (!u) {
        return;
    }
    CFStringRef s = CFURLGetString(u);
    if (!s) {
        return;
    }
    CFIndex len = CFStringGetLength(s);
    CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::vector<char> buf(static_cast<size_t>(max));
    if (CFStringGetCString(s, buf.data(), static_cast<CFIndex>(buf.size()), kCFStringEncodingUTF8)) {
        out.assign(buf.data());
    }
}
#endif

static inline uint8_t decrypt_first100_byte(uint8_t b) {
    // 解密规则（与加密 byte = ROL3(byte); byte = ~byte; 互逆）：
    // 1) 按位取反
    // 2) 循环右移 3 位（ROR3）
    uint8_t x = static_cast<uint8_t>(~b);
    return static_cast<uint8_t>((x >> 3) | (x << (8 - 3)));
}

// ==================== RangeDownloader 实现 ====================

#ifdef USE_CURL
// CURL 写入回调
static size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::vector<uint8_t>* buffer = static_cast<std::vector<uint8_t>*>(userp);
    
    size_t old_size = buffer->size();
    buffer->resize(old_size + realsize);
    std::memcpy(buffer->data() + old_size, contents, realsize);
    
    return realsize;
}
#endif

RangeDownloader::RangeDownloader() {
#ifdef USE_CURL
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
    }
#endif
}

RangeDownloader::~RangeDownloader() {
    close();
}

int RangeDownloader::open(const std::string& url) {
    url_ = url;
    effective_url_.clear();
    
    int ret = fetch_http_headers();
    if (ret < 0) {
        LOG_ERROR("获取 HTTP 头信息失败");
        return ret;
    }
    
    LOG_INFO("RangeDownloader 打开成功:");
    LOG_INFO("  URL: ", url_);
    LOG_INFO("  大小: ", content_length_, " 字节");
    LOG_INFO("  支持 Range: ", support_range_ ? "是" : "否");
    
    return 0;
}

#ifdef USE_CFNETWORK
// ==================== Apple 平台 CFNetwork 实现 ====================

int RangeDownloader::fetch_http_headers() {
    CFStringRef urlStr = CFStringCreateWithCString(kCFAllocatorDefault, url_.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithString(kCFAllocatorDefault, urlStr, nullptr);
    CFRelease(urlStr);
    
    if (!url) {
        LOG_ERROR("无效的 URL");
        return -1;
    }
    
    CFStringRef method = CFSTR("HEAD");
    CFHTTPMessageRef request = CFHTTPMessageCreateRequest(kCFAllocatorDefault, method, url, kCFHTTPVersion1_1);
    CFRelease(url);
    
    CFReadStreamRef stream = CFReadStreamCreateForHTTPRequest(kCFAllocatorDefault, request);
    CFRelease(request);
    
    // 允许自动重定向
    CFReadStreamSetProperty(stream, kCFStreamPropertyHTTPShouldAutoredirect, kCFBooleanTrue);
    // 禁用持久连接，避免 HEAD 请求后连接池复用失效连接
    CFReadStreamSetProperty(stream, CFSTR("kCFStreamPropertyHTTPAttemptPersistentConnection"), kCFBooleanFalse);
    
    if (!CFReadStreamOpen(stream)) {
        LOG_ERROR("无法打开 HTTP 流");
        CFRelease(stream);
        return -1;
    }
    
    // HEAD 请求没有 body，读取触发响应头的接收
    UInt8 buf[1];
    CFReadStreamRead(stream, buf, sizeof(buf));
    
    CFHTTPMessageRef response = (CFHTTPMessageRef)CFReadStreamCopyProperty(stream, kCFStreamPropertyHTTPResponseHeader);
    
    if (response) {
        CFStringRef contentLengthStr = CFHTTPMessageCopyHeaderFieldValue(response, CFSTR("Content-Length"));
        if (contentLengthStr) {
            char clBuffer[64];
            if (CFStringGetCString(contentLengthStr, clBuffer, sizeof(clBuffer), kCFStringEncodingUTF8)) {
                content_length_ = atoll(clBuffer);
            }
            CFRelease(contentLengthStr);
        }
        
        // 检查 Accept-Ranges
        CFStringRef acceptRanges = CFHTTPMessageCopyHeaderFieldValue(response, CFSTR("Accept-Ranges"));
        if (acceptRanges) {
            char arBuffer[64];
            if (CFStringGetCString(acceptRanges, arBuffer, sizeof(arBuffer), kCFStringEncodingUTF8)) {
                support_range_ = (strcmp(arBuffer, "none") != 0);
            }
            CFRelease(acceptRanges);
        } else {
            support_range_ = true;
        }
        
        CFRelease(response);
    } else {
        LOG_ERROR("未收到 HTTP 响应");
        CFReadStreamClose(stream);
        CFRelease(stream);
        return -1;
    }
    
    {
        CFURLRef finalURL = (CFURLRef)CFReadStreamCopyProperty(stream, kCFStreamPropertyHTTPFinalURL);
        if (finalURL) {
            cfurl_to_utf8_string(finalURL, effective_url_);
            CFRelease(finalURL);
        }
    }
    
    CFReadStreamClose(stream);
    CFRelease(stream);
    return 0;
}

int RangeDownloader::do_range_request(uint8_t* buffer, int64_t offset, int size) {
    CFStringRef urlStr = CFStringCreateWithCString(kCFAllocatorDefault, url_.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithString(kCFAllocatorDefault, urlStr, nullptr);
    CFRelease(urlStr);
    
    if (!url) {
        return -1;
    }
    
    CFHTTPMessageRef request = CFHTTPMessageCreateRequest(kCFAllocatorDefault, CFSTR("GET"), url, kCFHTTPVersion1_1);
    CFRelease(url);
    
    // 设置 Range 头
    std::string rangeValue = "bytes=" + std::to_string(offset) + "-" + std::to_string(offset + size - 1);
    CFStringRef rangeStr = CFStringCreateWithCString(kCFAllocatorDefault, rangeValue.c_str(), kCFStringEncodingUTF8);
    CFHTTPMessageSetHeaderFieldValue(request, CFSTR("Range"), rangeStr);
    CFRelease(rangeStr);
    
    CFReadStreamRef stream = CFReadStreamCreateForHTTPRequest(kCFAllocatorDefault, request);
    CFRelease(request);
    
    CFReadStreamSetProperty(stream, kCFStreamPropertyHTTPShouldAutoredirect, kCFBooleanTrue);
    CFReadStreamSetProperty(stream, CFSTR("kCFStreamPropertyHTTPAttemptPersistentConnection"), kCFBooleanFalse);
    
    // SSL 配置
    CFMutableDictionaryRef sslSettings = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(sslSettings, kCFStreamSSLLevel, kCFStreamSocketSecurityLevelNegotiatedSSL);
    CFReadStreamSetProperty(stream, kCFStreamPropertySSLSettings, sslSettings);
    CFRelease(sslSettings);
    
    if (!CFReadStreamOpen(stream)) {
        LOG_ERROR("无法打开 HTTP 流（Range 请求）");
        CFRelease(stream);
        return -1;
    }
    
    // 读取数据
    int total_read = 0;
    bool captured_effective_url = false;
    while (total_read < size) {
        if (abort_request_.load()) {
            CFReadStreamClose(stream);
            CFRelease(stream);
            return AVERROR_EXIT;
        }
        
        CFIndex bytesRead = CFReadStreamRead(stream, buffer + total_read, size - total_read);
        if (bytesRead > 0) {
            if (offset == 0 && !captured_effective_url) {
                CFURLRef finalURL = (CFURLRef)CFReadStreamCopyProperty(stream, kCFStreamPropertyHTTPFinalURL);
                if (finalURL) {
                    cfurl_to_utf8_string(finalURL, effective_url_);
                    CFRelease(finalURL);
                }
                captured_effective_url = true;
            }
            total_read += (int)bytesRead;
        } else if (bytesRead == 0) {
            break;  // EOF
        } else {
            LOG_ERROR("CFReadStream 读取失败");
            CFReadStreamClose(stream);
            CFRelease(stream);
            return -1;
        }
    }
    
    CFReadStreamClose(stream);
    CFRelease(stream);
    
    if (total_read > 0) {
        total_downloaded_.fetch_add(total_read);
        if (progress_callback_) {
            progress_callback_(total_downloaded_.load(), content_length_);
        }
    }
    
    return total_read;
}

#elif defined(USE_WINHTTP)
// ==================== Windows WinHTTP 实现 ====================

int RangeDownloader::fetch_http_headers() {
    // 解析 URL
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    std::wstring wurl(url_.begin(), url_.end());
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
        LOG_ERROR("URL 解析失败");
        return -1;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    HINTERNET hSession = WinHttpOpen(L"HXCPlayer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        LOG_ERROR("WinHTTP 会话创建失败");
        return -1;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"HEAD", path.c_str(), NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    // 获取 Content-Length
    WCHAR contentLength[32] = {0};
    DWORD size = sizeof(contentLength);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, contentLength, &size, WINHTTP_NO_HEADER_INDEX)) {
        content_length_ = _wtoi64(contentLength);
    }

    support_range_ = true;

    {
        DWORD optLen = 0;
        WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, nullptr, &optLen);
        if (optLen >= sizeof(WCHAR)) {
            std::vector<wchar_t> wbuf(optLen / sizeof(WCHAR));
            DWORD optLen2 = optLen;
            if (WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, wbuf.data(), &optLen2)) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, nullptr, 0, nullptr, nullptr);
                if (n > 1) {
                    std::string u8(static_cast<size_t>(n - 1), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, &u8[0], n, nullptr, nullptr);
                    effective_url_ = std::move(u8);
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return 0;
}

int RangeDownloader::do_range_request(uint8_t* buffer, int64_t offset, int size) {
    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    std::wstring wurl(url_.begin(), url_.end());
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
        return -1;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    HINTERNET hSession = WinHttpOpen(L"HXCPlayer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return -1;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    // 设置 Range 头
    std::string rangeValue = "bytes=" + std::to_string(offset) + "-" + std::to_string(offset + size - 1);
    std::wstring wRange = L"Range: " + std::wstring(rangeValue.begin(), rangeValue.end());

    if (!WinHttpSendRequest(hRequest, wRange.c_str(), -1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    if (offset == 0) {
        DWORD optLen = 0;
        WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, nullptr, &optLen);
        if (optLen >= sizeof(WCHAR)) {
            std::vector<wchar_t> wbuf(optLen / sizeof(WCHAR));
            DWORD optLen2 = optLen;
            if (WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, wbuf.data(), &optLen2)) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, nullptr, 0, nullptr, nullptr);
                if (n > 1) {
                    std::string u8(static_cast<size_t>(n - 1), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, &u8[0], n, nullptr, nullptr);
                    effective_url_ = std::move(u8);
                }
            }
        }
    }

    // 读取数据
    int total_read = 0;
    DWORD bytes_read = 0;
    while (total_read < size) {
        if (abort_request_.load()) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return AVERROR_EXIT;
        }

        if (!WinHttpReadData(hRequest, buffer + total_read, size - total_read, &bytes_read)) {
            break;
        }

        if (bytes_read == 0) break;
        total_read += bytes_read;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (total_read > 0) {
        total_downloaded_.fetch_add(total_read);
        if (progress_callback_) {
            progress_callback_(total_downloaded_.load(), content_length_);
        }
    }

    return total_read;
}

#else  // USE_CURL
// ==================== Android/Linux CURL 实现 ====================

int RangeDownloader::fetch_http_headers() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("CURL 初始化失败");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms_);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        char* eff = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
        if (eff && eff[0]) {
            effective_url_ = eff;
        }
        double content_length;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
        content_length_ = static_cast<int64_t>(content_length);
        support_range_ = true;
    } else {
        LOG_ERROR("CURL HEAD 请求失败: ", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_cleanup(curl);
    return 0;
}

int RangeDownloader::do_range_request(uint8_t* buffer, int64_t offset, int size) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    std::vector<uint8_t> response_data;

    std::string range_header = "Range: bytes=" + std::to_string(offset) + "-" +
                                std::to_string(offset + size - 1);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, range_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms_);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);

    int bytes_read = -1;

    if (res == CURLE_OK) {
        if (offset == 0) {
            char* eff = nullptr;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
            if (eff && eff[0]) {
                effective_url_ = eff;
            }
        }
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 206 || http_code == 200) {
            bytes_read = std::min((int)response_data.size(), size);
            std::memcpy(buffer, response_data.data(), bytes_read);

            total_downloaded_.fetch_add(bytes_read);
            if (progress_callback_) {
                progress_callback_(total_downloaded_.load(), content_length_);
            }
        } else {
            LOG_ERROR("HTTP 错误码: ", http_code);
        }
    } else {
        LOG_ERROR("CURL 请求失败: ", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return bytes_read;
}

#endif  // USE_CFNETWORK / USE_WINHTTP / USE_CURL

int RangeDownloader::read_range(uint8_t* buffer, int64_t offset, int size) {
    if (abort_request_.load()) {
        return AVERROR_EXIT;
    }
    
    int retry_count = 0;
    
    while (retry_count <= max_retries_) {
        int result = do_range_request(buffer, offset, size);
        
        if (result >= 0) {
            return result;
        }
        
        if (retry_count < max_retries_) {
            int backoff_ms = std::min(4000, 300 * (1 << std::min(retry_count, 4)));
            LOG_WARNING("Range 请求失败，重试 ", retry_count + 1, "/", max_retries_,
                        "，退避 ", backoff_ms, " ms");
            retry_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            if (abort_request_.load()) {
                return AVERROR_EXIT;
            }
        } else {
            LOG_ERROR("Range 请求失败，已达到最大重试次数");
            return result;
        }
    }
    
    return -1;
}

void RangeDownloader::close() {
    url_.clear();
    effective_url_.clear();
    content_length_ = -1;
    support_range_ = false;
    total_downloaded_.store(0);
}

// ==================== HttpRangeDataSource 实现 ====================

HttpRangeDataSource::HttpRangeDataSource() 
    : downloader_(std::make_unique<RangeDownloader>()) {
}

HttpRangeDataSource::~HttpRangeDataSource() {
    close();
}

int HttpRangeDataSource::open(const std::string& url) {
    int ret = downloader_->open(url);
    if (ret < 0) {
        return ret;
    }
    
    file_size_ = downloader_->get_size();
    current_position_ = 0;
    
    // 分配缓存
    cache_buffer_ = std::make_unique<uint8_t[]>(cache_size_);
    cache_start_ = -1;
    cache_end_ = -1;
    
    return 0;
}

int HttpRangeDataSource::read(uint8_t* buffer, int size) {
    if (current_position_ >= file_size_ && file_size_ > 0) {
        return 0;  // EOF
    }
    
    int total_read = 0;
    
    while (total_read < size) {
        // 检查缓存是否命中
        if (cache_start_ >= 0 && 
            current_position_ >= cache_start_ && 
            current_position_ < cache_end_) {
            // 缓存命中
            int64_t cache_offset = current_position_ - cache_start_;
            int available = cache_end_ - current_position_;
            int to_copy = std::min(available, size - total_read);
            
            std::memcpy(buffer + total_read, 
                       cache_buffer_.get() + cache_offset, 
                       to_copy);
            
            current_position_ += to_copy;
            total_read += to_copy;
        } else {
            // 缓存未命中，重新下载
            int download_size = std::max((int)cache_size_, size - total_read);
            
            int bytes_read = downloader_->read_range(cache_buffer_.get(), 
                                                     current_position_, 
                                                     download_size);
            
            if (bytes_read < 0) {
                return bytes_read < 0 ? bytes_read : total_read;
            }
            
            if (bytes_read == 0) {
                break;  // EOF
            }
            
            // 更新缓存范围
            cache_start_ = current_position_;
            cache_end_ = current_position_ + bytes_read;

            // 如果是加密文件：对文件头 [0,99] 的数据进行解密后再写入缓存
            if (encrypted_file_ && cache_start_ < 100) {
                int64_t decrypt_end = std::min<int64_t>(cache_end_, 100);
                int decrypt_len = (int)(decrypt_end - cache_start_);
                if (decrypt_len > 0) {
                    for (int i = 0; i < decrypt_len; ++i) {
                        cache_buffer_.get()[i] = decrypt_first100_byte(cache_buffer_.get()[i]);
                    }
                }
            }
        }
    }
    
    return total_read;
}

int64_t HttpRangeDataSource::seek(int64_t offset, int whence) {
    int64_t new_position = current_position_;
    
    switch (whence) {
        case SEEK_SET:
            new_position = offset;
            break;
        case SEEK_CUR:
            new_position = current_position_ + offset;
            break;
        case SEEK_END:
            if (file_size_ > 0) {
                new_position = file_size_ + offset;
            } else {
                return -1;  // 不支持从末尾 seek
            }
            break;
        case AVSEEK_SIZE:
            return file_size_;
        default:
            return -1;
    }
    
    if (new_position < 0 || (file_size_ > 0 && new_position > file_size_)) {
        return -1;
    }
    
    current_position_ = new_position;
    
    // seek 后使缓存失效，强制从新位置重新下载
    cache_start_ = -1;
    cache_end_ = -1;
    
    return current_position_;
}

int64_t HttpRangeDataSource::size() {
    return file_size_;
}

void HttpRangeDataSource::close() {
    downloader_->close();
    cache_buffer_.reset();
    cache_start_ = -1;
    cache_end_ = -1;
    current_position_ = 0;
    file_size_ = -1;
}

bool HttpRangeDataSource::seekable() const {
    return downloader_->support_range() && file_size_ > 0;
}

// ==================== LocalFileDataSource 实现 ====================

LocalFileDataSource::LocalFileDataSource() = default;

LocalFileDataSource::~LocalFileDataSource() {
    close();
}

int LocalFileDataSource::open(const std::string& url) {
    close();

    fp_ = std::fopen(url.c_str(), "rb");
    if (!fp_) {
        LOG_ERROR("LocalFileDataSource 打开失败: ", url, " errno=", errno);
        return -1;
    }

    if (std::fseek(fp_, 0, SEEK_END) != 0) {
        LOG_ERROR("LocalFileDataSource fseek(SEEK_END) 失败 errno=", errno);
        close();
        return -1;
    }

    long sz = std::ftell(fp_);
    if (sz < 0) {
        LOG_ERROR("LocalFileDataSource ftell 失败 errno=", errno);
        close();
        return -1;
    }
    file_size_ = static_cast<int64_t>(sz);

    if (std::fseek(fp_, 0, SEEK_SET) != 0) {
        LOG_ERROR("LocalFileDataSource fseek(SEEK_SET) 失败 errno=", errno);
        close();
        return -1;
    }

    current_position_ = 0;
    return 0;
}

int LocalFileDataSource::read(uint8_t* buffer, int size) {
    if (!fp_ || size <= 0) {
        return -1;
    }

    if (file_size_ > 0 && current_position_ >= file_size_) {
        return 0; // EOF
    }

    int64_t before = current_position_;
    size_t n = std::fread(buffer, 1, static_cast<size_t>(size), fp_);
    if (n == 0) {
        if (std::feof(fp_)) {
            return 0;
        }
        if (std::ferror(fp_)) {
            LOG_ERROR("LocalFileDataSource fread 失败 errno=", errno);
            return -1;
        }
    }

    current_position_ += static_cast<int64_t>(n);

    // 对文件全局前 100 字节做解密（与网络模式一致）
    if (encrypted_file_ && before < 100) {
        int64_t end = before + static_cast<int64_t>(n);
        int64_t decrypt_end = std::min<int64_t>(end, 100);
        for (int64_t off = before; off < decrypt_end; ++off) {
            int idx = static_cast<int>(off - before);
            buffer[idx] = decrypt_first100_byte(buffer[idx]);
        }
    }

    return static_cast<int>(n);
}

int64_t LocalFileDataSource::seek(int64_t offset, int whence) {
    if (!fp_) return -1;

    if (whence == AVSEEK_SIZE) {
        return file_size_;
    }

    int origin;
    switch (whence) {
        case SEEK_SET: origin = SEEK_SET; break;
        case SEEK_CUR: origin = SEEK_CUR; break;
        case SEEK_END: origin = SEEK_END; break;
        default: return -1;
    }

    if (std::fseek(fp_, static_cast<long>(offset), origin) != 0) {
        return -1;
    }

    long pos = std::ftell(fp_);
    if (pos < 0) return -1;
    current_position_ = static_cast<int64_t>(pos);
    return current_position_;
}

int64_t LocalFileDataSource::size() {
    return file_size_;
}

void LocalFileDataSource::close() {
    if (fp_) {
        std::fclose(fp_);
        fp_ = nullptr;
    }
    current_position_ = 0;
    file_size_ = -1;
}

// ==================== CustomAVIOContext 实现 ====================

CustomAVIOContext::CustomAVIOContext(std::unique_ptr<ICustomDataSource> data_source,
                                     size_t buffer_size)
    : data_source_(std::move(data_source))
    , buffer_size_(buffer_size) {
    
    // 分配 AVIO 缓冲区
    avio_buffer_ = (uint8_t*)av_malloc(buffer_size_);
    if (!avio_buffer_) {
        LOG_ERROR("AVIO 缓冲区分配失败");
        return;
    }

    // 创建 AVIOContext
    avio_ctx_ = avio_alloc_context(
        avio_buffer_,
        buffer_size_,
        0,
        this,
        read_packet,
        nullptr,
        data_source_->seekable() ? seek : nullptr
    );

    if (!avio_ctx_) {
        LOG_ERROR("AVIOContext 创建失败");
        av_free(avio_buffer_);
        avio_buffer_ = nullptr;
    }
}

CustomAVIOContext::~CustomAVIOContext() {
    close();
}

int CustomAVIOContext::open(const std::string& url) {
    // 打开数据源（如果数据源已经通过外部 open 过，可以跳过这一步）
    int ret = data_source_->open(url);
    if (ret < 0) {
        LOG_ERROR("自定义数据源打开失败");
        return ret;
    }

    // 如果构造时已初始化 avio_ctx_，直接返回成功
    if (avio_ctx_) {
        return 0;
    }

    // 分配 AVIO 缓冲区
    avio_buffer_ = (uint8_t*)av_malloc(buffer_size_);
    if (!avio_buffer_) {
        LOG_ERROR("AVIO 缓冲区分配失败");
        return AVERROR(ENOMEM);
    }

    // 创建 AVIOContext
    avio_ctx_ = avio_alloc_context(
        avio_buffer_,
        buffer_size_,
        0,
        this,
        read_packet,
        nullptr,
        data_source_->seekable() ? seek : nullptr
    );

    if (!avio_ctx_) {
        LOG_ERROR("AVIOContext 创建失败");
        av_free(avio_buffer_);
        avio_buffer_ = nullptr;
        return AVERROR(ENOMEM);
    }
    
    // 设置 seekable 标志
    avio_ctx_->seekable = data_source_->seekable() ? AVIO_SEEKABLE_NORMAL : 0;
    
    LOG_INFO("CustomAVIOContext 创建成功");
    return 0;
}

void CustomAVIOContext::close() {
    if (avio_ctx_) {
        // FFmpeg 在探测、HLS 等路径下可能通过 ffio_realloc_buf 替换 s->buffer，
        // 原先传入 avio_alloc_context 的缓冲区已被 libavformat 释放；
        // 只能释放「当前」avio_ctx_->buffer，绝不能再次 av_free 成员 avio_buffer_。
        av_free(avio_ctx_->buffer);
        avio_ctx_->buffer = nullptr;
        avio_context_free(&avio_ctx_);
    } else if (avio_buffer_) {
        // 仅构造失败或尚未创建 avio_ctx_ 时，缓冲区仍由本类独占
        av_free(avio_buffer_);
    }
    avio_buffer_ = nullptr;

    if (data_source_) {
        data_source_->close();
    }
}

int CustomAVIOContext::read_packet(void* opaque, uint8_t* buf, int buf_size) {
    CustomAVIOContext* ctx = static_cast<CustomAVIOContext*>(opaque);
    
    int bytes_read = ctx->data_source_->read(buf, buf_size);
    
    if (bytes_read < 0) {
        return AVERROR(EIO);
    }
    
    return bytes_read > 0 ? bytes_read : AVERROR_EOF;
}

int64_t CustomAVIOContext::seek(void* opaque, int64_t offset, int whence) {
    CustomAVIOContext* ctx = static_cast<CustomAVIOContext*>(opaque);
    
    return ctx->data_source_->seek(offset, whence);
}

} // namespace hxcplayer
