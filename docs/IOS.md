# iOS 平台实现指南

## 概述

iOS 版本使用 Objective-C/Swift 实现 UI 和平台特定功能,通过 Objective-C++ 桥接调用 C++ 核心播放器。

## 架构

```
┌─────────────────────────────────────┐
│   iOS UI Layer (Swift/ObjC)         │
│  - PlayerViewController             │
│  - PlayerView (Metal/OpenGL)        │
│  - PlayerControls                   │
└─────────────────────────────────────┘
              ↓ Bridge
┌─────────────────────────────────────┐
│   Bridge Layer (Objective-C++)      │
│  - PlayerBridge.mm                  │
│  - IOSRenderer.mm                   │
│  - IOSAudio.mm                      │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│   Core Player (C++)                 │
│  - PlayerCore                       │
│  - Decoder / Queue / Clock          │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│   FFmpeg / VideoToolbox             │
└─────────────────────────────────────┘
```

## 项目结构

```
ios/
├── YXVodPlayer/
│   ├── App/
│   │   ├── AppDelegate.h/m
│   │   ├── SceneDelegate.h/m
│   │   └── Info.plist
│   ├── Player/
│   │   ├── PlayerViewController.h/m
│   │   ├── PlayerView.h/m
│   │   ├── PlayerControls.h/m
│   │   └── MetalRenderer.h/m
│   ├── Bridge/
│   │   ├── PlayerBridge.h
│   │   ├── PlayerBridge.mm
│   │   ├── IOSRenderer.h/mm
│   │   └── IOSAudio.h/mm
│   ├── Resources/
│   │   ├── Main.storyboard
│   │   ├── Assets.xcassets
│   │   └── LaunchScreen.storyboard
│   └── Supporting Files/
├── Frameworks/
│   └── FFmpeg/              # FFmpeg 预编译库
│       ├── libavcodec.a
│       ├── libavformat.a
│       ├── libavutil.a
│       ├── libswscale.a
│       └── libswresample.a
├── YXVodPlayer.xcodeproj
└── Podfile                  # CocoaPods 依赖
```

## 核心组件

### 1. PlayerBridge (桥接层)

**PlayerBridge.h** (Objective-C)

```objc
#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, YXPlayerState) {
    YXPlayerStateIdle,
    YXPlayerStateOpening,
    YXPlayerStatePlaying,
    YXPlayerStatePaused,
    YXPlayerStateStopped,
    YXPlayerStateError
};

@protocol YXPlayerDelegate <NSObject>
@optional
- (void)playerDidChangeState:(YXPlayerState)state;
- (void)playerDidUpdatePosition:(NSTimeInterval)position;
- (void)playerDidEncounterError:(NSString *)error;
@end

@interface YXPlayerBridge : NSObject

@property (nonatomic, weak) id<YXPlayerDelegate> delegate;
@property (nonatomic, readonly) YXPlayerState state;
@property (nonatomic, readonly) NSTimeInterval duration;
@property (nonatomic, readonly) NSTimeInterval position;

- (instancetype)init;
- (BOOL)openFile:(NSString *)path;
- (void)close;
- (void)play;
- (void)pause;
- (void)stop;
- (void)seekToPosition:(NSTimeInterval)position;
- (void)setVolume:(NSInteger)volume; // 0-100
- (void)setView:(UIView *)view;

@end
```

**PlayerBridge.mm** (Objective-C++)

