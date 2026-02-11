# 快速入门指南

## 5 分钟快速开始

### macOS 用户

#### 1. 安装依赖

```bash
# 安装 Homebrew (如果还没有)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装依赖
brew install cmake ffmpeg sdl2 qt@5

# 配置环境变量
export Qt5_DIR=/usr/local/opt/qt@5/lib/cmake/Qt5
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

#### 2. 克隆并构建

```bash
cd /Users/debug/project/YXVodPlayer

# 给构建脚本添加执行权限
chmod +x build.sh
chmod +x scripts/setup_dev.sh

# 构建
./build.sh desktop release

# 运行
./build/desktop_release/bin/YXVodPlayer
```

### Linux 用户 (Ubuntu/Debian)

#### 1. 安装依赖

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake pkg-config git \
    libavcodec-dev libavformat-dev libavutil-dev \
    libswscale-dev libswresample-dev \
    libsdl2-dev qtbase5-dev
```

#### 2. 构建运行

```bash
cd YXVodPlayer
chmod +x build.sh
./build.sh desktop release
./build/desktop_release/bin/YXVodPlayer
```

### Windows 用户

#### 1. 使用 vcpkg 安装依赖

```powershell
# 安装 vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# 安装依赖
.\vcpkg install ffmpeg:x64-windows sdl2:x64-windows qt5-base:x64-windows
.\vcpkg integrate install
```

#### 2. 使用 CMake GUI 构建

1. 打开 CMake GUI
2. 设置源代码路径和构建路径
3. 点击 "Configure"
4. 选择 Visual Studio 生成器
5. 设置 CMAKE_TOOLCHAIN_FILE 为 vcpkg.cmake
6. 点击 "Generate"
7. 点击 "Open Project"
8. 在 Visual Studio 中构建

## Android 开发

### 准备

1. 安装 Android Studio
2. 安装 NDK (通过 SDK Manager)
3. 编译 FFmpeg for Android (参见 `docs/BUILD.md`)

### 构建

```bash
cd android
./gradlew assembleDebug

# 安装到设备
./gradlew installDebug
```

## iOS 开发

### 准备 (仅 macOS)

1. 安装 Xcode from App Store
2. 编译 FFmpeg for iOS (参见 `docs/BUILD.md`)

### 构建

```bash
cd ios
xcodebuild -project YXVodPlayer.xcodeproj \
           -scheme YXVodPlayer \
           -sdk iphonesimulator \
           build
```

## 测试播放器

### 准备测试视频

```bash
# 下载示例视频
curl -O https://sample-videos.com/video123/mp4/720/big_buck_bunny_720p_1mb.mp4

# 或使用 FFmpeg 生成测试视频
ffmpeg -f lavfi -i testsrc=duration=10:size=1280x720:rate=30 \
       -f lavfi -i sine=frequency=1000:duration=10 \
       -pix_fmt yuv420p test.mp4
```

### 运行播放器

#### Desktop

```bash
# 直接运行
./build/desktop_release/bin/YXVodPlayer

# 或通过命令行打开文件
./build/desktop_release/bin/YXVodPlayer /path/to/video.mp4
```

#### Android

在 Android Studio 中运行应用，或通过命令行：

```bash
adb install app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.yx.vodplayer/.PlayerActivity
```

## 基本用法

### 桌面版

1. 点击 "打开" 按钮选择视频文件
2. 或直接拖拽视频文件到窗口
3. 使用控制条进行播放控制：
   - 播放/暂停
   - 进度条拖动
   - 音量调节
4. 快捷键：
   - 空格: 播放/暂停
   - Ctrl+O: 打开文件
   - Ctrl+Q: 退出

### 移动版

1. 启动应用
2. 选择视频文件
3. 触摸屏幕显示/隐藏控制条
4. 滑动调节进度
5. 音量键调节音量

## API 使用示例

### C++ 核心 API

