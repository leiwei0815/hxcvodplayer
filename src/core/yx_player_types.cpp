/**
 * @file player_types.cpp
 * @brief 播放器类型实现
 */

#include "yx_player_types.h"

extern "C" {
#include <libavutil/time.h>
}

#ifndef NO_SDL
#include <SDL2/SDL.h>
#endif

namespace yxplayer {

double Clock::get_clock() const {
    if (paused) {
        return pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        return pts_drift + time - (time - last_updated);
    }
}

void Clock::set_clock(double pts, int serial) {
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(pts, serial, time);
}

void Clock::set_clock_at(double pts, int serial, double time) {
    this->pts = pts;
    this->last_updated = time;
    this->pts_drift = pts - time;
    this->serial = serial;
}

void Clock::sync_clock_to_slave(Clock* slave) {
    double clock = get_clock();
    double slave_clock = slave->get_clock();
    
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > 0.1)) {
        set_clock(slave_clock, slave->serial);
    }
}

} // namespace yxplayer
