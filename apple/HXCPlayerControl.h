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

// 对外枚举/类型
#import "HXCPlayerTypes.h"

// 导入播放器视图
#import "HXCPlayerView.h"
@class HXCPlayerControl;

NS_ASSUME_NONNULL_BEGIN

// ⚠️ 自定义数据源配置
@interface HXCPlayerDataSourceConfig : NSObject
@property (nonatomic, assign) NSInteger timeoutMs;          // 超时时间（毫秒），默认 30000
@property (nonatomic, assign) NSInteger maxRetries;         // 最大重试次数，默认 3
@property (nonatomic, assign) NSUInteger cacheSize;         // 缓存大小（字节），默认 2MB
@property (nonatomic, assign) NSUInteger avioBufferSize;    // AVIO 缓冲区大小（字节），默认 64KB

// 默认配置
+ (instancetype)defaultConfig;

/// 在播放前调用一次，用你自己的全局默认值覆盖 `defaultConfig` 的字段。
/// 之后每次播放只需要通过 `HXCPlayerDataSourcePlayModel` 传入 `url/mode/encryptedFile`。
+ (void)configureDefaultConfig:(nullable HXCPlayerDataSourceConfig *)config;
@end

/// 视频模型
@interface HXCPlayerVideo: NSObject
/// 视频id
@property (nonatomic, copy) NSString *videoId;
/// 签名
@property (nonatomic, copy) NSString *sign;
/// 业务密钥 ID（字符串）
@property (nonatomic, copy) NSString *secretId;
/// 时间戳
@property (nonatomic, copy) NSString *timestamp;

@end

/// 对外的播放模型：统一把 `url + mode + encryptedFile` 收敛到一次传参里。
@interface HXCPlayerDataSourcePlayModel : NSObject
@property (nonatomic, copy) NSString *url;
@property (nonatomic, assign) HXCPlayerDataSourceMode mode;
@property (nonatomic, assign) BOOL encryptedFile;
/// 通过 videoId + sign + secretId 播放
@property (nonatomic, strong, nullable) HXCPlayerVideo *video;

+ (instancetype)modelWithURL:(NSString *)url
                         mode:(HXCPlayerDataSourceMode)mode
                  encryptedFile:(BOOL)encryptedFile;
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

/// 弱网 QoE 指标回调：
/// - currentStallMs: 当前这次 loading 的持续时长（毫秒，结束时为本次总时长）
/// - totalStallMs:   本次播放会话累计卡顿时长（毫秒）
/// - reconnectCount: 已触发的自动恢复次数（仅统计 SDK 内部自动恢复）
- (void)player:(HXCPlayerControl *)player
didUpdateNetworkQoEWithCurrentStallMs:(NSInteger)currentStallMs
  totalStallMs:(NSInteger)totalStallMs
reconnectCount:(NSInteger)reconnectCount;

#if TARGET_OS_IOS

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
 * License：若启用 `HXCPlayerLicenseManager` 的门禁（`setPlaybackLicenseGateEnabled:YES`），
 * 需先成功执行 `checkLicenseWithLicenseKey:licenseURL:completionHandler:`（回调仅告知成功/失败），否则
 * `playURL:` / `playWithModel:` / `play` / `replay` / `seekToPosition:` / 画中画启动 等接口会失败并通过 delegate 返回 `HXCPlayerErrorLicenseValidationFailed`。
 * 
 * 支持 iOS 和 macOS 平台，使用系统原生的音视频渲染：
 * - 视频: AVSampleBufferDisplayLayer
 * - 音频: AudioQueue
 * - 变速: SoundTouch
 * 
 */
@interface HXCPlayerControl : NSObject

// 属性
@property (nonatomic, weak, nullable) id<HXCPlayerControlDelegate> delegate;
@property (nonatomic, assign) double volume;           // 音量 (0.0-1.0)
@property (nonatomic, assign) double playbackRate;     // 播放速度 (0.5-2.0)
@property (nonatomic, assign) double startPosition;    // 起始播放位置（秒）
@property (nonatomic, assign) HXCAspectRatioMode aspectRatioMode;  // 视频显示模式
@property (nonatomic, assign) HXCPlayerDecodeMode decodeMode;  // 解码模式（默认软解，需在 open 前设置）
@property (nonatomic, assign) BOOL autoPlayer; // 是否在打开成功后自动播放，默认 YES
@property (nonatomic, readonly) HXCPlayerState state;
@property (nonatomic, readonly) double duration;
@property (nonatomic, readonly) double position;
@property (nonatomic, readonly) BOOL isHardwareDecodingActive; // 当前视频是否使用硬解（失败回退软解后为 NO）
@property (nonatomic, strong, readonly) HXCPlayerView *videoView;  // 视频视图（自动管理布局）

/// 是否启用“可恢复错误后自动重开一次”（默认 NO）
@property (nonatomic, assign) BOOL autoReopenOnRecoverableErrorEnabled;

/// 自动重开最大次数（默认 1，最小 0）
@property (nonatomic, assign) NSInteger autoReopenMaxAttempts;

#if TARGET_OS_IOS
// 画中画相关属性（仅 iOS）
@property (nonatomic, readonly) BOOL isPictureInPictureSupported;   // 设备是否支持画中画
@property (nonatomic, readonly) BOOL isPictureInPictureActive;      // 画中画是否正在运行
@property (nonatomic, readonly) BOOL isPictureInPicturePossible;    // 当前是否可以启动画中画
@property (nonatomic, assign) BOOL canStartPictureInPictureAutomaticallyFromInline API_AVAILABLE(ios(14.2));  // 是否允许自动从内联启动画中画（iOS 14.2+）
#endif

// 播放控制
- (BOOL)playURL:(NSString *)url;                      // 打开 URL（按 autoPlayer 决定是否自动播放）

/// 使用播放模型打开：内部自动构建 `HXCPlayerDataSourceConfig`（仅通过 `encryptedFile` 差异化），并按 autoPlayer 决定是否自动播放。
- (BOOL)playWithModel:(HXCPlayerDataSourcePlayModel *)model;

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

/// 当前日志等级（与 `setLogLevel:` 枚举一致；未设置时与核心默认一致，一般为 Info）
+(HXCPlayerLogLevel)currentLogLevel;

/// 当前文件日志目录（与 `setLogDir:` 一致；未启用文件日志时为空字符串）
+(NSString *)currentLogDirectory;

/// 当前正在写入的日志文件完整路径（未轮转前；未启用文件日志时为空字符串）
+(NSString *)currentLogFilePath;

/// 设置日志存储路径（会创建目录并启用文件日志）。若未调用，则在首个 `HXCPlayerControl` 初始化时使用默认路径 `Documents/HXCPlayerLogs`。
+(void)setLogDir:(NSString *)dir;

/// 关闭文件日志并刷新队列（全局单例）；通常在 App 退出或不再需要写文件时调用。不要在每个播放器 `dealloc` 里调用。
+(void)disableFileLogging;

@end

NS_ASSUME_NONNULL_END

#endif // HXCPLAYER_CONTROL_H
