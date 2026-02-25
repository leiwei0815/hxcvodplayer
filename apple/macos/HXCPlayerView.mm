/**
 * @file HXCPlayerView.mm
 * @brief macOS 原生视频显示视图实现
 */

#import "HXCPlayerView.h"
#import "HXCPlayerControl.h"
#import <AVFoundation/AVFoundation.h>

@interface HXCPlayerView ()
@property (nonatomic, weak) HXCPlayerControl *player;
@end

@implementation HXCPlayerView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        [self setupView];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    self = [super initWithCoder:coder];
    if (self) {
        [self setupView];
    }
    return self;
}

- (void)setupView {
    // 设置为 layer-backed view
    self.wantsLayer = YES;
    self.layer.backgroundColor = NSColor.blackColor.CGColor;
}

- (void)setPlayer:(HXCPlayerControl *)player {
    // 移除旧的视频层
    if (_player && _player.videoLayer) {
        [_player.videoLayer removeFromSuperlayer];
    }
    
    _player = player;
    
    // 添加新的视频层
    if (_player && _player.videoLayer) {
        NSLog(@"[PlayerView] 设置播放器 - 添加 VideoLayer, bounds: %@", NSStringFromRect(self.bounds));
        _player.videoLayer.frame = self.bounds;
        [self.layer addSublayer:_player.videoLayer];
        NSLog(@"✅ VideoLayer 已添加到视图, frame: %@", NSStringFromRect(_player.videoLayer.frame));
    } else {
        NSLog(@"❌ VideoLayer 为空！");
    }
}

- (void)layout {
    [super layout];
    
    // 更新视频层尺寸
    if (_player && _player.videoLayer) {
        NSLog(@"[PlayerView] 布局更新 - 新 bounds: %@", NSStringFromRect(self.bounds));
        _player.videoLayer.frame = self.bounds;
    }
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)isFlipped {
    return YES;
}

@end
