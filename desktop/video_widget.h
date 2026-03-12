/**
 * @file video_widget.h
 * @brief 视频显示窗口
 */

#ifndef VIDEO_WIDGET_H
#define VIDEO_WIDGET_H

#include <QWidget>
#include <QImage>
#include <memory>
#include "hxc_frame_queue.h"
#include "hxc_player_types.h"

extern "C" {
#include <SDL2/SDL.h>
}

class VideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();
    
    void setVideoSize(int width, int height);
    void updateFrame(hxcplayer::FrameQueue<hxcplayer::VideoFrame>* video_queue);
    
    // ⚠️ 设置显示模式（使用核心层的枚举）
    void setAspectRatioMode(hxcplayer::AspectRatioMode mode);
    hxcplayer::AspectRatioMode getAspectRatioMode() const { return aspect_ratio_mode_; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    QSize sizeHint() const override;

signals:
    void aspectRatioModeChanged(hxcplayer::AspectRatioMode mode);

private:
    QImage convertFrameToImage(AVFrame* frame);
    QRect calculateDisplayRect();

private:
    QImage current_image_;
    int video_width_;
    int video_height_;
    
    struct SwsContext* sws_ctx_;
    AVFrame* rgb_frame_;
    uint8_t* rgb_buffer_;
    
    // ⚠️ 显示模式（从核心层同步）
    hxcplayer::AspectRatioMode aspect_ratio_mode_;
};

#endif // VIDEO_WIDGET_H
