//
//  SeekSlider.h
//  HXCPlayer
//
//  自定义进度条，支持拖动结束时的回调
//

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class SeekSlider;

@protocol SeekSliderDelegate <NSObject>
@optional
/// 用户开始拖动进度条
- (void)seekSliderDidBeginTracking:(SeekSlider *)slider;
/// 用户正在拖动进度条（连续回调）
- (void)seekSliderDidContinueTracking:(SeekSlider *)slider;
/// 用户松手，拖动结束
- (void)seekSliderDidEndTracking:(SeekSlider *)slider;
@end

@interface SeekSlider : NSSlider

@property (nonatomic, weak) id<SeekSliderDelegate> seekDelegate;

@end

NS_ASSUME_NONNULL_END
