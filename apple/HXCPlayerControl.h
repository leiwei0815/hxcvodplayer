/**
 * @file HXCPlayerControl.h
 * @brief Apple 平台统一播放器控制类（支持 iOS 和 macOS）
 */

#ifndef HXCPLAYER_CONTROL_H
#define HXCPLAYER_CONTROL_H

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

// 导入播放器视图
#import "HXCPlayerView.h"
@class HXCPlayerControl;

// 日志等级，默认debug会记录所有日志，release模式下建议开启infoLevel
typedef NS_ENUM(NSInteger, HXCPlayerLogLevel) {
    HXCPlayerLogLevelDebug,
    HXCPlayerLogLevelInfo,
    HXCPlayerLogLevelWarning,
    HXCPlayerLogLevelError
};

// 播放器状态
typedef NS_ENUM(NSInteger, HXCPlayerState) {
    HXCPlayerStateIdle = 0,      // 空闲
    HXCPlayerStateOpening,       // 正在打开
    HXCPlayerStateReady,         // 准备就绪
    HXCPlayerStatePlaying,       // 播放中
    HXCPlayerStatePaused,        // 暂停
    HXCPlayerStateStopped,       // 停止
    HXCPlayerStateError          // 错误
};

// 视频显示模式
typedef NS_ENUM(NSInteger, HXCAspectRatioMode) {
    HXCAspectRatioModeFit = 0,   // 适应（保持宽高比，黑边）
    HXCAspectRatioModeFill       // 填充（裁剪）
};

// ⚠️ 播放器错误码定义（与 C++ 层 PlayerErrorCode 保持一致，全部使用负数）
typedef NS_ENUM(NSInteger, HXCPlayerErrorCode) {
    // 自定义错误码（负数）
    HXCPlayerErrorNone = 0,                             // 无错误
    HXCPlayerErrorInvalidURL = -1001,                   // 无效的 URL
    HXCPlayerErrorOpenInputFailed = -1002,              // 打开输入失败
    HXCPlayerErrorFindStreamInfoFailed = -1003,         // 查找流信息失败
    HXCPlayerErrorNoVideoStream = -1004,                // 没有视频流
    HXCPlayerErrorNoAudioStream = -1005,                // 没有音频流
    HXCPlayerErrorCodecNotFound = -1006,                // 找不到解码器
    HXCPlayerErrorCodecOpenFailed = -1007,              // 打开解码器失败
    HXCPlayerErrorAllocContextFailed = -1008,           // 分配上下文失败
    HXCPlayerErrorSDLInitFailed = -1009,                // SDL 初始化失败
    HXCPlayerErrorAudioDeviceOpenFailed = -1010,        // 音频设备打开失败
    HXCPlayerErrorSeekFailed = -1011,                   // Seek 操作失败
    HXCPlayerErrorReadFrameFailed = -1012,              // 读取帧失败
    HXCPlayerErrorDecodeFailed = -1013,                 // 解码失败
    HXCPlayerErrorOutOfMemory = -1014,                  // 内存不足
    HXCPlayerErrorNotSupportPIPPlayer = -1015,          // 当前设备不支持画中画播放 (iOS特有)
    HXCPlayerErrorAudioSessionConfigFail = -1016,       // 音频会话配置失败 (iOS特有)
    HXCPlayerErrorInputInvalidData = -1018,             // 无效数据
    HXCPlayerErrorNotSupport = -1019,                   // 不支持的格式或协议
    HXCPlayerErrorUnknown = -1099,                      // 未知错误
    
    // 网络相关错误 (-2001 ~ -2999)
    HXCPlayerErrorNetConnectionTimeout = -2001,         // 网络连接超时
    HXCPlayerErrorNetConnectionRefused = -2002,         // 服务器拒绝连接
    HXCPlayerErrorNetUnreachable = -2003,               // 网络不可达
    
    // HTTP 相关错误 (-3001 ~ -3999)
    HXCPlayerErrorHTTPBadRequest = -3001,               // HTTP 请求错误（400）
    HXCPlayerErrorHTTPNotFound = -3002,                 // HTTP 404 文件不存在
    HXCPlayerErrorHTTPServerError = -3003,              // HTTP 服务器错误（5xx）
    HXCPlayerErrorHTTPUnauthorized = -3004,             // 需要身份验证（401）
    HXCPlayerErrorHTTPForbidden = -3005,                // 访问被禁止（403）
    
    // FFmpeg 错误码范围 (负数)
    // 例如：AVERROR_EOF, AVERROR(ENOMEM), AVERROR(EINVAL) 等
    // 可以通过 NSError.code 获取具体的 FFmpeg 错误码
};

// ⚠️ 数据源模式
typedef NS_ENUM(NSInteger, HXCPlayerDataSourceMode) {
    HXCPlayerDataSourceModeDefault = 0,      // 默认模式（FFmpeg 直接打开）
    HXCPlayerDataSourceModeCustomHTTP = 1,   // 自定义 HTTP Range 下载器
    HXCPlayerDataSourceModeCustomFile = 2,   // 本地文件自定义读取（支持加密文件头解密）
};

