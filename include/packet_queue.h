/**
 * @file packet_queue.h
 * @brief 数据包队列（参照 ffplay 实现）
 */

#ifndef YXVODPLAYER_PACKET_QUEUE_H
#define YXVODPLAYER_PACKET_QUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace yxplayer {

/**
 * @brief 数据包队列
 * 用于缓存解复用后的音视频数据包
 */
class PacketQueue {
public:
    PacketQueue() 
        : nb_packets_(0)
        , size_(0)
        , duration_(0)
        , abort_(false)
        , serial_(0) {
    }
    
    ~PacketQueue() {
        flush();
    }
    
    // 放入数据包
    int put(AVPacket* pkt) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (abort_) {
            return -1;
        }
        
        AVPacket* pkt_copy = av_packet_alloc();
        if (!pkt_copy) {
            return -1;
        }
        
        if (av_packet_ref(pkt_copy, pkt) < 0) {
            av_packet_free(&pkt_copy);
            return -1;
        }
        
        queue_.push(pkt_copy);
        nb_packets_++;
        size_ += pkt_copy->size;
        duration_ += pkt_copy->duration;
        
        cond_.notify_one();
        return 0;
    }
    
    // 放入空包（用于刷新）
    int put_nullpacket(int stream_index) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (abort_) {
            return -1;
        }
        
        // ⚠️ 直接创建并推入空包，不使用 av_packet_ref
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            return -1;
        }
        
        // 设置为空包（flush packet）
        pkt->data = nullptr;
        pkt->size = 0;
        pkt->stream_index = stream_index;
        
        queue_.push(pkt);
        nb_packets_++;
        
        cond_.notify_one();
        return 0;
    }
    
    // 获取数据包
    int get(AVPacket* pkt, bool block) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (true) {
            if (abort_) {
                return -1;
            }
            
            if (!queue_.empty()) {
                AVPacket* pkt_front = queue_.front();
                queue_.pop();
                
                av_packet_move_ref(pkt, pkt_front);
                av_packet_free(&pkt_front);
                
                nb_packets_--;
                size_ -= pkt->size;
                duration_ -= pkt->duration;
                
                return 1;
            } else if (!block) {
                return 0;
            } else {
                cond_.wait(lock);
            }
        }
    }
    
    // 清空队列
    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        while (!queue_.empty()) {
            AVPacket* pkt = queue_.front();
            queue_.pop();
            av_packet_free(&pkt);
        }
        
        nb_packets_ = 0;
        size_ = 0;
        duration_ = 0;
        serial_++;
    }
    
    // 中止队列操作
    void abort() {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_ = true;
        cond_.notify_all();
    }
    
    // 重新启动队列
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_ = false;
    }
    
    // 获取队列大小
    int get_nb_packets() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return nb_packets_;
    }
    
    int get_size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }
    
    int64_t get_duration() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return duration_;
    }
    
    int get_serial() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return serial_;
    }
    
    bool is_abort() const {
        return abort_;
    }

private:
    std::queue<AVPacket*> queue_;
    int nb_packets_;            // 包数量
    int size_;                  // 总大小（字节）
    int64_t duration_;          // 总时长
    std::atomic<bool> abort_;   // 中止标志
    int serial_;                // 序列号
    
    mutable std::mutex mutex_;
    std::condition_variable cond_;
};

} // namespace yxplayer

#endif // YXVODPLAYER_PACKET_QUEUE_H
