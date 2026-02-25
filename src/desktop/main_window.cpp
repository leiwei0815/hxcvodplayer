/**
 * @file main_window.cpp
 * @brief Qt 主窗口实现
 */

#include "main_window.h"
#include "ui_main_window.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTime>
#include <QDebug>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , is_seeking_(false)
    , last_video_pts_(0.0) {
    
    ui->setupUi(this);
    
    // ⚠️ 彻底断开 Qt 自动连接的所有信号
    QList<QSlider*> sliders = {ui->seekSlider, ui->volumeSlider};
    for (auto* slider : sliders) {
        disconnect(slider, nullptr, this, nullptr);
    }
    
    setupUI();
    
    // 创建播放器
    player_ = std::make_unique<hxcplayer::PlayerCore>();
    
    // ⚠️ 设置默认配置（可以从 UI 修改）
    hxcplayer::PlayerConfig config;
    config.start_time = 67.0;  // 默认从头开始，可通过 UI 修改
    player_->set_config(config);
    
    // 设置回调
    player_->set_state_changed_callback([this](hxcplayer::PlayerState state) {
        QMetaObject::invokeMethod(this, [this, state]() {
            onStateChanged(state);
        });
    });
    
    player_->set_error_callback([this](const std::string& error) {
        QMetaObject::invokeMethod(this, [this, error]() {
            onError(QString::fromStdString(error));
        });
    });
    
    // UI 更新定时器（只更新进度条等 UI 元素，不负责视频刷新）
    update_timer_ = new QTimer(this);
    connect(update_timer_, &QTimer::timeout, this, &MainWindow::updateUI);
    update_timer_->start(100);  // 100ms 更新一次进度条即可
//    
//    // ⚠️ 视频刷新定时器（高频率检查，由帧 PTS 控制实际刷新）
    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::refreshVideo);
    refresh_timer_->start(10);  // 10ms 检查一次（不是刷新频率！）
}

MainWindow::~MainWindow() {
    player_->close();
    delete ui;
}

void MainWindow::setupUI() {
    setWindowTitle("YXVodPlayer");
    setMinimumSize(800, 600);
    resize(1280, 720);
    
    // 启用拖放
    setAcceptDrops(true);
    
    // 创建视频显示窗口
    video_widget_ = new VideoWidget(this);
    setCentralWidget(video_widget_);
    
    // ⚠️ 设置 slider 属性，避免跳动
    ui->seekSlider->setTracking(true);  // 启用连续跟踪（默认就是 true）
    ui->seekSlider->setSingleStep(1);   // 最小步长
    ui->seekSlider->setPageStep(10);    // 翻页步长
    
    // ⚠️ 手动连接滑动条信号（使用新的槽函数名，避免自动连接）
    // seekSlider - 只连接需要的信号
    connect(ui->seekSlider, &QSlider::sliderPressed, this, &MainWindow::handleSeekSliderPressed, Qt::UniqueConnection);
    connect(ui->seekSlider, &QSlider::sliderReleased, this, &MainWindow::handleSeekSliderReleased, Qt::UniqueConnection);
    connect(ui->seekSlider, &QSlider::sliderMoved, this, &MainWindow::handleSeekSliderMoved, Qt::UniqueConnection);
    
    // volumeSlider - 只连接 valueChanged
    connect(ui->volumeSlider, &QSlider::valueChanged, this, &MainWindow::handleVolumeChanged, Qt::UniqueConnection);
    
    // ⚠️ 倍速选择框
    ui->speedComboBox->setCurrentIndex(2);  // 默认选择 1.0x
    connect(ui->speedComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &MainWindow::handleSpeedChanged, Qt::UniqueConnection);
    
    // ⚠️ 显示模式按钮
    ui->aspectRatioButton->setText("适应");  // 默认适应模式
    
    // ⚠️ 连接视频显示模式变化信号
    connect(video_widget_, &VideoWidget::aspectRatioModeChanged, 
            this, &MainWindow::onAspectRatioModeChanged);
    
    // 设置初始状态
    ui->playPauseButton->setEnabled(false);
    ui->stopButton->setEnabled(false);
//    ui->seekSlider->setEnabled(false);
    ui->volumeSlider->setValue(100);
}

