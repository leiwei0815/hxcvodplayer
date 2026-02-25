/**
 * @file HXCPlayerView.mm
 * @brief 统一的播放器视图实现
 */

#import "HXCPlayerView.h"
#import <QuartzCore/QuartzCore.h>

@implementation HXCPlayerView

#if TARGET_OS_IOS
// iOS: 指定使用 AVSampleBufferDisplayLayer 作为 backing layer
+ (Class)layerClass {
    return [AVSampleBufferDisplayLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self setupVideoLayer];
    }
    return self;
}

- (void)setupVideoLayer {
    // iOS: 系统会自动使用 layerClass 指定的类创建 layer
    _videoLayer = (AVSampleBufferDisplayLayer *)self.layer;
    _videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;
    _videoLayer.backgroundColor = [UIColor blackColor].CGColor;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    // iOS: layer 的 frame 会自动跟随 view 的 bounds
}

#else  // macOS

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self setupVideoLayer];
    }
    return self;
}

- (void)setupVideoLayer {
    // macOS: 需要手动设置 layer-backed view
    self.wantsLayer = YES;
    
    // 创建并设置 AVSampleBufferDisplayLayer
    AVSampleBufferDisplayLayer *videoLayer = [[AVSampleBufferDisplayLayer alloc] init];
    videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;
    videoLayer.backgroundColor = [NSColor blackColor].CGColor;
    
    self.layer = videoLayer;
    _videoLayer = videoLayer;
}

- (void)layout {
    [super layout];
    // macOS: layer 的 frame 会自动跟随 view 的 bounds
}

- (BOOL)isFlipped {
    // macOS: 使用翻转坐标系，与 iOS 保持一致
    return YES;
}

#endif

@end
