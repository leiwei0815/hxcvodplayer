#include "player_window.h"
#include "ui_player_window.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QMetaObject>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QPainter>
#include <QEvent>
#include <QTextEdit>
#include <QCheckBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <vector>

// ==================== OpenMediaDialog 实现 ====================

OpenMediaDialog::OpenMediaDialog(QWidget *parent)
    : QDialog(parent) {
    
    setWindowTitle("打开媒体");
    setMinimumWidth(500);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 类型选择
    QGroupBox* typeGroup = new QGroupBox("选择媒体类型");
    QVBoxLayout* typeLayout = new QVBoxLayout(typeGroup);
    
    localFileRadio_ = new QRadioButton("本地文件");
    networkUrlRadio_ = new QRadioButton("网络地址 (HTTP/HTTPS/RTSP)");
    localFileRadio_->setChecked(true);
    
    typeLayout->addWidget(localFileRadio_);
    typeLayout->addWidget(networkUrlRadio_);
    mainLayout->addWidget(typeGroup);
    
    // 路径输入
    QHBoxLayout* pathLayout = new QHBoxLayout();
    QLabel* pathLabel = new QLabel("路径:");
    pathEdit_ = new QLineEdit();
    pathEdit_->setPlaceholderText("请选择文件或输入网络地址...");
    browseButton_ = new QPushButton("浏览...");
    
    pathLayout->addWidget(pathLabel);
    pathLayout->addWidget(pathEdit_, 1);
    pathLayout->addWidget(browseButton_);
    mainLayout->addLayout(pathLayout);
    
    // 示例说明
    QLabel* hintLabel = new QLabel(
        "网络地址示例:\n"
        "  HTTP: http://example.com/video.mp4\n"
        "  HTTPS: https://example.com/stream.m3u8\n"
        "  RTSP: rtsp://example.com:554/stream"
    );
    hintLabel->setStyleSheet("QLabel { color: gray; font-size: 10pt; }");
    mainLayout->addWidget(hintLabel);
    
    // 按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    QPushButton* okButton = new QPushButton("确定");
    QPushButton* cancelButton = new QPushButton("取消");
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);
    
    // 连接信号
    connect(localFileRadio_, &QRadioButton::toggled, this, &OpenMediaDialog::onTypeChanged);
    connect(browseButton_, &QPushButton::clicked, this, &OpenMediaDialog::onBrowseClicked);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    onTypeChanged();
}

QString OpenMediaDialog::getMediaPath() const {
    return pathEdit_->text().trimmed();
}

bool OpenMediaDialog::isLocalFile() const {
    return localFileRadio_->isChecked();
}

void OpenMediaDialog::onTypeChanged() {
    bool isLocal = localFileRadio_->isChecked();
    browseButton_->setEnabled(isLocal);
    
    if (isLocal) {
        pathEdit_->setPlaceholderText("请选择本地视频文件...");
    } else {
        pathEdit_->setPlaceholderText("请输入网络地址 (http://, https://, rtsp://)...");
    }
}

void OpenMediaDialog::onBrowseClicked() {
    QString filename = QFileDialog::getOpenFileName(
        this,
        "选择视频文件",
        "",
        "视频文件 (*.mp4 *.mkv *.avi *.flv *.mov *.m3u8 *.ts);;所有文件 (*.*)"
    );
    
    if (!filename.isEmpty()) {
        pathEdit_->setText(filename);
    }
}

// ==================== PlayerWindow 实现 ====================

