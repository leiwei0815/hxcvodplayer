/**
 * @file main.cpp
 * @brief 应用程序入口
 */

#include "main_window.h"
#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
}

int main(int argc, char *argv[]) {
    // ========== 调试断点：程序入口 ==========
    std::cout << "=========================================" << std::endl;
    std::cout << "YXVodPlayer 启动中..." << std::endl;
    std::cout << "参数数量: " << argc << std::endl;
    for (int i = 0; i < argc; i++) {
        std::cout << "参数[" << i << "]: " << argv[i] << std::endl;
    }
    std::cout << "=========================================" << std::endl;
    
    // 初始化 FFmpeg
    av_log_set_level(AV_LOG_WARNING);
    std::cout << "FFmpeg 初始化完成" << std::endl;
    
    // 创建 Qt 应用
    QApplication app(argc, argv);
    app.setApplicationName("YXVodPlayer");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("YX");
    
    std::cout << "Qt 应用创建完成" << std::endl;
    
    // ========== 调试断点：创建主窗口 ==========
    std::cout << "正在创建主窗口..." << std::endl;
    MainWindow window;
    std::cout << "主窗口创建完成" << std::endl;
    
    // ⚠️ 命令行参数支持：
    // 用法 1: ./YXVodPlayer <文件路径>
    // 用法 2: ./YXVodPlayer <文件路径> <开始时间(秒)>
    if (argc > 1) {
        QString filename = QString::fromUtf8(argv[1]);
        double start_time = 0.0;
        
        // 如果有第二个参数，作为开始播放时间
        if (argc > 2) {
            bool ok;
            start_time = QString::fromUtf8(argv[2]).toDouble(&ok);
            if (!ok || start_time < 0) {
                std::cerr << "警告: 无效的开始时间参数，使用默认值 0" << std::endl;
                start_time = 0.0;
            }
        }
        
        std::cout << "=========================================" << std::endl;
        std::cout << "检测到命令行参数:" << std::endl;
        std::cout << "  文件: " << filename.toStdString() << std::endl;
        if (start_time > 0) {
            std::cout << "  开始时间: " << start_time << " 秒" << std::endl;
        }
        std::cout << "=========================================" << std::endl;
        
        // ⚠️ 延迟打开文件，确保窗口已显示
        QTimer::singleShot(100, [&window, filename, start_time]() {
            std::cout << "定时器触发，设置开始时间并打开文件" << std::endl;
            if (start_time > 0) {
                window.setStartTime(start_time);
            }
            // window.openFile(filename);  // 需要添加公共方法
        });
    }
    
    // ========== 调试断点：显示窗口 ==========
    std::cout << "显示主窗口..." << std::endl;
    window.show();
    std::cout << "窗口已显示，进入事件循环" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    // 进入事件循环
    int result = app.exec();
    
    std::cout << "=========================================" << std::endl;
    std::cout << "应用程序退出，返回码: " << result << std::endl;
    std::cout << "=========================================" << std::endl;
    
    return result;
}