```objcpp
#import "PlayerBridge.h"
#include "player_core.h"
#include "ios_renderer.h"

using namespace yxplayer;

@interface YXPlayerBridge() {
    PlayerCore* _player;
    IOSRenderer* _renderer;
}
@end

@implementation YXPlayerBridge

- (instancetype)init {
    self = [super init];
    if (self) {
        _player = new PlayerCore();
        _renderer = new IOSRenderer();
        
        // 设置回调
        __weak typeof(self) weakSelf = self;
        
        _player->set_state_changed_callback([weakSelf](PlayerState state) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [weakSelf.delegate playerDidChangeState:(YXPlayerState)state];
            });
        });
        
        _player->set_error_callback([weakSelf](const std::string& error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                NSString* errorStr = [NSString stringWithUTF8String:error.c_str()];
                [weakSelf.delegate playerDidEncounterError:errorStr];
            });
        });
        
        _player->set_position_changed_callback([weakSelf](double position) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [weakSelf.delegate playerDidUpdatePosition:position];
            });
        });
    }
    return self;
}

- (void)dealloc {
    if (_player) {
        _player->close();
        delete _player;
    }
    if (_renderer) {
        delete _renderer;
    }
}

- (BOOL)openFile:(NSString *)path {
    if (!_player) return NO;
    std::string pathStr = [path UTF8String];
    return _player->open(pathStr) == 0;
}

- (void)close {
    if (_player) {
        _player->close();
    }
}

- (void)play {
    if (_player) {
        _player->play();
    }
}

- (void)pause {
    if (_player) {
        _player->pause();
    }
}

- (void)stop {
    if (_player) {
        _player->stop();
    }
}

- (void)seekToPosition:(NSTimeInterval)position {
    if (_player) {
        _player->seek(position);
    }
}

- (void)setVolume:(NSInteger)volume {
    if (_player) {
        _player->set_volume((int)volume);
    }
}

- (void)setView:(UIView *)view {
    if (_renderer) {
        _renderer->setView(view);
    }
}

- (YXPlayerState)state {
    if (_player) {
        return (YXPlayerState)_player->get_state();
    }
    return YXPlayerStateIdle;
}

- (NSTimeInterval)duration {
    if (_player) {
        return _player->get_duration();
    }
    return 0;
}

- (NSTimeInterval)position {
    if (_player) {
        return _player->get_position();
    }
    return 0;
}

@end
```

### 2. PlayerViewController

**PlayerViewController.h**

```objc
#import <UIKit/UIKit.h>
#import "PlayerBridge.h"

@interface PlayerViewController : UIViewController <YXPlayerDelegate>

@property (nonatomic, strong) NSString *videoPath;

@end
```

**PlayerViewController.m**

```objc
#import "PlayerViewController.h"
#import "PlayerView.h"
#import "PlayerControls.h"

@interface PlayerViewController ()

@property (nonatomic, strong) YXPlayerBridge *player;
@property (nonatomic, strong) PlayerView *playerView;
@property (nonatomic, strong) PlayerControls *controls;
@property (nonatomic, strong) NSTimer *updateTimer;

@end

@implementation PlayerViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.view.backgroundColor = [UIColor blackColor];
    
    // 创建播放器视图
    self.playerView = [[PlayerView alloc] initWithFrame:self.view.bounds];
    self.playerView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.playerView];
    
    // 创建控制条
    self.controls = [[PlayerControls alloc] initWithFrame:CGRectMake(0, self.view.bounds.size.height - 80, self.view.bounds.size.width, 80)];
    self.controls.autoresizingMask = UIViewAutoresizingFlexibleTopMargin | UIViewAutoresizingFlexibleWidth;
    [self.view addSubview:self.controls];
    
    // 创建播放器
    self.player = [[YXPlayerBridge alloc] init];
    self.player.delegate = self;
    [self.player setView:self.playerView];
    
    // 设置控制条回调
    __weak typeof(self) weakSelf = self;
    self.controls.onPlayPause = ^{
        [weakSelf togglePlayPause];
    };
    
    self.controls.onSeek = ^(NSTimeInterval position) {
        [weakSelf.player seekToPosition:position];
    };
    
    self.controls.onVolumeChanged = ^(NSInteger volume) {
        [weakSelf.player setVolume:volume];
    };
    
    // 更新定时器
    self.updateTimer = [NSTimer scheduledTimerWithTimeInterval:0.1
                                                       target:self
                                                     selector:@selector(updateUI)
                                                     userInfo:nil
                                                      repeats:YES];
    
    // 打开视频
    if (self.videoPath) {
        [self openVideo:self.videoPath];
    }
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [self.updateTimer invalidate];
    self.updateTimer = nil;
    [self.player close];
}

- (void)openVideo:(NSString *)path {
    if ([self.player openFile:path]) {
        [self.player play];
    }
}

- (void)togglePlayPause {
    if (self.player.state == YXPlayerStatePlaying) {
        [self.player pause];
    } else {
        [self.player play];
    }
}

- (void)updateUI {
    NSTimeInterval position = self.player.position;
    NSTimeInterval duration = self.player.duration;
    
    [self.controls updateProgress:position duration:duration];
}

#pragma mark - YXPlayerDelegate

- (void)playerDidChangeState:(YXPlayerState)state {
    [self.controls updateState:state];
}

- (void)playerDidUpdatePosition:(NSTimeInterval)position {
    // 位置更新
}

- (void)playerDidEncounterError:(NSString *)error {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"错误"
                                                                  message:error
                                                           preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"确定" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

@end
```