PlayerWindow::PlayerWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::PlayerWindow)
    , player_(nullptr)
    , is_seeking_(false)
    , duration_(0.0) {
    
    ui->setupUi(this);
    setWindowTitle("HXCPlayer SDK 测试 - 支持本地文件和网络流");
    
    // ========== 配置视频容器为原生窗口 ==========
    // 不再使用 SDL 自动渲染，改用 Qt 的 paintEvent
    // ui->videoContainer->setAttribute(Qt::WA_NativeWindow);
    // ui->videoContainer->setAttribute(Qt::WA_PaintOnScreen);
    // ui->videoContainer->setAttribute(Qt::WA_OpaquePaintEvent);
    ui->videoContainer->setAttribute(Qt::WA_OpaquePaintEvent);  // 只设置这个即可
    ui->videoContainer->setStyleSheet("background-color: black;");
    
    // 安装事件过滤器，用于自定义绘制
    ui->videoContainer->installEventFilter(this);
    
    qDebug() << "videoContainer 配置为 Qt 绘制模式";
    // ==========================================
    
    // ========== 启用文件日志（通过 SDK API）==========
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/hxcplayer_logs";
    QDir().mkpath(logDir);  // 确保目录存在
    
    // 通过 SDK 的 C API 启用日志
    player_core_set_log_level(0);  // 0 = DEBUG 级别
    player_core_enable_file_logging(logDir.toUtf8().constData(), "sdk_test");
    
    // 获取实际的日志文件路径
    const char* log_file_path = player_core_get_current_log_file();
    QString logFile = QString::fromUtf8(log_file_path ? log_file_path : "未知");
    
    qDebug() << "日志文件:" << logFile;
    
    QMessageBox::information(this, "日志信息", 
        QString("日志文件位置:\n%1\n\n请在测试后查看此文件\n\n关键日志查找：\n"
                "- '视频刷新线程已启动'\n"
                "- 'SDL 渲染器初始化成功'\n"
                "- '设置渲染窗口'\n"
                "- 'ERROR' (查看所有错误)").arg(logFile));
    // ====================================
    
    // 创建播放器（使用 SDK 的 C API）
    player_ = player_core_create();
    if (!player_) {
        QMessageBox::critical(this, "错误", "无法创建播放器");
        return;
    }
    
    // ========== 设置为手动渲染模式（不使用 SDL）==========
    player_core_set_render_mode(player_, RENDER_MODE_MANUAL);
    
    // 创建刷新定时器，定期从播放器获取帧并绘制
    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, [this]() {
        ui->videoContainer->update();  // 触发 paintEvent
    });
    refresh_timer_->start(16);  // ~60 FPS
    qDebug() << "使用手动渲染模式 + Qt paintEvent";
    // ====================================
    
    // ========== 设置回调（替代定时器）==========
    // 状态变化回调
    player_core_set_state_changed_callback(player_, [](PlayerStateC state, void* user_data) {
        PlayerWindow* self = static_cast<PlayerWindow*>(user_data);
        QMetaObject::invokeMethod(self, "onStateChanged", Qt::QueuedConnection);
    }, this);
    
    // 错误回调
    player_core_set_error_callback(player_, [](int error_code, const char* error, void* user_data) {
        PlayerWindow* self = static_cast<PlayerWindow*>(user_data);
        QString errorMsg = QString::fromUtf8(error);
        QMetaObject::invokeMethod(self, [self, errorMsg]() {
            QMessageBox::critical(self, "播放错误", errorMsg);
        }, Qt::QueuedConnection);
    }, this);
    
    // 播放进度回调（替代定时器）
    player_core_set_position_changed_callback(player_, [](double position, void* user_data) {
        PlayerWindow* self = static_cast<PlayerWindow*>(user_data);
        QMetaObject::invokeMethod(self, "onProgressChanged", Qt::QueuedConnection);
    }, this);
    
    // 播放完成回调
    player_core_set_playback_completed_callback(
        player_,
        [](void* user_data) {
        PlayerWindow* self = static_cast<PlayerWindow*>(user_data);
        QMetaObject::invokeMethod(self, "onPlaybackFinished", Qt::QueuedConnection);
    }, this);
    // ==========================================
    
    // 初始化 UI 状态
    ui->playButton->setEnabled(false);
    ui->pauseButton->setEnabled(false);
    ui->stopButton->setEnabled(false);
    ui->seekSlider->setEnabled(false);
    ui->volumeSlider->setValue(100);
    
    qDebug() << "播放器初始化完成，使用回调机制（无定时器）";
}

