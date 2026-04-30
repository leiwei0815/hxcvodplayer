#ifndef PLAYER_WINDOW_H
#define PLAYER_WINDOW_H

#include <QMainWindow>
#include <QDialog>
#include <QLineEdit>
#include <QRadioButton>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
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

// ========== SecureHLS 测试对话框 ==========
// 用于配置并测试 HLS AES-128 自定义解密链路
class SecureHLSDialog : public QDialog {
    Q_OBJECT

public:
    explicit SecureHLSDialog(QWidget *parent = nullptr);

    // 获取用户填写的参数
    QString getM3u8Url() const;
    QString getSecureHeaders() const;  // 多行，每行 "Key: Value"
    bool    useLocalTestServer() const;

private slots:
    void onUseLocalToggled(bool checked);
    void onVerifyClicked();

private:
    QLineEdit*  m3u8UrlEdit_;
    QTextEdit*  headersEdit_;
    QCheckBox*  localTestCheck_;
    QLabel*     hintLabel_;
    QPushButton* verifyBtn_;

    void fillLocalDefaults();
    void fillRealServerDefaults();
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
    void on_secureHlsButton_clicked();   // SecureHLS 测试入口

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