### 3. PlayerView (Metal 渲染)

**PlayerView.h**

```objc
#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>

@interface PlayerView : MTKView

@end
```

**PlayerView.m**

```objc
#import "PlayerView.h"

@interface PlayerView () <MTKViewDelegate>

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id<MTLTexture> videoTexture;
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;

@end

@implementation PlayerView

- (instancetype)initWithFrame:(CGRect)frame {
    self.device = MTLCreateSystemDefaultDevice();
    self = [super initWithFrame:frame device:self.device];
    if (self) {
        [self setupMetal];
    }
    return self;
}

- (void)setupMetal {
    self.delegate = self;
    self.clearColor = MTLClearColorMake(0, 0, 0, 1);
    
    // 创建命令队列
    self.commandQueue = [self.device newCommandQueue];
    
    // 加载着色器
    id<MTLLibrary> library = [self.device newDefaultLibrary];
    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"vertexShader"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"fragmentShader"];
    
    // 创建渲染管线
    MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;
    pipelineDescriptor.colorAttachments[0].pixelFormat = self.colorPixelFormat;
    
    NSError *error = nil;
    self.pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (error) {
        NSLog(@"创建渲染管线失败: %@", error);
    }
}

- (void)updateVideoTexture:(CVPixelBufferRef)pixelBuffer {
    // 创建纹理从 CVPixelBuffer
    // TODO: 实现纹理更新
}

#pragma mark - MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    // 处理尺寸变化
}

- (void)drawInMTKView:(MTKView *)view {
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    
    MTLRenderPassDescriptor *renderPassDescriptor = self.currentRenderPassDescriptor;
    if (renderPassDescriptor) {
        id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        
        [renderEncoder setRenderPipelineState:self.pipelineState];
        
        // 绘制视频纹理
        if (self.videoTexture) {
            // TODO: 绘制纹理到屏幕
        }
        
        [renderEncoder endEncoding];
    }
    
    [commandBuffer presentDrawable:self.currentDrawable];
    [commandBuffer commit];
}

@end
```

### 4. IOSRenderer (C++ 渲染器)

**IOSRenderer.h**

```objcpp
#ifndef IOS_RENDERER_H
#define IOS_RENDERER_H

#include "platform_interface.h"
#import <UIKit/UIKit.h>

namespace yxplayer {

class IOSRenderer : public IVideoRenderer {
public:
    IOSRenderer();
    ~IOSRenderer() override;
    
    bool init(int width, int height, PixelFormat format) override;
    bool render_frame(const VideoFrame* frame) override;
    void resize(int width, int height) override;
    void clear() override;
    void destroy() override;
    
    void setView(UIView* view);
    
private:
    UIView* view_;
    int width_;
    int height_;
    PixelFormat format_;
};

} // namespace yxplayer

#endif // IOS_RENDERER_H
```

**IOSRenderer.mm**

```objcpp
#include "IOSRenderer.h"
#include "PlayerView.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace yxplayer {

IOSRenderer::IOSRenderer()
    : view_(nil)
    , width_(0)
    , height_(0)
    , format_(PixelFormat::YUV420P) {
}

IOSRenderer::~IOSRenderer() {
    destroy();
}

bool IOSRenderer::init(int width, int height, PixelFormat format) {
    width_ = width;
    height_ = height;
    format_ = format;
    return true;
}

void IOSRenderer::setView(UIView* view) {
    view_ = view;
}

bool IOSRenderer::render_frame(const VideoFrame* frame) {
    if (!view_ || !frame || !frame->frame) {
        return false;
    }
    
    // 渲染到 Metal 视图
    if ([view_ isKindOfClass:[PlayerView class]]) {
        PlayerView* playerView = (PlayerView*)view_;
        // TODO: 更新纹理并渲染
    }
    
    return true;
}

void IOSRenderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
}

void IOSRenderer::clear() {
    // 清空渲染
}

void IOSRenderer::destroy() {
    view_ = nil;
}

} // namespace yxplayer
```

## 硬件加速（VideoToolbox）

### 使用 VideoToolbox 解码