bool PlayerWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == ui->videoContainer && event->type() == QEvent::Paint) {
        QPainter painter(ui->videoContainer);
        painter.fillRect(ui->videoContainer->rect(), Qt::black);
        
        if (player_) {
            // 尝试获取新帧（RGB 格式）
            int width, height, linesize;

            // 先用最小缓冲区探测实际分辨率（buffer_size=0 会返回 -2 表示缓冲区太小，但不取帧）
            // 策略：先尝试当前缓冲区大小，若返回 -2 则按实际尺寸扩容后重试
            if (rgb_buffer_.empty()) {
                rgb_buffer_.resize(1920 * 1080 * 3);  // 初始 1080p 大小
            }

            int ret = player_core_get_video_frame_rgb(player_, rgb_buffer_.data(), (int)rgb_buffer_.size(),
                                                      &width, &height, &linesize);
            if (ret == -2) {
                // 缓冲区不足，按实际分辨率扩容后重取
                int needed = width * height * 3;
                if (needed > 0) {
                    rgb_buffer_.resize(needed);
                    qDebug() << "[Render] 缓冲区扩容至" << needed << "bytes (" << width << "x" << height << ")";
                    ret = player_core_get_video_frame_rgb(player_, rgb_buffer_.data(), (int)rgb_buffer_.size(),
                                                          &width, &height, &linesize);
                }
            }

            if (ret == 0) {
                // 成功获取并转换了帧，创建 QImage（copy 避免数据被覆盖）
                current_frame_ = QImage(rgb_buffer_.data(), width, height, linesize,
                                       QImage::Format_RGB888).copy();
                static int frame_log_count = 0;
                if (++frame_log_count <= 5 || frame_log_count % 300 == 0) {
                    qDebug() << "[Render] 成功获取帧 #" << frame_log_count
                             << " size=" << width << "x" << height;
                }
            }
            
            // 绘制当前帧
            if (!current_frame_.isNull()) {
                // 计算显示矩形（保持宽高比）
                QRect widget_rect = ui->videoContainer->rect();
                QSize frame_size = current_frame_.size();
                
                if (!frame_size.isEmpty()) {
                    double widget_aspect = (double)widget_rect.width() / widget_rect.height();
                    double frame_aspect = (double)frame_size.width() / frame_size.height();
                    
                    QRect display_rect;
                    if (widget_aspect > frame_aspect) {
                        // 窗口更宽，以高度为准
                        int display_width = (int)(widget_rect.height() * frame_aspect);
                        int x_offset = (widget_rect.width() - display_width) / 2;
                        display_rect = QRect(x_offset, 0, display_width, widget_rect.height());
                    } else {
                        // 窗口更高，以宽度为准
                        int display_height = (int)(widget_rect.width() / frame_aspect);
                        int y_offset = (widget_rect.height() - display_height) / 2;
                        display_rect = QRect(0, y_offset, widget_rect.width(), display_height);
                    }
                    
                    painter.drawImage(display_rect, current_frame_);
                }
            }
        }
        
        return true;
    }
    
    return QMainWindow::eventFilter(obj, event);
}

PlayerWindow::~PlayerWindow() {
    if (player_) {
        player_core_stop(player_);
        player_core_destroy(player_);
    }
    
    // 禁用文件日志（刷新并关闭日志文件）
    player_core_disable_file_logging();
    
    delete ui;
}

void PlayerWindow::on_openButton_clicked() {
    // 显示自定义对话框
    OpenMediaDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    
    QString mediaPath = dialog.getMediaPath();
    if (mediaPath.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入有效的媒体路径");
        return;
    }
    
    // 停止当前播放
    player_core_stop(player_);
    
    // 打开新媒体
    qDebug() << "正在打开:" << mediaPath;
    int ret = player_core_open(player_, mediaPath.toUtf8().constData());
    if (ret < 0) {
        QString errorMsg = dialog.isLocalFile() 
            ? "无法打开文件: " + mediaPath 
            : "无法连接到网络地址: " + mediaPath;
        QMessageBox::critical(this, "错误", errorMsg);
        return;
    }
    
    // 获取时长
    duration_ = player_core_get_duration(player_);
    
    // 启用控制按钮
    ui->playButton->setEnabled(true);
    ui->pauseButton->setEnabled(true);
    ui->stopButton->setEnabled(true);
    ui->seekSlider->setEnabled(true);
    
    // 更新状态栏
    QString typeStr = dialog.isLocalFile() ? "本地文件" : "网络流";
    ui->statusLabel->setText(QString("%1: %2").arg(typeStr).arg(mediaPath));
    
    // 自动开始播放
    player_core_play(player_);
    
    qDebug() << "媒体已打开，时长:" << duration_ << "秒";
}

void PlayerWindow::on_playButton_clicked() {
    player_core_play(player_);
}

void PlayerWindow::on_pauseButton_clicked() {
    player_core_pause(player_);
}

