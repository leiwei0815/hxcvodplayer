# Qt 示例项目
QT += core gui widgets

TARGET = qt_player_example
TEMPLATE = app

# C++17
CONFIG += c++17

# HXCPlayer SDK 路径
HXCPLAYER_SDK = $$PWD/..

# 包含目录
INCLUDEPATH += $$HXCPLAYER_SDK/include

# 库目录
LIBS += -L$$HXCPLAYER_SDK/lib -lhxcplayer

# 源文件
SOURCES += \
    qt_player_example.cpp

HEADERS += \
    qt_player_example.h

# Windows 特定
win32 {
    # 复制 DLL 到输出目录
    CONFIG(debug, debug|release) {
        DESTDIR = $$OUT_PWD/debug
    } else {
        DESTDIR = $$OUT_PWD/release
    }
    
    # 复制 DLL
    QMAKE_POST_LINK += $$quote(xcopy /Y /Q \"$$HXCPLAYER_SDK/bin/*.dll\" \"$$DESTDIR\\\" $$escape_expand(\\n\\t))
}

# macOS 特定
macx {
    # Framework 路径
    LIBS += -F$$HXCPLAYER_SDK/Frameworks
    LIBS += -framework HXCPlayer
}

# 安装规则
target.path = $$[QT_INSTALL_EXAMPLES]/hxcplayer
INSTALLS += target
