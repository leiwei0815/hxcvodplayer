/**
 * @file main_window.h
 * @brief Qt 主窗口
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <memory>

#ifdef _WIN32
// Windows: 使用 SDK C API
#include "../win-sdk/hxcplayer_sdk_c_api.h"

// 辅助宏：Windows SDK C API 调用
#define PLAYER_CALL_VOID(method, ...) \
    if (player_sdk_) { hxc_player_##method(player_sdk_, ##__VA_ARGS__); }

#define PLAYER_CALL(method, default_val, ...) \
    (player_sdk_ ? hxc_player_##method(player_sdk_, ##__VA_ARGS__) : default_val)

#define PLAYER_EXISTS() (player_sdk_ != nullptr)

#else
// 其他平台: 使用 C++ API
#include "hxc_player_core.h"

// 辅助宏：C++ API 调用
#define PLAYER_CALL_VOID(method, ...) \
    if (player_) { player_->method(__VA_ARGS__); }

#define PLAYER_CALL(method, default_val, ...) \
    (player_ ? player_->method(__VA_ARGS__) : default_val)

#define PLAYER_EXISTS() (player_ != nullptr)

#endif

#include "video_widget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
private slots:
    void on_openButton_clicked();
    void on_playPauseButton_clicked();
    void on_stopButton_clicked();
    
    // ⚠️ 显示模式切换
    void onAspectRatioModeChanged(hxcplayer::AspectRatioMode mode);
    
    // ⚠️ 重命名避免 Qt 自动连接（去掉 on_ 前缀）
    void handleSeekSliderPressed();
    void handleSeekSliderReleased();
    void handleSeekSliderMoved(int position);
    void handleVolumeChanged(int value);
    
    void updateUI();  // 已弃用，保留以兼容旧代码
    void updateProgress(double position);  // 播放进度更新（由回调触发）
    void onPlaybackCompleted();  // 播放完成回调
    void onStateChanged(hxcplayer::PlayerState state);
    void onError(const QString& error);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

public slots:
    void openFile(const QString& filename);  // 改为 public slot，方便外部调用
    
    // ⚠️ 设置开始播放时间（在打开文件前调用）
    void setStartTime(double seconds);

private:
    void setupUI();
    QString formatTime(double seconds);
    void refreshVideo();  // 视频刷新
    void openLocalFile();  // 打开本地文件
    void openNetworkURL();  // 打开网络地址

private slots:
    void handleSpeedChanged(int index);  // 倍速变化处理
    void on_aspectRatioButton_clicked();  // 显示模式按钮点击

private:
    Ui::MainWindow *ui;
    
#ifdef _WIN32
    HXCPlayerSDK* player_sdk_;  // Windows: SDK C API 句柄
#else
    std::unique_ptr<hxcplayer::PlayerCore> player_;  // 其他平台: C++ API
#endif
    
    VideoWidget* video_widget_;
    QTimer* refresh_timer_;  // 视频刷新定时器（Windows D3D11 模式不使用）
    bool is_seeking_;
    double last_video_pts_;  // 上一帧的 PTS
};

#endif // MAINWINDOW_H
