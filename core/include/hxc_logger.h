/**
 * @file logger.h
 * @brief 增强的日志系统（支持文件写入、日志轮转、线程安全、自动清理、异步写入）
 */

#ifndef YXVODPLAYER_LOGGER_H
#define YXVODPLAYER_LOGGER_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include <cstring>
#include <mutex>
#include <memory>
#include <chrono>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>

// 文件系统操作
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Android Logcat 支持
#ifdef __ANDROID__
#include <android/log.h>
#define ANDROID_LOG_TAG "PlayerCore"
#endif

namespace hxcplayer {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR_LEVEL  // Windows 平台 ERROR 是宏，改为 ERROR_LEVEL
};

// 日志消息结构（用于异步队列）
struct LogMessage {
    std::string timestamp;
    std::string level;
    std::string message;
    
    LogMessage(const std::string& ts, const std::string& lvl, const std::string& msg)
        : timestamp(ts), level(lvl), message(msg) {}
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    
    // 设置日志级别
    void set_level(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }
    
    LogLevel get_level() const {
        return level_;
    }
    
    // 启用文件日志
    void enable_file_logging(const std::string& log_dir, const std::string& prefix = "hxcplayer") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        log_dir_ = log_dir;
        log_prefix_ = prefix;
        file_logging_enabled_ = true;
        
        // 启动异步写入线程
        if (!async_thread_running_) {
            async_thread_running_ = true;
            async_thread_ = std::thread(&Logger::async_write_thread, this);
        }
        
        // 打开日志文件
        open_log_file();
        
        // 写入文件头（同步写入，确保立即完成）
        if (log_file_.is_open()) {
            log_file_ << "========================================" << std::endl;
            log_file_ << "HXCPlayer Log Started" << std::endl;
            log_file_ << "Time: " << get_timestamp() << std::endl;
            log_file_ << "========================================" << std::endl;
            log_file_.flush();
        }
        
        // 清理超过保留期限的旧日志文件（调用内部实现，避免重复加锁）
        cleanup_old_logs_internal();
    }
    
    // 禁用文件日志
    void disable_file_logging() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            file_logging_enabled_ = false;
        }
        
        // 停止异步写入线程
        if (async_thread_running_) {
            async_thread_running_ = false;
            async_cv_.notify_one();
            
            if (async_thread_.joinable()) {
                async_thread_.join();
            }
        }
        
        // 写入文件尾并关闭（处理剩余的日志消息）
        std::lock_guard<std::mutex> lock(mutex_);
        flush_async_queue();
        
        if (log_file_.is_open()) {
            log_file_ << "========================================" << std::endl;
            log_file_ << "HXCPlayer Log Ended" << std::endl;
            log_file_ << "Time: " << get_timestamp() << std::endl;
            log_file_ << "========================================" << std::endl;
            log_file_.close();
        }
    }
    
    // 设置最大日志文件大小（字节），超过后自动轮转
    void set_max_file_size(size_t max_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_file_size_ = max_size;
    }
    
    // 设置日志保留天数（默认7天）
    void set_log_retention_days(int days) {
        std::lock_guard<std::mutex> lock(mutex_);
        log_retention_days_ = days;
    }
    
    // 获取当前日志文件路径
    std::string get_current_log_file() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_log_file_;
    }
    
    /** 当前文件日志目录（未调用 enable_file_logging 时为空） */
    std::string get_log_dir() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return log_dir_;
    }
    
    // 手动清理旧日志文件
    int cleanup_old_logs() {
        std::lock_guard<std::mutex> lock(mutex_);
        return cleanup_old_logs_internal();
    }
    
    template<typename... Args>
    void debug(Args&&... args) {
        if (level_ <= LogLevel::DEBUG) {
            log("DEBUG", std::forward<Args>(args)...);
        }
    }
    
    template<typename... Args>
    void info(Args&&... args) {
        if (level_ <= LogLevel::INFO) {
            log("INFO", std::forward<Args>(args)...);
        }
    }
    
    template<typename... Args>
    void warning(Args&&... args) {
        if (level_ <= LogLevel::WARNING) {
            log("WARNING", std::forward<Args>(args)...);
        }
    }
    
    template<typename... Args>
    void error(Args&&... args) {
        if (level_ <= LogLevel::ERROR_LEVEL) {
            log("ERROR", std::forward<Args>(args)...);
        }
    }
    
    // 记录带来源位置的日志（用于调试）
    template<typename... Args>
    void debug_with_location(const char* file, int line, const char* func, Args&&... args) {
        if (level_ <= LogLevel::DEBUG) {
            std::ostringstream oss;
            oss << "[" << get_filename(file) << ":" << line << "] [" << func << "] ";
            ((oss << args), ...);
            log("DEBUG", oss.str());
        }
    }

    template<typename... Args>
    void info_with_location(const char* file, int line, const char* func, Args&&... args) {
        if (level_ <= LogLevel::INFO) {
            std::ostringstream oss;
            oss << "[" << get_filename(file) << ":" << line << "] [" << func << "] ";
            ((oss << args), ...);
            log("INFO", oss.str());
        }
    }

    template<typename... Args>
    void warning_with_location(const char* file, int line, const char* func, Args&&... args) {
        if (level_ <= LogLevel::WARNING) {
            std::ostringstream oss;
            oss << "[" << get_filename(file) << ":" << line << "] [" << func << "] ";
            ((oss << args), ...);
            log("WARNING", oss.str());
        }
    }
    
    template<typename... Args>
    void error_with_location(const char* file, int line, const char* func, Args&&... args) {
        if (level_ <= LogLevel::ERROR_LEVEL) {
            std::ostringstream oss;
            oss << "[" << get_filename(file) << ":" << line << "] [" << func << "] ";
            ((oss << args), ...);
            log("ERROR", oss.str());
        }
    }

