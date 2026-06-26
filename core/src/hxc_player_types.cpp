/**
 * @file player_types.cpp
 * @brief 播放器类型实现
 */

#include "hxc_player_types.h"

extern "C" {
#include <libavutil/time.h>
}

#ifndef NO_SDL
#include <SDL2/SDL.h>
#endif

namespace hxcplayer {

double Clock::get_clock() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (paused) {
        return pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        // FFplay clock model:
        // pts_drift = pts - update_time, so current clock should be pts_drift + now.
        // Previous formula mistakenly canceled "time", returning almost fixed pts and
        // causing progress jitter/back-jump side effects in recovery/loading heuristics.
        return pts_drift + time;
    }
}

void Clock::set_clock(double pts, int serial) {
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(pts, serial, time);
}

void Clock::set_clock_at(double pts, int serial, double time) {
    std::lock_guard<std::mutex> lock(mutex_);
    this->pts = pts;
    this->last_updated = time;
    this->pts_drift = pts - time;
    this->serial = serial;
}

void Clock::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (paused) {
        return;
    }
    double time = av_gettime_relative() / 1000000.0;
    pts = pts_drift + time;
    last_updated = time;
    paused = true;
}

void Clock::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!paused) {
        return;
    }
    double time = av_gettime_relative() / 1000000.0;
    last_updated = time;
    pts_drift = pts - time;
    paused = false;
}

void Clock::sync_clock_to_slave(Clock* slave) {
    double clock = get_clock();
    double slave_clock = slave->get_clock();
    int slave_serial = 0;
    {
        std::lock_guard<std::mutex> lock(slave->mutex_);
        slave_serial = slave->serial;
    }
    
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > 0.1)) {
        set_clock(slave_clock, slave_serial);
    }
}

} // namespace hxcplayer