```objcpp
#include <VideoToolbox/VideoToolbox.h>

class VideoToolboxDecoder {
public:
    bool init(AVCodecParameters* codecpar);
    bool decode(AVPacket* packet, CVPixelBufferRef* pixelBuffer);
    void flush();
    
private:
    VTDecompressionSessionRef session_;
    CMVideoFormatDescriptionRef formatDesc_;
};

bool VideoToolboxDecoder::init(AVCodecParameters* codecpar) {
    // 创建格式描述
    const uint8_t* extradata = codecpar->extradata;
    size_t extradata_size = codecpar->extradata_size;
    
    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
        kCFAllocatorDefault,
        2,  // parameter set count
        &extradata,  // parameter sets
        nullptr,  // parameter set sizes
        4,  // NAL unit header length
        &formatDesc_
    );
    
    if (status != noErr) {
        return false;
    }
    
    // 创建解压缩会话
    VTDecompressionOutputCallbackRecord callback = {
        .decompressionOutputCallback = decompressionOutputCallback,
        .decompressionOutputRefCon = this
    };
    
    CFDictionaryRef attrs = nullptr;  // 解码属性
    
    status = VTDecompressionSessionCreate(
        kCFAllocatorDefault,
        formatDesc_,
        nullptr,  // decoder specification
        attrs,
        &callback,
        &session_
    );
    
    return status == noErr;
}

bool VideoToolboxDecoder::decode(AVPacket* packet, CVPixelBufferRef* pixelBuffer) {
    // 创建 CMSampleBuffer
    CMBlockBufferRef blockBuffer = nullptr;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        packet->data,
        packet->size,
        kCFAllocatorNull,
        nullptr,
        0,
        packet->size,
        0,
        &blockBuffer
    );
    
    if (status != noErr) {
        return false;
    }
    
    CMSampleBufferRef sampleBuffer = nullptr;
    status = CMSampleBufferCreate(
        kCFAllocatorDefault,
        blockBuffer,
        true,
        nullptr,
        nullptr,
        formatDesc_,
        1,
        0,
        nullptr,
        0,
        nullptr,
        &sampleBuffer
    );
    
    CFRelease(blockBuffer);
    
    if (status != noErr) {
        return false;
    }
    
    // 解码
    VTDecodeFrameFlags flags = 0;
    VTDecodeInfoFlags infoFlags = 0;
    
    status = VTDecompressionSessionDecodeFrame(
        session_,
        sampleBuffer,
        flags,
        pixelBuffer,
        &infoFlags
    );
    
    CFRelease(sampleBuffer);
    
    return status == noErr;
}
```

## Info.plist 配置

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>$(DEVELOPMENT_LANGUAGE)</string>
    <key>CFBundleExecutable</key>
    <string>$(EXECUTABLE_NAME)</string>
    <key>CFBundleIdentifier</key>
    <string>$(PRODUCT_BUNDLE_IDENTIFIER)</string>
    <key>CFBundleName</key>
    <string>$(PRODUCT_NAME)</string>
    <key>CFBundlePackageType</key>
    <string>$(PRODUCT_BUNDLE_PACKAGE_TYPE)</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    
    <!-- 支持的方向 -->
    <key>UISupportedInterfaceOrientations</key>
    <array>
        <string>UIInterfaceOrientationLandscapeLeft</string>
        <string>UIInterfaceOrientationLandscapeRight</string>
    </array>
    
    <!-- 文件访问权限 -->
    <key>NSPhotoLibraryUsageDescription</key>
    <string>需要访问相册以选择视频</string>
    
    <key>UIFileSharingEnabled</key>
    <true/>
    <key>LSSupportsOpeningDocumentsInPlace</key>
    <true/>
    
    <!-- 后台音频播放 -->
    <key>UIBackgroundModes</key>
    <array>
        <string>audio</string>
    </array>
</dict>
</plist>
```

## 构建配置

### Xcode Project Settings

1. **Build Settings**
   - Header Search Paths: FFmpeg include 路径
   - Library Search Paths: FFmpeg lib 路径
   - Other Linker Flags: `-lc++`

2. **Frameworks**
   - UIKit.framework
   - MetalKit.framework
   - VideoToolbox.framework
   - AVFoundation.framework
   - CoreMedia.framework
   - CoreVideo.framework

## 性能优化

1. **硬件解码**: VideoToolbox
2. **Metal 渲染**: 高性能 GPU 渲染
3. **零拷贝**: CVPixelBuffer 直接渲染
4. **后台播放**: 音频后台继续播放

## 调试

### Xcode 调试

1. 设置断点
2. 查看 Console 输出
3. Instruments 性能分析

### 日志

```objc
NSLog(@"Player state: %ld", (long)self.player.state);
```

## 发布

1. Archive 构建
2. 导出 IPA
3. 上传到 App Store Connect
4. TestFlight 测试
5. 提交审核

详见 `BUILD.md`。
