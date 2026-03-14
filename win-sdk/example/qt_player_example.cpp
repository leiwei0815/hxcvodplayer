/**
 * @file qt_player_example.cpp
 * @brief HXCPlayer SDK Qt 自动渲染示例
 * 
 * 演示如何在 Qt 应用中使用 HXCPlayer SDK 的自动渲染功能
 */

#include "qt_player_example.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTimer>

// 定义 DLL 导入
#define HXCPLAYER_DLL_IMPORTS
#include "hxcplayer_sdk.h"

QtPlayerExample::QtPlayerExample(QWidget *parent)
    : QWidget(parent)
    , player_(nullptr)
    , videoWidget_(nullptr)
    , refreshTimer_(nullptr)
{
    setupUI();
    initPlayer();
}

QtPlayerExample::~QtPlayerExample()
{
    cleanupPlayer();
}

void QtPlayerExample::setupUI()
{
    setWindowTitle("HXCPlayer Qt Example");
    resize(800, 600);
    
    // 创建主布局
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 视频显示区域
    videoWidget_ = new QWidget(this);
    videoWidget_->setMinimumSize(640, 480);
    videoWidget_->setStyleSheet("background-color: black;");
    mainLayout->addWidget(videoWidget_);
    
    // 进度条
    progressSlider_ = new QSlider(Qt::Horizontal, this);
    progressSlider_->setRange(0, 1000);
    mainLayout->addWidget(progressSlider_);
    
    // 时间显示
    timeLabel_ = new QLabel("0:00 / 0:00", this);
    mainLayout->addWidget(timeLabel_);
    
    // 控制按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    QPushButton* btnOpen = new QPushButton("打开", this);
    QPushButton* btnPlay = new QPushButton("播放", this);
    QPushButton* btnPause = new QPushButton("暂停", this);
    QPushButton* btnStop = new QPushButton("停止", this);
    
    buttonLayout->addWidget(btnOpen);
    buttonLayout->addWidget(btnPlay);
    buttonLayout->addWidget(btnPause);
    buttonLayout->addWidget(btnStop);
    buttonLayout->addStretch();
    
    mainLayout->addLayout(buttonLayout);
    
    // 连接信号
    connect(btnOpen, &QPushButton::clicked, this, &QtPlayerExample::onOpenClicked);
    connect(btnPlay, &QPushButton::clicked, this, &QtPlayerExample::onPlayClicked);
    connect(btnPause, &QPushButton::clicked, this, &QtPlayerExample::onPauseClicked);
    connect(btnStop, &QPushButton::clicked, this, &QtPlayerExample::onStopClicked);
    
    // 创建刷新定时器
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &QtPlayerExample::onRefreshTimer);
    refreshTimer_->start(16);  // 约 60 FPS
    
    // 创建进度更新定时器
    QTimer* progressTimer = new QTimer(this);
    connect(progressTimer, &QTimer::timeout, this, &QtPlayerExample::onProgressTimer);
    progressTimer->start(100);  // 10 Hz
}

void QtPlayerExample::initPlayer()
{
    // 创建播放器
    player_ = player_core_create();
    if (!player_) {
        QMessageBox::critical(this, "错误", "创建播放器失败！");
        return;
    }
    
    // 设置渲染窗口（Qt QWidget 的窗口句柄）
#ifdef _WIN32
    HWND hwnd = (HWND)videoWidget_->winId();
    player_core_set_render_window(player_, (void*)hwnd);
#elif defined(__APPLE__)
    void* nsview = (void*)videoWidget_->winId();
    player_core_set_render_window(player_, nsview);
#else
    // Linux
    WId wid = videoWidget_->winId();
    player_core_set_render_window(player_, (void*)wid);
#endif
    
    // 设置为自动渲染模式
    player_core_set_render_mode(player_, RENDER_MODE_AUTO);
    
    // 设置回调
    player_core_set_state_changed_callback(player_, 
        [](PlayerStateC state, void* user_data) {
            // 可以在这里处理状态变化
        }, this);
    
    player_core_set_error_callback(player_,
        [](int error_code, const char* error_msg, void* user_data) {
            QtPlayerExample* self = (QtPlayerExample*)user_data;
            QString msg = QString("播放器错误 [%1]: %2")
                .arg(error_code).arg(error_msg);
            QMessageBox::critical(self, "错误", msg);
        }, this);
    
    player_core_set_playback_completed_callback(player_,
        [](void* user_data) {
            // 播放完成
        }, this);
}

void QtPlayerExample::cleanupPlayer()
{
    if (player_) {
        player_core_stop(player_);
        player_core_destroy(player_);
        player_ = nullptr;
    }
}

void QtPlayerExample::onOpenClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "打开视频文件",
        "",
        "视频文件 (*.mp4 *.avi *.mkv *.flv *.mov);;所有文件 (*.*)"
    );
    
    if (!fileName.isEmpty()) {
        // 转换为 UTF-8
        std::string path = fileName.toUtf8().constData();
        
        // 打开视频
        if (player_core_open(player_, path.c_str()) != 0) {
            QMessageBox::critical(this, "错误", "打开视频失败！");
        }
    }
}

void QtPlayerExample::onPlayClicked()
{
    if (player_) {
        player_core_play(player_);
    }
}

void QtPlayerExample::onPauseClicked()
{
    if (player_) {
        player_core_pause(player_);
    }
}

void QtPlayerExample::onStopClicked()
{
    if (player_) {
        player_core_stop(player_);
        progressSlider_->setValue(0);
        timeLabel_->setText("0:00 / 0:00");
    }
}

void QtPlayerExample::onRefreshTimer()
{
    // 刷新视频帧
    if (player_) {
        player_core_refresh_video(player_);
    }
}

void QtPlayerExample::onProgressTimer()
{
    // 更新进度条和时间显示
    if (player_) {
        double position = player_core_get_position(player_);
        double duration = player_core_get_duration(player_);
        
        if (duration > 0) {
            int progress = (int)((position / duration) * 1000);
            progressSlider_->setValue(progress);
            
            // 格式化时间
            QString timeText = QString("%1:%2 / %3:%4")
                .arg((int)position / 60).arg((int)position % 60, 2, 10, QChar('0'))
                .arg((int)duration / 60).arg((int)duration % 60, 2, 10, QChar('0'));
            timeLabel_->setText(timeText);
        }
    }
}

// main.cpp
#include <QApplication>
#include "qt_player_example.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // 初始化 SDK
    hxcplayer_init();
    
    QtPlayerExample window;
    window.show();
    
    int result = app.exec();
    
    // 清理 SDK
    hxcplayer_cleanup();
    
    return result;
}