void MainWindow::on_openButton_clicked() {
//    openFile("https://111453136245362688.tenwiseacademy.cn/f325d6cebae3d4ddcfd73ecb63f1fb23/bd2a08a90684fa70b99d8401415a6ebd.mp4");
    openFile("https://111453136245362688.tenwiseacademy.cn/6e05f006034f11e0772fd44df4beb686/4632d236ac2612c4729de505aa4fdab9.mp4");
//    QString filename = QFileDialog::getOpenFileName(
//        this,
//        "打开视频文件",
//        QString(),
//        "视频文件 (*.mp4 *.mkv *.avi *.flv *.mov *.wmv);;所有文件 (*.*)"
//    );
//    
//    if (!filename.isEmpty()) {
//        openFile(filename);
//    }
}

void MainWindow::on_playPauseButton_clicked() {
    if (!player_) return;
    
    auto state = player_->get_state();
    if (state == hxcplayer::PlayerState::Playing) {
        player_->pause();
        ui->playPauseButton->setText("播放");
    } else if (state == hxcplayer::PlayerState::Paused) {
        player_->play();
        ui->playPauseButton->setText("暂停");
    }
}

void MainWindow::on_stopButton_clicked() {
    if (player_) {
        player_->stop();
        ui->playPauseButton->setText("播放");
        ui->playPauseButton->setEnabled(false);
        ui->stopButton->setEnabled(false);
        ui->seekSlider->setEnabled(false);
        
        // 阻止信号
        ui->seekSlider->blockSignals(true);
        ui->seekSlider->setValue(0);
        ui->seekSlider->blockSignals(false);
        
        ui->timeLabel->setText("00:00 / 00:00");
    }
}

// ⚠️ 新的槽函数（重命名，避免 Qt 自动连接）
void MainWindow::handleSeekSliderPressed() {
    is_seeking_ = true;
    
    // ⚠️ 停止所有定时器，避免干扰
    if (update_timer_) update_timer_->stop();
    if (refresh_timer_) refresh_timer_->stop();
}

void MainWindow::handleSeekSliderReleased() {
    int value = ui->seekSlider->value();
    
    if (!player_) {
        is_seeking_ = false;
        // ⚠️ 重新启动定时器
        if (update_timer_) update_timer_->start(100);
        if (refresh_timer_) refresh_timer_->start(10);
        return;
    }
    
    double duration = player_->get_duration();
    if (duration > 0) {
        double position = (value / 1000.0) * duration;
        
        // ⚠️ 立即更新时间显示和 slider 位置，让用户看到最终位置
        ui->timeLabel->setText(formatTime(position) + " / " + formatTime(duration));
        
        // ⚠️ 立即手动设置 slider 到目标位置，防止 updateUI() 回弹
        // 使用 blockSignals 避免触发信号
        ui->seekSlider->blockSignals(true);
        ui->seekSlider->setValue(value);  // 确保停留在用户选择的位置
        ui->seekSlider->blockSignals(false);
        
        // 执行 seek
        player_->seek(position);
    }
    
    // ⚠️ 在 seek 调用后再恢复状态和定时器
    // 这样 updateUI() 即使获取到旧时钟，也不会覆盖我们手动设置的值
    is_seeking_ = false;
    
    // ⚠️ 重新启动定时器
    if (update_timer_) update_timer_->start(100);
    if (refresh_timer_) refresh_timer_->start(10);
}

void MainWindow::handleSeekSliderMoved(int position) {
    Q_UNUSED(position);
    // ⚠️ 拖动时不做任何操作，避免触发布局变化导致 slider 尺寸改变
    // 时间显示将在 sliderReleased 时更新
}

