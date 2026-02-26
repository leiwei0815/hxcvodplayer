//
//  SeekSlider.m
//  HXCPlayerMacOSTest
//
//  自定义进度条，支持拖动结束时的回调
//

#import "SeekSlider.h"

@interface SeekSliderCell : NSSliderCell
@property (nonatomic, weak) SeekSlider *seekSlider;
@end

@implementation SeekSliderCell

- (BOOL)startTrackingAt:(NSPoint)startPoint inView:(NSView *)controlView {
    // 开始拖动
    if ([self.seekSlider.seekDelegate respondsToSelector:@selector(seekSliderDidBeginTracking:)]) {
        [self.seekSlider.seekDelegate seekSliderDidBeginTracking:self.seekSlider];
    }
    return [super startTrackingAt:startPoint inView:controlView];
}

- (BOOL)continueTracking:(NSPoint)lastPoint at:(NSPoint)currentPoint inView:(NSView *)controlView {
    // 正在拖动
    BOOL result = [super continueTracking:lastPoint at:currentPoint inView:controlView];
    if ([self.seekSlider.seekDelegate respondsToSelector:@selector(seekSliderDidContinueTracking:)]) {
        [self.seekSlider.seekDelegate seekSliderDidContinueTracking:self.seekSlider];
    }
    return result;
}

- (void)stopTracking:(NSPoint)lastPoint at:(NSPoint)stopPoint inView:(NSView *)controlView mouseIsUp:(BOOL)flag {
    // 拖动结束
    [super stopTracking:lastPoint at:stopPoint inView:controlView mouseIsUp:flag];
    if ([self.seekSlider.seekDelegate respondsToSelector:@selector(seekSliderDidEndTracking:)]) {
        [self.seekSlider.seekDelegate seekSliderDidEndTracking:self.seekSlider];
    }
}

@end

@implementation SeekSlider

+ (Class)cellClass {
    return [SeekSliderCell class];
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        [self setupCell];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    self = [super initWithCoder:coder];
    if (self) {
        [self setupCell];
    }
    return self;
}

- (void)setupCell {
    if ([self.cell isKindOfClass:[SeekSliderCell class]]) {
        ((SeekSliderCell *)self.cell).seekSlider = self;
    }
}

@end