void PlayerWindow::on_stopButton_clicked() {
    player_core_stop(player_);
}

void PlayerWindow::on_volumeSlider_valueChanged(int value) {
    if (player_) {
        player_core_set_volume(player_, value / 100.0);
    }
}

void PlayerWindow::on_seekSlider_sliderPressed() {
    is_seeking_ = true;
}

void PlayerWindow::on_seekSlider_sliderReleased() {
    if (player_ && duration_ > 0) {
        double position = duration_ * ui->seekSlider->value() / 1000.0;
        player_core_seek(player_, position);
    }
    is_seeking_ = false;
}

void PlayerWindow::on_seekSlider_sliderMoved(int position) {
    if (player_ && duration_ > 0) {
        double seek_time = duration_ * position / 1000.0;
        ui->timeLabel->setText(formatTime(seek_time) + " / " + formatTime(duration_));
    }
}

// ========== 回调触发的 UI 更新槽函数 ==========

void PlayerWindow::onProgressChanged() {
    if (!player_ || is_seeking_) return;
    
    double position = player_core_get_position(player_);
    updatePlaybackInfo(position);
}

void PlayerWindow::onStateChanged() {
    if (!player_) return;
    
    PlayerStateC state = player_core_get_state(player_);
    
    QString stateText;
    switch (state) {
        case PLAYER_STATE_IDLE:     stateText = "空闲"; break;
        case PLAYER_STATE_OPENING:  stateText = "打开中..."; break;
        case PLAYER_STATE_PLAYING:  stateText = "播放中"; break;
        case PLAYER_STATE_PAUSED:   stateText = "已暂停"; break;
        case PLAYER_STATE_STOPPED:  stateText = "已停止"; break;
        case PLAYER_STATE_ERROR:    stateText = "错误"; break;
        default:                    stateText = "未知"; break;
    }
    ui->stateLabel->setText("状态: " + stateText);
    
    qDebug() << "状态变化:" << stateText;
}

void PlayerWindow::onPlaybackFinished() {
    qDebug() << "播放完成";
    ui->stateLabel->setText("状态: 播放完成");
    QMessageBox::information(this, "提示", "视频播放完成");
}

// ========== 辅助函数 ==========

void PlayerWindow::updatePlaybackInfo(double position) {
    if (duration_ > 0) {
        int slider_pos = (int)(position * 1000 / duration_);
        ui->seekSlider->setValue(slider_pos);
        ui->timeLabel->setText(formatTime(position) + " / " + formatTime(duration_));
    }
}

QString PlayerWindow::formatTime(double seconds) {
    int hours = (int)(seconds / 3600);
    int minutes = (int)((seconds - hours * 3600) / 60);
    int secs = (int)(seconds - hours * 3600 - minutes * 60);
    
    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    } else {
        return QString("%1:%2")
            .arg(minutes)
            .arg(secs, 2, 10, QChar('0'));
    }
}

// ==================== SecureHLS 测试入口 ====================

void PlayerWindow::on_secureHlsButton_clicked() {
    SecureHLSDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QString m3u8Url = dlg.getM3u8Url().trimmed();
    if (m3u8Url.isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写 m3u8 地址");
        return;
    }

    // 将多行 headers 文本转换为 FFmpeg 所需格式（每行末加 \r\n）
    QString rawHeaders = dlg.getSecureHeaders().trimmed();
    QString ffmpegHeaders;
    if (!rawHeaders.isEmpty()) {
        QStringList lines = rawHeaders.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                ffmpegHeaders += trimmed + "\r\n";
            }
        }
    }

    qDebug() << "[SecureHLS] m3u8 URL :" << m3u8Url;
    qDebug() << "[SecureHLS] headers  :" << ffmpegHeaders;

    // 停止当前播放
    player_core_stop(player_);

    // 调用 SecureHLS 模式打开
    PlayerDataSourceC source{};
    source.url            = m3u8Url.toUtf8().constData();
    source.start_position = 0.0;
    source.mode           = PLAYER_DATA_SOURCE_MODE_SECURE_HLS;
    source.encrypted_file = 0;

    QByteArray headerBytes = ffmpegHeaders.toUtf8();
    source.secure_headers = headerBytes.isEmpty() ? nullptr : headerBytes.constData();

    PlayerDataSourceConfigC cfg{};
    cfg.timeout_ms       = 30000;
    cfg.max_retries      = 3;
    cfg.cache_size       = 2 * 1024 * 1024;
    cfg.avio_buffer_size = 64 * 1024;

    int ret = player_core_open_with_mode(player_, &source, &cfg);
    if (ret < 0) {
        QMessageBox::critical(this, "SecureHLS 错误",
            QString("player_core_open_with_mode 失败，错误码: %1\n\n"
                    "常见原因：\n"
                    "1. m3u8 URL 不可访问\n"
                    "2. key 接口未返回 16 字节二进制原始密钥\n"
                    "3. 鉴权 header 缺失或格式错误\n"
                    "4. 本地测试服务器未启动（运行 start_test_server.bat）").arg(ret));
        return;
    }

    duration_ = player_core_get_duration(player_);
    ui->playButton->setEnabled(true);
    ui->pauseButton->setEnabled(true);
    ui->stopButton->setEnabled(true);
    ui->seekSlider->setEnabled(true);
    ui->statusLabel->setText("SecureHLS: " + m3u8Url);

    player_core_play(player_);
    qDebug() << "[SecureHLS] 播放已启动";
}