void MainWindow::handleVolumeChanged(int value) {
    if (player_) {
        player_->set_volume(value);
    }
}

void MainWindow::updateUI() {
    if (!player_) return;
    
    // ⚠️ 关键修复：拖动时完全跳过 updateUI，避免任何干扰
    if (is_seeking_) {
        return;  // 用户正在拖动，不做任何更新
    }
    
    auto state = player_->get_state();
    if (state == hxcplayer::PlayerState::Paused) {
        return;
    }
    
    // 更新进度条和时间
    double position = player_->get_position();
    double duration = player_->get_duration();
    
    if (duration > 0) {
        int value = static_cast<int>((position / duration) * 1000);
        int current_value = ui->seekSlider->value();
        
        // ⚠️ 只有值变化超过阈值时才更新，避免 seek 后的小幅抖动
        // seek 刚完成时，音频时钟可能还有几毫秒的延迟
        const int threshold = 2;  // 允许 0.2% 的误差（约 0.2 秒 / 100 秒视频）
        if (std::abs(value - current_value) > threshold) {
            // 阻止信号，避免触发 valueChanged
            ui->seekSlider->blockSignals(true);
            ui->seekSlider->setValue(value);
            ui->seekSlider->blockSignals(false);
        }
        
        // 更新时间标签
        ui->timeLabel->setText(formatTime(position) + " / " + formatTime(duration));
    }
}

// ⚠️ 新增：基于 PTS 的精确视频刷新（参考 ffplay）
void MainWindow::refreshVideo() {
    if (!player_) return;
    
    // ⚠️ 拖动时不执行任何操作
    if (is_seeking_) {
        return;
    }
    
    auto state = player_->get_state();
    if (state != hxcplayer::PlayerState::Playing) {
        return;
    }
    
    auto* video_queue = player_->get_video_queue();
    if (!video_queue || video_queue->size() <= 0) {
        return;
    }
    
    // 获取队列中的帧（不移除）
    auto* vf = video_queue->peek_readable();
    if (!vf || !vf->frame) {
        return;
    }
    
    // 获取主时钟（音频时钟）
    double master_clock = player_->get_position();
    double frame_pts = vf->pts;
    
    if (std::isnan(frame_pts)) {
        // 如果没有 PTS，直接显示
        video_widget_->updateFrame(video_queue);
        return;
    }
    
    // ⚠️ 获取播放速率
    double playback_rate = player_->get_playback_rate();
    
    // ⚠️ 计算帧与主时钟的差值
    double delay = frame_pts - master_clock;
    
    // ⚠️ 参考 ffplay：根据播放速率和帧持续时间调整同步策略
    // 帧持续时间（下一帧 PTS - 当前帧 PTS）
    double frame_duration = vf->duration;
    
    // ⚠️ 日志：原始帧持续时间
    static int duration_log_count = 0;
    if (++duration_log_count % 200 == 0) {
        qDebug() << "[帧持续] 原始duration:" << frame_duration 
                 << ", 是否有效:" << (frame_duration > 0 && !std::isnan(frame_duration));
    }
    
    // ⚠️ 修复：如果 duration 无效或太小，使用帧率估算
    if (frame_duration <= 0 || std::isnan(frame_duration) || frame_duration < 0.001) {
        // 如果没有 duration，使用帧率估算
        const auto& media_info = player_->get_media_info();
        if (media_info.video_fps > 0) {
            frame_duration = 1.0 / media_info.video_fps;
            if (duration_log_count % 200 == 0) {
                qDebug() << "[帧持续] 使用帧率计算:" << frame_duration 
                         << "秒 (fps=" << media_info.video_fps << ")";
            }
        } else {
            frame_duration = 0.04;  // 默认 25fps
            if (duration_log_count % 200 == 0) {
                qDebug() << "[帧持续] 使用默认值: 0.04秒 (25fps)";
            }
        }
    }
    
    // ⚠️ 关键：根据播放速率调整帧持续时间
    // 2.0x 时，每帧显示时间减半；0.5x 时，每帧显示时间加倍
    double adjusted_frame_duration = frame_duration / playback_rate;
    
    // ⚠️ 同步阈值（参考 ffplay AV_SYNC_THRESHOLD）
    const double AV_SYNC_THRESHOLD_MIN = 0.04;  // 最小同步阈值 40ms
    const double AV_SYNC_THRESHOLD_MAX = 0.1;   // 最大同步阈值 100ms
    double sync_threshold = std::max(AV_SYNC_THRESHOLD_MIN, 
                                     std::min(AV_SYNC_THRESHOLD_MAX, adjusted_frame_duration));
    
    // ⚠️ 日志：视频同步信息
    static int video_sync_count = 0;
    if (++video_sync_count % 100 == 0) {
        qDebug() << "[视频同步] PTS:" << frame_pts 
                 << ", 主时钟:" << master_clock
                 << ", 延迟:" << delay
                 << ", 速率:" << playback_rate << "x"
                 << ", 帧时长:" << frame_duration
                 << ", 调整后:" << adjusted_frame_duration
                 << ", 阈值:" << sync_threshold;
    }
    
    // ⚠️ 判断是否需要丢帧或等待
    if (delay <= -sync_threshold) {
        // 视频严重落后音频，丢帧
        if (video_sync_count % 20 == 0) {
            qDebug() << "[视频] 丢帧，延迟:" << delay << ", 阈值:" << -sync_threshold;
        }
        video_queue->next();
        last_video_pts_ = frame_pts;
    } else if (delay > sync_threshold) {
        // 视频领先音频太多，等待
        // 不做任何操作，下次再检查
    } else {
        // 在同步范围内，显示这一帧
        video_widget_->updateFrame(video_queue);
        last_video_pts_ = frame_pts;
    }
}