```cpp
#include "player_core.h"

using namespace yxplayer;

int main() {
    // 创建播放器
    PlayerCore player;
    
    // 设置配置
    PlayerConfig config;
    config.sync_mode = SyncMode::AudioMaster;
    player.set_config(config);
    
    // 设置回调
    player.set_state_changed_callback([](PlayerState state) {
        std::cout << "状态变化: " << (int)state << std::endl;
    });
    
    player.set_error_callback([](const std::string& error) {
        std::cerr << "错误: " << error << std::endl;
    });
    
    // 打开文件
    if (player.open("test.mp4") == 0) {
        // 播放
        player.play();
        
        // 等待播放完成
        while (player.get_state() == PlayerState::Playing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    // 关闭
    player.close();
    
    return 0;
}
```

### Qt 集成

```cpp
#include "player_core.h"
#include <QApplication>
#include <QMainWindow>

class PlayerWindow : public QMainWindow {
public:
    PlayerWindow() {
        player_ = std::make_unique<yxplayer::PlayerCore>();
        
        player_->set_state_changed_callback([this](yxplayer::PlayerState state) {
            // 更新 UI
        });
    }
    
private:
    std::unique_ptr<yxplayer::PlayerCore> player_;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    PlayerWindow window;
    window.show();
    return app.exec();
}
```

### Android JNI

```java
public class MainActivity extends Activity {
    private NativePlayer player;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        player = new NativePlayer();
        player.nativeCreate();
        
        player.setStateCallback(state -> {
            // 更新 UI
        });
        
        player.nativeOpen(player.nativeHandle, "/path/to/video.mp4");
        player.nativePlay(player.nativeHandle);
    }
}
```

### iOS Objective-C

```objc
@interface ViewController : UIViewController
@property (nonatomic, strong) YXPlayerBridge *player;
@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.player = [[YXPlayerBridge alloc] init];
    self.player.delegate = self;
    
    [self.player openFile:@"/path/to/video.mp4"];
    [self.player play];
}

- (void)playerDidChangeState:(YXPlayerState)state {
    // 更新 UI
}

@end
```

## 常见问题

### Q: 找不到 FFmpeg 库

**A:** 确保 FFmpeg 已正确安装并配置了 pkg-config：

```bash
# macOS
brew install ffmpeg
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

# Linux
sudo apt install libavcodec-dev libavformat-dev libavutil-dev
```

### Q: Qt5 找不到

**A:** 设置 Qt5_DIR 环境变量：

```bash
# macOS
export Qt5_DIR=/usr/local/opt/qt@5/lib/cmake/Qt5

# Linux
export Qt5_DIR=/usr/lib/x86_64-linux-gnu/cmake/Qt5
```

### Q: SDL2 链接错误

**A:** 确保安装了 SDL2 开发库：

```bash
# macOS
brew install sdl2

# Linux
sudo apt install libsdl2-dev
```

### Q: 视频播放卡顿

**A:** 尝试以下方法：
1. 检查 CPU 使用率
2. 启用硬件加速
3. 调整缓冲区大小
4. 降低视频分辨率

### Q: 音视频不同步

**A:** 
1. 检查同步模式设置
2. 调整音频缓冲大小
3. 确保音频设备正常工作

## 下一步

- 阅读 [架构文档](ARCHITECTURE.md) 了解设计细节
- 查看 [构建指南](BUILD.md) 了解高级构建选项
- 参考 [Android 指南](ANDROID.md) 开发 Android 版本
- 参考 [iOS 指南](IOS.md) 开发 iOS 版本

## 获取帮助

- 查看文档: `docs/` 目录
- 提交 Issue
- 参考 FFmpeg 文档: https://ffmpeg.org/documentation.html
- 参考 SDL2 文档: https://wiki.libsdl.org/

## 贡献

欢迎提交 Pull Request！请确保：

1. 代码符合项目风格
2. 添加必要的注释
3. 通过所有测试
4. 更新相关文档