// ==================== SecureHLSDialog 实现 ====================

SecureHLSDialog::SecureHLSDialog(QWidget *parent)
    : QDialog(parent) {

    setWindowTitle("SecureHLS / HLS AES-128 测试");
    setMinimumWidth(560);

    QVBoxLayout* root = new QVBoxLayout(this);

    // ---- 快捷填充按钮行 ----
    QHBoxLayout* quickFillLayout = new QHBoxLayout();
    localTestCheck_ = new QCheckBox("使用本地测试服务器（127.0.0.1:8765）");
    QPushButton* fillRealBtn = new QPushButton("填充真实后台参数");
    fillRealBtn->setStyleSheet("QPushButton { background:#52c41a; color:white; border-radius:4px; padding:4px 10px; }");
    quickFillLayout->addWidget(localTestCheck_);
    quickFillLayout->addStretch();
    quickFillLayout->addWidget(fillRealBtn);
    root->addLayout(quickFillLayout);

    hintLabel_ = new QLabel(
        "<b>本地测试服务器说明：</b><br>"
        "运行项目根目录下 <code>win-sdk-example/start_test_server.bat</code><br>"
        "服务器会在本机生成 AES-128 加密 HLS 流，无需真实 CDN。<br>"
        "key 接口返回 <b>16 字节 raw binary</b>，用于验证解密链路。"
    );
    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet("QLabel { background:#fffbe6; border:1px solid #ffe58f; "
                              "padding:6px; border-radius:4px; }");
    root->addWidget(hintLabel_);

    connect(fillRealBtn, &QPushButton::clicked, this, &SecureHLSDialog::fillRealServerDefaults);

    // ---- 表单 ----
    QFormLayout* form = new QFormLayout();

    m3u8UrlEdit_ = new QLineEdit();
    m3u8UrlEdit_->setPlaceholderText("http://127.0.0.1:8765/stream.m3u8");
    form->addRow("m3u8 地址：", m3u8UrlEdit_);

    headersEdit_ = new QTextEdit();
    headersEdit_->setFixedHeight(100);
    headersEdit_->setPlaceholderText(
        "每行一个 header，格式：Key: Value\n"
        "例如：\n"
        "X-Playback-Session: ps_test_123\n"
        "X-File-Id: f_demo_001"
    );
    form->addRow("鉴权 Headers：", headersEdit_);

    root->addLayout(form);

    // ---- 验证按钮（打开 key 接口检测） ----
    verifyBtn_ = new QPushButton("检测 key 接口（curl 验证）");
    verifyBtn_->setToolTip("在命令行执行 curl 请求 key 接口，检查返回是否为 16 字节");
    root->addWidget(verifyBtn_);

    // ---- 关键说明 ----
    QLabel* warnLabel = new QLabel(
        "<b>⚠️ 解密失败最常见原因：</b><br>"
        "1. key 接口返回的不是 <b>16 字节原始二进制</b>（例如返回了 JSON / Base64 / Hex 字符串）<br>"
        "2. m3u8 中 <code>EXT-X-KEY URI</code> 与实际 key 地址不符<br>"
        "3. FFmpeg 请求 key 时 header 未透传（查看日志中 'SecureHLS 注入 headers 成功'）<br>"
        "4. IV 与加密时不一致（若未指定 IV，FFmpeg 默认用分片序号）"
    );
    warnLabel->setWordWrap(true);
    warnLabel->setStyleSheet("QLabel { background:#fff1f0; border:1px solid #ffa39e; "
                             "padding:6px; border-radius:4px; font-size:9pt; }");
    root->addWidget(warnLabel);

    // ---- 按钮 ----
    QDialogButtonBox* bbox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bbox->button(QDialogButtonBox::Ok)->setText("开始播放");
    bbox->button(QDialogButtonBox::Cancel)->setText("取消");
    root->addWidget(bbox);

    connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(localTestCheck_, &QCheckBox::toggled, this, &SecureHLSDialog::onUseLocalToggled);
    connect(verifyBtn_, &QPushButton::clicked, this, &SecureHLSDialog::onVerifyClicked);
}