void MainWindow::onStateChanged(hxcplayer::PlayerState state) {
    switch (state) {
        case hxcplayer::PlayerState::Playing:
            ui->playPauseButton->setText("暂停");
            ui->playPauseButton->setEnabled(true);
            ui->stopButton->setEnabled(true);
            ui->seekSlider->setEnabled(true);
            break;
            
        case hxcplayer::PlayerState::Paused:
            ui->playPauseButton->setText("播放");
            break;
            
        case hxcplayer::PlayerState::Stopped:
        case hxcplayer::PlayerState::Error:
            ui->playPauseButton->setText("播放");
            ui->playPauseButton->setEnabled(false);
            ui->stopButton->setEnabled(false);
            ui->seekSlider->setEnabled(false);
            break;
            
        default:
            break;
    }
}

void MainWindow::onError(const QString& error) {
    QMessageBox::critical(this, "错误", error);
}

void MainWindow::onAspectRatioModeChanged(hxcplayer::AspectRatioMode mode) {
    // ⚠️ 同步到核心层
    if (player_) {
        player_->set_aspect_ratio_mode(mode);
    }
    
    // ⚠️ 更新按钮文本
    if (mode == hxcplayer::AspectRatioMode::Fit) {
        ui->aspectRatioButton->setText("适应");
    } else {
        ui->aspectRatioButton->setText("填充");
    }
    
    // ⚠️ 在状态栏显示当前模式
    QString modeText = (mode == hxcplayer::AspectRatioMode::Fit) ? "适应模式" : "填充模式";
    statusBar()->showMessage("显示模式: " + modeText, 2000);
}

void MainWindow::setStartTime(double seconds) {
    if (!player_) return;
    
    // ⚠️ 获取当前配置，修改开始时间，再设置回去
    auto config = player_->get_config();
    config.start_time = seconds;
    player_->set_config(config);
}

