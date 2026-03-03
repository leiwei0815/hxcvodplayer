/**
 * @file logger.h
 * @brief 简单的日志系统
 */

#ifndef YXVODPLAYER_LOGGER_H
#define YXVODPLAYER_LOGGER_H

#include <iostream>
#include <sstream>
#include <string>
#include <ctime>
#include <cstring>

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
    ERROR
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    
    void set_level(LogLevel level) {
        level_ = level;
    }
    
    LogLevel get_level() const {
        return level_;
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
        if (level_ <= LogLevel::ERROR) {
            log("ERROR", std::forward<Args>(args)...);
        }
    }

private:
    Logger() : level_(LogLevel::INFO) {}
    
    template<typename... Args>
    void log(const char* level_str, Args&&... args) {
        std::ostringstream oss;
        
        // 构建消息内容
        ((oss << args), ...);
        std::string message = oss.str();
        
#ifdef __ANDROID__
        // Android 平台使用 Logcat
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
        // 其他平台使用标准输出
        // 时间戳
        time_t now = time(nullptr);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        
        std::cerr << "[" << timestamp << "] [" << level_str << "] " << message << std::endl;
#endif
    }
    
    LogLevel level_;
};

// 便捷宏
#define LOG_DEBUG(...) hxcplayer::Logger::instance().debug(__VA_ARGS__)
#define LOG_INFO(...) hxcplayer::Logger::instance().info(__VA_ARGS__)
#define LOG_WARNING(...) hxcplayer::Logger::instance().warning(__VA_ARGS__)
#define LOG_ERROR(...) hxcplayer::Logger::instance().error(__VA_ARGS__)

} // namespace hxcplayer

#endif // YXVODPLAYER_LOGGER_H
