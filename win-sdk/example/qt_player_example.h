#ifndef QT_PLAYER_EXAMPLE_H
#define QT_PLAYER_EXAMPLE_H

#include <QWidget>

// 前向声明
class QSlider;
class QLabel;
class QTimer;
struct PlayerCoreHandle;

class QtPlayerExample : public QWidget
{
    Q_OBJECT

public:
    QtPlayerExample(QWidget *parent = nullptr);
    ~QtPlayerExample();

private slots:
    void onOpenClicked();
    void onPlayClicked();
    void onPauseClicked();
    void onStopClicked();
    void onRefreshTimer();
    void onProgressTimer();

private:
    void setupUI();
    void initPlayer();
    void cleanupPlayer();

private:
    PlayerCoreHandle* player_;
    QWidget* videoWidget_;
    QSlider* progressSlider_;
    QLabel* timeLabel_;
    QTimer* refreshTimer_;
};

#endif // QT_PLAYER_EXAMPLE_H
