/**
 * @file video_widget.cpp
 * @brief 视频显示窗口实现
 */

#include "video_widget.h"
#include <QPainter>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QActionGroup>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
    , video_width_(0)
    , video_height_(0)
    , sws_ctx_(nullptr)
    , rgb_frame_(nullptr)
    , rgb_buffer_(nullptr)
    , aspect_ratio_mode_(hxcplayer::AspectRatioMode::Fit) {  // ⚠️ 默认 Fit 模式
    
    setMinimumSize(320, 240);
    setStyleSheet("background-color: black;");
    setAttribute(Qt::WA_OpaquePaintEvent);
}

VideoWidget::~VideoWidget() {
    if (rgb_frame_) {
        av_frame_free(&rgb_frame_);
    }
    if (rgb_buffer_) {
        av_free(rgb_buffer_);
    }
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
    }
}

void VideoWidget::setVideoSize(int width, int height) {
    if (width == video_width_ && height == video_height_) {
        return;
    }
    
    video_width_ = width;
    video_height_ = height;
    
    // 清理旧的转换上下文
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    
    if (rgb_frame_) {
        av_frame_free(&rgb_frame_);
        rgb_frame_ = nullptr;
    }
    
    if (rgb_buffer_) {
        av_free(rgb_buffer_);
        rgb_buffer_ = nullptr;
    }
    
    // 分配新的缓冲区
    if (width > 0 && height > 0) {
        rgb_frame_ = av_frame_alloc();
        rgb_frame_->format = AV_PIX_FMT_RGB24;
        rgb_frame_->width = width;
        rgb_frame_->height = height;
        
        int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
        rgb_buffer_ = (uint8_t*)av_malloc(buffer_size);
        
        av_image_fill_arrays(rgb_frame_->data, rgb_frame_->linesize,
                           rgb_buffer_, AV_PIX_FMT_RGB24, width, height, 1);
    }
    
    updateGeometry();
}

