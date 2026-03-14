#ifndef PLAYER_WINDOW_H
#define PLAYER_WINDOW_H

#include <QMainWindow>
#include <QDialog>
#include <QLineEdit>
#include <QRadioButton>
#include "hxcplayer_sdk.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PlayerWindow; }
QT_END_NAMESPACE

class OpenMediaDialog : public QDialog {
    Q_OBJECT

public:
    explicit OpenMediaDialog(QWidget *parent = nullptr);
    QString getMediaPath() const;
    bool isLocalFile() const;

private slots:
    void onTypeChanged();
    void onBrowseClicked();

private:
    QRadioButton* localFileRadio_;
    QRadioButton* networkUrlRadio_;
    QLineEdit* pathEdit_;
    QPushButton* browseButton_;
};

class PlayerWindow : public QMainWindow {
    Q_OBJECT

public:
    PlayerWindow(QWidget *parent = nullptr);
    ~PlayerWindow();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;  // 事件过滤器（用于绘制）

private slots:
    void on_openButton_clicked();
    void on_playButton_clicked();
    void on_pauseButton_clicked();
    void on_stopButton_clicked();
    void on_volumeSlider_valueChanged(int value);
    void on_seekSlider_sliderPressed();
    void on_seekSlider_sliderReleased();
    void on_seekSlider_sliderMoved(int position);
    
    // 回调触发的 UI 更新槽
    void onProgressChanged();
    void onStateChanged();
    void onPlaybackFinished();

private:
    Ui::PlayerWindow *ui;
    PlayerCoreHandle* player_;
    bool is_seeking_;
    double duration_;
    QTimer* refresh_timer_;   // 刷新定时器
    QImage current_frame_;    // 当前显示的帧
    std::vector<unsigned char> rgb_buffer_;  // RGB 缓冲区
    
    QString formatTime(double seconds);
    void updatePlaybackInfo(double position);
};

#endif // PLAYER_WINDOW_H