private:
    Logger() 
        : level_(LogLevel::INFO)
        , file_logging_enabled_(false)
        , max_file_size_(10 * 1024 * 1024)  // 默认 10MB
        , current_file_size_(0)
        , log_prefix_("hxcplayer")
        , log_retention_days_(7)  // 默认保留 7 天
        , write_count_(0)
        , async_thread_running_(false)
    {}
    
    ~Logger() {
        disable_file_logging();
    }
    
    // 打开日志文件
    void open_log_file() {
        // 关闭旧文件
        if (log_file_.is_open()) {
            log_file_.close();
        }
        
        // 生成日志文件名：prefix_YYYYMMDD_HHMMSS.log
        std::string filename = log_dir_ + "/" + log_prefix_ + "_" + get_date_time_string() + ".log";
        current_log_file_ = filename;
        current_file_size_ = 0;
        
        log_file_.open(filename, std::ios::out | std::ios::app);
    }
    
    // 检查并轮转日志文件
    void rotate_log_if_needed() {
        if (current_file_size_ >= max_file_size_) {
            open_log_file();
        }
    }
    
    // 清理超过保留期限的旧日志文件（内部实现，已加锁）
    int cleanup_old_logs_internal() {
        if (log_dir_.empty() || log_prefix_.empty()) {
            return 0;
        }
        
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto cutoff_time = now - std::chrono::hours(24 * log_retention_days_);
        
        int deleted_count = 0;
        
#ifdef _WIN32
        // Windows 实现
        std::string search_path = log_dir_ + "/" + log_prefix_ + "_*.log";
        WIN32_FIND_DATAA find_data;
        HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::string filepath = log_dir_ + "/" + find_data.cFileName;
                    
                    // 跳过当前正在使用的日志文件
                    if (filepath == current_log_file_) {
                        continue;
                    }
                    
                    // 获取文件修改时间
                    FILETIME ft = find_data.ftLastWriteTime;
                    ULARGE_INTEGER ull;
                    ull.LowPart = ft.dwLowDateTime;
                    ull.HighPart = ft.dwHighDateTime;
                    
                    // 转换为 time_point
                    auto file_time = std::chrono::system_clock::from_time_t(
                        (ull.QuadPart - 116444736000000000ULL) / 10000000ULL
                    );
                    
                    // 检查是否超过保留期限
                    if (file_time < cutoff_time) {
                        if (DeleteFileA(filepath.c_str())) {
                            deleted_count++;
                        }
                    }
                }
            } while (FindNextFileA(hFind, &find_data));
            FindClose(hFind);
        }