void SecureHLSDialog::onUseLocalToggled(bool checked) {
    if (checked) {
        fillLocalDefaults();
    }
}

void SecureHLSDialog::fillLocalDefaults() {
    m3u8UrlEdit_->setText("http://127.0.0.1:8765/stream.m3u8");
    headersEdit_->setPlainText(
        "X-Playback-Session: ps_test_local_123\r\n"
        "X-File-Id: f_demo_001\r\n"
    );
}

void SecureHLSDialog::fillRealServerDefaults() {
    localTestCheck_->setChecked(false);
    m3u8UrlEdit_->setText(
        "https://111453136245362688.tenwiseacademy.cn"
        "/111453136245362688/8jwu5vj74orbp7itf9138eiy1t23kl80a1"
        "/a6c917r96qvxm23w.m3u8"
    );
    headersEdit_->setPlainText(
        "P-HX-SecretID: 30d4e4cc44029ec36a9c1e3e35139723\r\n"
        "P-HX-FileId: 114290945964511233\r\n"
        "P-HX-Timestamp: 1777466855\r\n"
        "P-HX-Sign: 072d04f3c56ff94655bb441e180faffec722e1df68a5adb9a9a2a1df31ea32de\r\n"
        "P-HX-Terminal-Type: iOS\r\n"
    );
    hintLabel_->setText(
        "<b>已填充真实后台参数</b><br>"
        "URL: <code>111453136245362688.tenwiseacademy.cn/...a6c917r96qvxm23w.m3u8</code><br>"
        "注意：<b>P-HX-Timestamp</b> 可能已过期，若播放失败请更新签名参数后重试。"
    );
    hintLabel_->setStyleSheet("QLabel { background:#f6ffed; border:1px solid #b7eb8f; "
                              "padding:6px; border-radius:4px; }");
}

void SecureHLSDialog::onVerifyClicked() {
    QString url = m3u8UrlEdit_->text().trimmed();
    // 从 m3u8 URL 推断 key 地址
    // 本地服务器规范：key 在 /key 路由
    QString keyUrl;
    if (url.contains("127.0.0.1:8765")) {
        keyUrl = "http://127.0.0.1:8765/key";
    } else {
        keyUrl = url; // 用户自行替换
    }

    QString cmd = QString(
        "start cmd /k \"curl -v -o key_check.bin \"%1\" && "
        "echo. && echo === key 文件字节数（必须=16）=== && "
        "for %%A in (key_check.bin) do echo %%~zA bytes && pause\""
    ).arg(keyUrl);

    qDebug() << "[SecureHLS] 执行 key 检测:" << cmd;
    QMessageBox::information(this, "key 接口检测",
        QString("将打开命令行执行 curl 请求：\n%1\n\n"
                "请确认输出 <16 bytes> 才是正确的 raw 二进制 key。\n"
                "若显示 JSON/Base64/大于16字节 → 服务端返回格式有误！").arg(keyUrl));
    system(cmd.toLocal8Bit().constData());
}

QString SecureHLSDialog::getM3u8Url() const {
    return m3u8UrlEdit_->text();
}

QString SecureHLSDialog::getSecureHeaders() const {
    return headersEdit_->toPlainText();
}

bool SecureHLSDialog::useLocalTestServer() const {
    return localTestCheck_->isChecked();
}
