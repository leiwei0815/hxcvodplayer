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
    player_ = std::make_unique<yxplayer::PlayerCore>();
    
    // ⚠️ 设置默认配置（可以从 UI 修改）
    yxplayer::PlayerConfig config;
    config.start_time = 60.0;  // 默认从头开始，可通过 UI 修改
    player_->set_config(config);
    
    // 设置回调
    player_->set_state_changed_callback([this](yxplayer::PlayerState state) {
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
    openFile("https://111453136245362688.tenwiseacademy.cn/f325d6cebae3d4ddcfd73ecb63f1fb23/bd2a08a90684fa70b99d8401415a6ebd.mp4");
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
    if (state == yxplayer::PlayerState::Playing) {
        player_->pause();
        ui->playPauseButton->setText("播放");
    } else if (state == yxplayer::PlayerState::Paused) {
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
    if (state == yxplayer::PlayerState::Paused) {
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
    if (state != yxplayer::PlayerState::Playing) {
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
    
    // ⚠️ 计算帧与主时钟的差值
    double diff = frame_pts - master_clock;
    
    // 同步阈值
    const double SYNC_THRESHOLD_MIN = 0.04;  // 40ms
    const double SYNC_THRESHOLD_MAX = 0.1;   // 100ms
    const double NOSYNC_THRESHOLD = 10.0;    // 10s，超过则认为不同步
    
    if (std::fabs(diff) < NOSYNC_THRESHOLD) {
        if (diff <= -SYNC_THRESHOLD_MAX) {
            // 视频严重落后音频（超过 100ms），丢帧
            qDebug() << "视频落后，丢帧 diff=" << diff;
            video_queue->next();
            last_video_pts_ = frame_pts;
        } else if (diff >= SYNC_THRESHOLD_MIN) {
            // 视频领先音频（超过 40ms），等待下次检查
            // 不做任何操作，下次再检查
        } else {
            // 在合理范围内（-100ms ~ 40ms），显示帧
            video_widget_->updateFrame(video_queue);
            last_video_pts_ = frame_pts;
        }
    } else {
        // 时钟差异太大，可能 seek 了，直接显示
        video_widget_->updateFrame(video_queue);
        last_video_pts_ = frame_pts;
    }
}

void MainWindow::onStateChanged(yxplayer::PlayerState state) {
    switch (state) {
        case yxplayer::PlayerState::Playing:
            ui->playPauseButton->setText("暂停");
            ui->playPauseButton->setEnabled(true);
            ui->stopButton->setEnabled(true);
            ui->seekSlider->setEnabled(true);
            break;
            
        case yxplayer::PlayerState::Paused:
            ui->playPauseButton->setText("播放");
            break;
            
        case yxplayer::PlayerState::Stopped:
        case yxplayer::PlayerState::Error:
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

void MainWindow::onAspectRatioModeChanged(yxplayer::AspectRatioMode mode) {
    // ⚠️ 同步到核心层
    if (player_) {
        player_->set_aspect_ratio_mode(mode);
    }
    
    // ⚠️ 在状态栏显示当前模式
    QString modeText = (mode == yxplayer::AspectRatioMode::Fit) ? "适应模式" : "填充模式";
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
