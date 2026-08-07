/**
 * @file frame_queue.h
 * @brief 帧队列管理（参照 ffplay 实现）
 */

#ifndef YXVODPLAYER_FRAME_QUEUE_H
#define YXVODPLAYER_FRAME_QUEUE_H

#include "hxc_player_types.h"
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>

namespace hxcplayer {

/**
 * @brief 帧队列
 * 用于缓存解码后的音视频帧，参照 ffplay 的 FrameQueue 实现
 */
template<typename T>
class FrameQueue {
public:
    explicit FrameQueue(int max_size = 16)
        : max_size_(max_size)
        , rindex_(0)
        , windex_(0)
        , size_(0)
        , keep_last_(false)
        , rindex_shown_(0)
        , abort_(false) {
        frames_.resize(max_size);
    }
    
    ~FrameQueue() {
        flush();
    }
    
    // 获取可写帧
    T* peek_writable() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (size_ >= max_size_ && !abort_) {
            cond_write_.wait(lock);
        }
        
        if (abort_) {
            return nullptr;
        }
        
        return &frames_[windex_];
    }
    
    // 推入帧
    void push() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (++windex_ == max_size_) {
            windex_ = 0;
        }
        size_++;
        cond_read_.notify_one();
    }
    
    // 获取可读帧（当前帧）
    T* peek_readable() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (size_ - rindex_shown_ <= 0 && !abort_) {
            cond_read_.wait(lock);
        }
        
        if (abort_) {
            return nullptr;
        }
        
        return &frames_[rindex_];
    }

    // 原子地获取可读帧并对 AVFrame 取引用（在内部锁下完成 peek+av_frame_ref）。
    // 背景：peek_readable() 返回槽指针后会释放内部锁，调用方在锁外读取 frame->data 期间，
    // 另一线程的 flush() 可能 av_frame_free 释放该帧，导致 swr_convert/memcpy 访问已释放内存
    // 引发 SEGV。本方法在锁内对 AVFrame 取引用，调用方持有引用期间即使 flush 释放队列帧，
    // 底层缓冲区仍存活，避免 use-after-free。
    // 返回槽指针（用于 pts/serial 等元数据），*out_ref 填充为引用后的 AVFrame
    // （调用方负责 av_frame_free）。无可读帧或取引用失败时 *out_ref=nullptr 且返回 nullptr。
    T* peek_readable_frame_ref(AVFrame** out_ref) {
        std::unique_lock<std::mutex> lock(mutex_);

        while (size_ - rindex_shown_ <= 0 && !abort_) {
            cond_read_.wait(lock);
        }

        if (abort_ || size_ - rindex_shown_ <= 0) {
            if (out_ref) *out_ref = nullptr;
            return nullptr;
        }

        if (out_ref) {
            AVFrame* src = frames_[rindex_].frame;
            // av_frame_clone = av_frame_alloc + av_frame_ref，返回新 AVFrame（调用方用 av_frame_free 释放）。
            *out_ref = src ? av_frame_clone(src) : nullptr;
        }
        return &frames_[rindex_];
    }
    
    // 获取下一帧（不移动指针）
    T* peek_next() {
        std::lock_guard<std::mutex> lock(mutex_);
        return &frames_[(rindex_ + 1) % max_size_];
    }
    
    // 获取上一帧（不移动指针）
    T* peek_last() {
        std::lock_guard<std::mutex> lock(mutex_);
        return &frames_[rindex_];
    }

    // 非阻塞取当前可读帧（size > 0 时才返回，否则返回 nullptr）
    // 用于 Qt/UI 线程等不能阻塞的场景
    T* peek_last_nonblocking() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ - rindex_shown_ <= 0) {
            return nullptr;
        }
        return &frames_[rindex_];
    }
    
    // 移动到下一帧
    void next() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (keep_last_ && !rindex_shown_) {
            rindex_shown_ = 1;
            return;
        }
        
        // ⚠️ 释放当前帧的 AVFrame（防止内存泄漏！）
        if (frames_[rindex_].frame) {
            av_frame_free(&frames_[rindex_].frame);
            frames_[rindex_].frame = nullptr;
        }
        
        if (++rindex_ == max_size_) {
            rindex_ = 0;
        }
        size_--;
        cond_write_.notify_one();
    }
    
    // 获取队列大小
    int size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ - rindex_shown_;
    }
    
    // 获取剩余空间
    int nb_remaining() const {
        return size();
    }
    
    // 清空队列
    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        // ⚠️ 显式清空每个帧，触发 AVFrame 释放（重要！）
        for (int i = 0; i < max_size_; i++) {
            if (frames_[i].frame) {
                av_frame_free(&frames_[i].frame);
                frames_[i].frame = nullptr;
            }
        }
        rindex_ = 0;
        windex_ = 0;
        size_ = 0;
        rindex_shown_ = 0;
        
        // ⚠️ 唤醒等待的线程（可能在 peek_writable 中阻塞）
        cond_write_.notify_all();
        cond_read_.notify_all();
    }
    
    // 中止队列操作
    void abort() {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_ = true;
        cond_read_.notify_all();
        cond_write_.notify_all();
    }
    
    // 重新启动队列
    void restart() {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_ = false;
    }
    
    // 设置是否保留最后一帧
    void set_keep_last(bool keep) {
        keep_last_ = keep;
    }

private:
    std::vector<T> frames_;
    int max_size_;
    int rindex_;                // 读索引
    int windex_;                // 写索引
    int size_;                  // 当前大小
    bool keep_last_;            // 是否保留最后一帧
    int rindex_shown_;          // 是否已显示
    std::atomic<bool> abort_;   // 中止标志
    
    mutable std::mutex mutex_;
    std::condition_variable cond_read_;
    std::condition_variable cond_write_;
};

} // namespace hxcplayer

#endif // YXVODPLAYER_FRAME_QUEUE_H