// ⚠️ 自定义数据源配置
@interface HXCPlayerDataSourceConfig : NSObject
@property (nonatomic, assign) NSInteger timeoutMs;          // 超时时间（毫秒），默认 30000
@property (nonatomic, assign) NSInteger maxRetries;         // 最大重试次数，默认 3
@property (nonatomic, assign) NSUInteger cacheSize;         // 缓存大小（字节），默认 2MB
@property (nonatomic, assign) NSUInteger avioBufferSize;    // AVIO 缓冲区大小（字节），默认 64KB
@property (nonatomic, assign) BOOL encryptedFile;           // 是否为加密文件（仅解密文件头前 100 字节）

// 默认配置
+ (instancetype)defaultConfig;
@end

// 播放器回调协议
@protocol HXCPlayerControlDelegate <NSObject>
@optional
// 状态变化通知
- (void)player:(HXCPlayerControl *)player didChangeState:(HXCPlayerState)state;

// 错误通知
- (void)player:(HXCPlayerControl *)player didFailWithError:(NSError *)error;

// 播放进度更新（真实播放位置）
- (void)player:(HXCPlayerControl *)player didUpdatePosition:(double)position;

// 缓冲进度更新（解码位置）
- (void)player:(HXCPlayerControl *)player didUpdateBufferProgress:(double)position;

// 播放完成通知
- (void)playerDidFinishPlaying:(HXCPlayerControl *)player;

// 网络加载状态通知（isLoading: YES=加载中，NO=加载完成）
- (void)player:(HXCPlayerControl *)player didChangeLoadingState:(BOOL)isLoading;

#if TARGET_OS_IOS

/// 画中画状态
typedef NS_ENUM(NSInteger, HXCPlayerPIPState) {
    //未开始
    HXCPlayerPIPStateNone = 0,
    //即将开始
    HXCPlayerPIPStateWillStart,
    //已经开始
    HXCPlayerPIPStateDidStart,
    //即将停止
    HXCPlayerPIPStateWillStop,
    //已经停止
    HXCPlayerPIPStateDidStop,
    //已经恢复
    HXCPlayerPIPStateRestore
};

// 画中画状态回调
- (void)player:(HXCPlayerControl *)player pictureInPictureStateDidChange:(HXCPlayerPIPState)state;

// 画中画回到应用内恢复ui操作,可以实现这个方法实现自己的ui恢复操作，画中画会默认恢复不需要操作 restored需要返回YES
- (void)player:(HXCPlayerControl *)player
    restoreUserInterfaceForPictureInPictureStopWithCompletionHandler:(void (^)(BOOL restored))completionHandler;
#endif

@end





/**
 * @brief Apple 平台统一播放器控制类
 * 
 * 支持 iOS 和 macOS 平台，使用系统原生的音视频渲染：
 * - 视频: AVSampleBufferDisplayLayer
 * - 音频: AudioQueue
 * - 变速: SoundTouch
 * 
 */
@interface HXCPlayerControl : NSObject

// 属性
@property (nonatomic, weak) id<HXCPlayerControlDelegate> delegate;
@property (nonatomic, assign) double volume;           // 音量 (0.0-1.0)
@property (nonatomic, assign) double playbackRate;     // 播放速度 (0.5-2.0)
@property (nonatomic, assign) double startPosition;    // 起始播放位置（秒）
@property (nonatomic, assign) HXCAspectRatioMode aspectRatioMode;  // 视频显示模式
@property (nonatomic, readonly) HXCPlayerState state;
@property (nonatomic, readonly) double duration;
@property (nonatomic, readonly) double position;
@property (nonatomic, strong, readonly) HXCPlayerView *videoView;  // 视频视图（自动管理布局）

#if TARGET_OS_IOS
// 画中画相关属性（仅 iOS）
@property (nonatomic, readonly) BOOL isPictureInPictureSupported;   // 设备是否支持画中画
@property (nonatomic, readonly) BOOL isPictureInPictureActive;      // 画中画是否正在运行
@property (nonatomic, readonly) BOOL isPictureInPicturePossible;    // 当前是否可以启动画中画
@property (nonatomic, assign) BOOL canStartPictureInPictureAutomaticallyFromInline API_AVAILABLE(ios(14.2));  // 是否允许自动从内联启动画中画（iOS 14.2+）
#endif

// 播放控制
- (BOOL)openURL:(NSString *)url;                      // 打开 URL（不自动播放）
- (BOOL)prepareToPlay:(NSString *)url;                // 准备播放（等同于 openURL，不自动播放）

// 使用指定数据源模式打开（推荐方式）
- (BOOL)openURL:(NSString *)url withMode:(HXCPlayerDataSourceMode)mode config:(HXCPlayerDataSourceConfig *)config;

- (void)play;                                          // 开始播放
- (void)pause;                                         // 暂停
- (void)resume;                                        // 恢复播放
- (void)stop;                                          // 停止播放并释放资源
- (void)replay;                                        // 重新播放
- (void)seekToPosition:(double)position;               // 跳转到指定位置

#if TARGET_OS_IOS
// 画中画控制（仅 iOS）
- (void)startPictureInPicture;                         // 开启画中画
- (void)stopPictureInPicture;                          // 停止画中画
#endif

/// 设置日志等级 需要在播放之前设置
+(void)setLogLevel:(HXCPlayerLogLevel)level;

/// 设置日志存储路径，默认在document/HXCPlayerLogs。需要在播放之前设置
+(void)setLogDir:(NSString *)dir;

@end

#endif // HXCPLAYER_CONTROL_H