void VideoWidget::updateFrame(hxcplayer::FrameQueue<hxcplayer::VideoFrame>* video_queue) {
    if (!video_queue) return;
    
    // 检查队列中是否有帧
    if (video_queue->size() <= 0) return;
    
    auto* vf = video_queue->peek_readable();
    if (!vf || !vf->frame) return;
    
    // ⚠️ 这里可以添加 PTS 检查，但由于 video_thread 已经做了同步
    // 我们只需要确保按顺序显示即可
    
    AVFrame* frame = vf->frame;
    
    // 如果视频尺寸发生变化，重新初始化
    if (frame->width != video_width_ || frame->height != video_height_) {
        setVideoSize(frame->width, frame->height);
    }
    
    // 转换图像格式
    if (!sws_ctx_) {
        sws_ctx_ = sws_getContext(
            frame->width, frame->height, (AVPixelFormat)frame->format,
            video_width_, video_height_, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
    }
    
    if (sws_ctx_ && rgb_frame_) {
        // 执行格式转换
        sws_scale(sws_ctx_, 
                 (const uint8_t* const*)frame->data, frame->linesize, 
                 0, frame->height,
                 rgb_frame_->data, rgb_frame_->linesize);
        
        // 转换为 QImage
        current_image_ = QImage(rgb_frame_->data[0], 
                               video_width_, 
                               video_height_,
                               rgb_frame_->linesize[0], 
                               QImage::Format_RGB888).copy();
        
        // 触发重绘
        update();
    }
    
    // 移动到下一帧
    video_queue->next();
}

QImage VideoWidget::convertFrameToImage(AVFrame* frame) {
    if (!frame) {
        return QImage();
    }
    
    // 这个方法可以用于其他格式的转换
    return QImage();
}

void VideoWidget::setAspectRatioMode(hxcplayer::AspectRatioMode mode) {
    if (aspect_ratio_mode_ != mode) {
        aspect_ratio_mode_ = mode;
        update();  // 触发重绘
    }
}

QRect VideoWidget::calculateDisplayRect() {
    if (video_width_ <= 0 || video_height_ <= 0) {
        return rect();
    }
    
    QRect widget_rect = rect();
    double widget_aspect = (double)widget_rect.width() / widget_rect.height();
    double video_aspect = (double)video_width_ / video_height_;
    
    QRect display_rect;
    
    if (aspect_ratio_mode_ == hxcplayer::AspectRatioMode::Fit) {
        // ⚠️ Fit 模式：等比缩放，保持完整画面，可能有黑边
        if (widget_aspect > video_aspect) {
            // 窗口更宽，以高度为准
            int display_width = (int)(widget_rect.height() * video_aspect);
            int x_offset = (widget_rect.width() - display_width) / 2;
            display_rect = QRect(x_offset, 0, display_width, widget_rect.height());
        } else {
            // 窗口更高，以宽度为准
            int display_height = (int)(widget_rect.width() / video_aspect);
            int y_offset = (widget_rect.height() - display_height) / 2;
            display_rect = QRect(0, y_offset, widget_rect.width(), display_height);
        }
    } else {
        // ⚠️ Fill 模式：等比拉伸填充，无黑边，画面会被裁剪
        if (widget_aspect > video_aspect) {
            // 窗口更宽，以宽度为准（会裁剪上下）
            int display_height = (int)(widget_rect.width() / video_aspect);
            int y_offset = (widget_rect.height() - display_height) / 2;
            display_rect = QRect(0, y_offset, widget_rect.width(), display_height);
        } else {
            // 窗口更高，以高度为准（会裁剪左右）
            int display_width = (int)(widget_rect.height() * video_aspect);
            int x_offset = (widget_rect.width() - display_width) / 2;
            display_rect = QRect(x_offset, 0, display_width, widget_rect.height());
        }
    }
    
    return display_rect;
}

void VideoWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    
    if (!current_image_.isNull()) {
        QRect display_rect = calculateDisplayRect();
        painter.drawImage(display_rect, current_image_);
    }
}

void VideoWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    emit resized(width(), height());
    update();
}

void VideoWidget::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    
    // ⚠️ 创建显示模式子菜单
    QMenu* aspectRatioMenu = menu.addMenu("显示模式");
    
    QActionGroup* modeGroup = new QActionGroup(this);
    
    QAction* fitAction = aspectRatioMenu->addAction("适应（Fit）");
    fitAction->setCheckable(true);
    fitAction->setChecked(aspect_ratio_mode_ == hxcplayer::AspectRatioMode::Fit);
    fitAction->setData(static_cast<int>(hxcplayer::AspectRatioMode::Fit));
    modeGroup->addAction(fitAction);
    
    QAction* fillAction = aspectRatioMenu->addAction("填充（Fill）");
    fillAction->setCheckable(true);
    fillAction->setChecked(aspect_ratio_mode_ == hxcplayer::AspectRatioMode::Fill);
    fillAction->setData(static_cast<int>(hxcplayer::AspectRatioMode::Fill));
    modeGroup->addAction(fillAction);
    
    // ⚠️ 连接信号
    connect(fitAction, &QAction::triggered, [this]() {
        setAspectRatioMode(hxcplayer::AspectRatioMode::Fit);
        emit aspectRatioModeChanged(hxcplayer::AspectRatioMode::Fit);
    });
    
    connect(fillAction, &QAction::triggered, [this]() {
        setAspectRatioMode(hxcplayer::AspectRatioMode::Fill);
        emit aspectRatioModeChanged(hxcplayer::AspectRatioMode::Fill);
    });
    
    menu.exec(event->globalPos());
}

QSize VideoWidget::sizeHint() const {
    if (video_width_ > 0 && video_height_ > 0) {
        return QSize(video_width_, video_height_);
    }
    return QSize(800, 600);
}