#else
        // POSIX (Linux/macOS/Android) 实现
        DIR* dir = opendir(log_dir_.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string filename = entry->d_name;
                
                // 只处理匹配前缀的 .log 文件
                if (filename.find(log_prefix_) == 0 && 
                    filename.find(".log") != std::string::npos) {
                    
                    std::string filepath = log_dir_ + "/" + filename;
                    
                    // 跳过当前正在使用的日志文件
                    if (filepath == current_log_file_) {
                        continue;
                    }
                    
                    // 获取文件修改时间
                    struct stat file_stat;
                    if (stat(filepath.c_str(), &file_stat) == 0) {
                        auto file_time = std::chrono::system_clock::from_time_t(file_stat.st_mtime);
                        
                        // 检查是否超过保留期限
                        if (file_time < cutoff_time) {
                            if (unlink(filepath.c_str()) == 0) {
                                deleted_count++;
                            }
                        }
                    }
                }
            }
            closedir(dir);
        }
#endif
        
        return deleted_count;
    }
    
    // 异步写入线程
    void async_write_thread() {
        while (async_thread_running_ || !async_queue_.empty()) {
            std::unique_lock<std::mutex> lock(async_mutex_);
            
            // 等待新的日志消息或停止信号
            async_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !async_queue_.empty() || !async_thread_running_;
            });
            
            // 批量处理队列中的消息
            while (!async_queue_.empty()) {
                LogMessage msg = async_queue_.front();
                async_queue_.pop();
                lock.unlock();
                
                // 写入文件（不持锁，避免阻塞日志调用）
                write_log_to_file(msg);
                
                lock.lock();
            }
        }
    }
    
    // 写入单条日志到文件
    void write_log_to_file(const LogMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (log_file_.is_open()) {
            std::string log_line = "[" + msg.timestamp + "] [" + msg.level + "] " + msg.message + "\n";
            log_file_ << log_line;
            
            // 定期 flush（每写入一定数量后，而不是每次都 flush）
            current_file_size_ += log_line.size();
            write_count_++;
            
            if (write_count_ >= 10) {  // 每10条日志 flush 一次
                log_file_.flush();
                write_count_ = 0;
            }
            
            rotate_log_if_needed();
        }
    }
    
    // 刷新异步队列（在禁用日志时调用）
    void flush_async_queue() {
        std::lock_guard<std::mutex> lock(async_mutex_);
        while (!async_queue_.empty()) {
            LogMessage msg = async_queue_.front();
            async_queue_.pop();
            
            if (log_file_.is_open()) {
                std::string log_line = "[" + msg.timestamp + "] [" + msg.level + "] " + msg.message + "\n";
                log_file_ << log_line;
            }
        }
        
        if (log_file_.is_open()) {
            log_file_.flush();
        }
    }
    
    // 获取时间戳字符串
    std::string get_timestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&time_t_now));
        
        char result[64];
        snprintf(result, sizeof(result), "%s.%03d", buffer, (int)ms.count());
        return result;
    }
    
    // 获取日期时间字符串（用于文件名）
    std::string get_date_time_string() const {
        time_t now = time(nullptr);
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", localtime(&now));
        return buffer;
    }
    
    // 从完整路径中提取文件名
    const char* get_filename(const char* path) const {
        const char* filename = strrchr(path, '/');
        if (!filename) filename = strrchr(path, '\\');
        return filename ? filename + 1 : path;
    }
    
    template<typename... Args>
    void log(const char* level_str, Args&&... args) {
        // 构建消息内容
        std::ostringstream oss;
        ((oss << args), ...);
        std::string message = oss.str();
        
        // 获取时间戳
        std::string timestamp = get_timestamp();
        
#ifdef __ANDROID__
        // Android 平台使用 Logcat（同步，但很快）
        android_LogPriority priority;
        if (strcmp(level_str, "DEBUG") == 0) {
            priority = ANDROID_LOG_DEBUG;
        } else if (strcmp(level_str, "INFO") == 0) {
            priority = ANDROID_LOG_INFO;
        } else if (strcmp(level_str, "WARNING") == 0) {
            priority = ANDROID_LOG_WARN;
        } else {
            priority = ANDROID_LOG_ERROR;
        }
        __android_log_print(priority, ANDROID_LOG_TAG, "%s", message.c_str());
#else
        // 其他平台使用标准输出（同步，但很快）
        std::cerr << "[" << timestamp << "] [" << level_str << "] " << message << std::endl;
#endif
        
        // 文件日志使用异步写入（不阻塞播放线程）
        if (file_logging_enabled_) {
            std::lock_guard<std::mutex> lock(async_mutex_);
            async_queue_.emplace(timestamp, level_str, message);
            async_cv_.notify_one();
        }
    }
    
    mutable std::mutex mutex_;
    LogLevel level_;
    
    // 文件日志相关
    bool file_logging_enabled_;
    std::string log_dir_;
    std::string log_prefix_;
    std::string current_log_file_;
    std::ofstream log_file_;
    size_t max_file_size_;
    size_t current_file_size_;
    int log_retention_days_;  // 日志保留天数
    int write_count_;  // 写入计数，用于定期 flush
    
    // 异步写入相关
    std::thread async_thread_;
    std::atomic<bool> async_thread_running_;
    std::queue<LogMessage> async_queue_;
    std::mutex async_mutex_;
    std::condition_variable async_cv_;
};