void MainWindow::openFile(const QString& filename) {
    if (!player_) {
        qWarning() << "播放器未初始化";
        return;
    }
    
    if (!video_widget_) {
        qWarning() << "视频窗口未初始化";
        return;
    }
    
    qDebug() << "正在打开文件:" << filename;
    
    // 关闭之前的文件
    player_->close();
    
    // 打开新文件
    int ret = player_->open(filename.toStdString());
    if (ret == 0) {
        // 设置窗口标题（网络 URL 显示简短信息）
        if (filename.startsWith("http://") || filename.startsWith("https://")) {
            setWindowTitle("YXVodPlayer - 网络视频");
        } else {
            setWindowTitle("YXVodPlayer - " + QFileInfo(filename).fileName());
        }
        
        // 更新 UI
        ui->playPauseButton->setEnabled(true);
        ui->stopButton->setEnabled(true);
        ui->seekSlider->setEnabled(true);
        ui->playPauseButton->setText("暂停");
        
        // 获取媒体信息
        const auto& info = player_->get_media_info();
        if (info.video_width > 0 && info.video_height > 0) {
            qDebug() << "视频尺寸:" << info.video_width << "x" << info.video_height;
            video_widget_->setVideoSize(info.video_width, info.video_height);
        }
        
        qDebug() << "文件打开成功";
    } else {
        qWarning() << "文件打开失败，错误码:" << ret;
        QMessageBox::warning(this, "打开失败", 
            QString("无法打开文件：%1\n错误码：%2").arg(filename).arg(ret));
    }
}

QString MainWindow::formatTime(double seconds) {
    int total_seconds = static_cast<int>(seconds);
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int secs = total_seconds % 60;
    
    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    } else {
        return QString("%1:%2")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (player_) {
        player_->close();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        if (!urlList.isEmpty()) {
            QString filename = urlList.first().toLocalFile();
            openFile(filename);
        }
    }
}

// ⚠️ 倍速变化处理
void MainWindow::handleSpeedChanged(int index) {
    if (!player_) {
        return;
    }
    
    // 根据选择的索引设置播放速率
    double rate = 1.0;
    switch (index) {
        case 0: rate = 0.5;  break;  // 0.5x
        case 1: rate = 0.75; break;  // 0.75x
        case 2: rate = 1.0;  break;  // 1.0x (正常)
        case 3: rate = 1.25; break;  // 1.25x
        case 4: rate = 1.5;  break;  // 1.5x
        case 5: rate = 2.0;  break;  // 2.0x
        default: rate = 1.0; break;
    }
    
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    qDebug() << "[UI] 用户选择播放速率:" << rate << "x (索引:" << index << ")";
    qDebug() << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
    
    player_->set_playback_rate(rate);
    
    // 验证设置是否成功
    double actual_rate = player_->get_playback_rate();
    qDebug() << "[UI] 实际播放速率:" << actual_rate << "x";
}

// ⚠️ 显示模式按钮点击处理
void MainWindow::on_aspectRatioButton_clicked() {
    if (!video_widget_) {
        return;
    }
    
    // 获取当前模式
    hxcplayer::AspectRatioMode current_mode = video_widget_->getAspectRatioMode();
    
    // 切换模式
    hxcplayer::AspectRatioMode new_mode;
    if (current_mode == hxcplayer::AspectRatioMode::Fit) {
        new_mode = hxcplayer::AspectRatioMode::Fill;
        ui->aspectRatioButton->setText("填充");
    } else {
        new_mode = hxcplayer::AspectRatioMode::Fit;
        ui->aspectRatioButton->setText("适应");
    }
    
    // 设置新模式
    video_widget_->setAspectRatioMode(new_mode);
    
    qDebug() << "[UI] 切换显示模式:" 
             << (new_mode == hxcplayer::AspectRatioMode::Fit ? "适应" : "填充");
}
