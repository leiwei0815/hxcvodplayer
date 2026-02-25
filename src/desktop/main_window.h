/**
 * @file main_window.h
 * @brief Qt 主窗口
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <memory>
#include "yx_player_core.h"
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
    void onAspectRatioModeChanged(yxplayer::AspectRatioMode mode);
    
    // ⚠️ 重命名避免 Qt 自动连接（去掉 on_ 前缀）
    void handleSeekSliderPressed();
    void handleSeekSliderReleased();
    void handleSeekSliderMoved(int position);
    void handleVolumeChanged(int value);
    
    void updateUI();
    void onStateChanged(yxplayer::PlayerState state);
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

private slots:
    void handleSpeedChanged(int index);  // 倍速变化处理

private:
    Ui::MainWindow *ui;
    std::unique_ptr<yxplayer::PlayerCore> player_;
    VideoWidget* video_widget_;
    QTimer* update_timer_;
    QTimer* refresh_timer_;  // 视频刷新定时器
    bool is_seeking_;
    double last_video_pts_;  // 上一帧的 PTS
};

#endif // MAINWINDOW_H