// 调用位置信息：优先使用包含类名/函数签名的信息
#if defined(__clang__) || defined(__GNUC__)
#define HXC_LOG_CALLSITE_FUNC __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#define HXC_LOG_CALLSITE_FUNC __FUNCSIG__
#else
#define HXC_LOG_CALLSITE_FUNC __FUNCTION__
#endif

// 便捷宏（默认包含文件/行号/函数签名）
//#define LOG_DEBUG(...) hxcplayer::Logger::instance().debug_with_location(__FILE__, __LINE__, HXC_LOG_CALLSITE_FUNC, __VA_ARGS__)
//#define LOG_INFO(...) hxcplayer::Logger::instance().info_with_location(__FILE__, __LINE__, HXC_LOG_CALLSITE_FUNC, __VA_ARGS__)
//#define LOG_WARNING(...) hxcplayer::Logger::instance().warning_with_location(__FILE__, __LINE__, HXC_LOG_CALLSITE_FUNC, __VA_ARGS__)
//#define LOG_ERROR(...) hxcplayer::Logger::instance().error_with_location(__FILE__, __LINE__, HXC_LOG_CALLSITE_FUNC, __VA_ARGS__)
#define LOG_DEBUG(...) hxcplayer::Logger::instance().debug(__VA_ARGS__)
#define LOG_INFO(...) hxcplayer::Logger::instance().info(__VA_ARGS__)
#define LOG_WARNING(...) hxcplayer::Logger::instance().warning(__VA_ARGS__)
#define LOG_ERROR(...) hxcplayer::Logger::instance().error(__VA_ARGS__)

// 带位置信息的日志宏
#define LOG_DEBUG_LOC(...) hxcplayer::Logger::instance().debug_with_location(__FILE__, __LINE__, HXC_LOG_CALLSITE_FUNC, __VA_ARGS__)
#define LOG_ERROR_LOC(...) hxcplayer::Logger::instance().error_with_location(__FILE__, __LINE__, HXC_LOG_CALLSITE_FUNC, __VA_ARGS__)

} // namespace hxcplayer

#endif // YXVODPLAYER_LOGGER_H
