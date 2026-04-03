/**
 * @file HXCVDownloadManager.h
 * @brief 视频下载入口：单文件断点续传、HLS(m3u8)、任务管理与 Core Data 持久化
 *
 * -------------------------------------------------------------------------------------------------
 * 后台下载模式（可选，默认关闭）
 * -------------------------------------------------------------------------------------------------
 * 默认使用 `NSURLSessionConfiguration` 的「默认会话」：应用被系统挂起后，传输通常会暂停。
 * 若希望退到后台或进程结束后仍由系统在适当时机继续传输，可开启「后台下载模式」，此时内部改用
 * `backgroundSessionConfigurationWithIdentifier:` 创建会话。
 *
 * 集成步骤概要（请务必按顺序核对 Apple 当前文档，Xcode 文案可能随版本变化）：
 *
 * 1. Xcode 工程
 *    - 打开 **Signing & Capabilities** → **+ Capability** → **Background Modes**。
 *    - 勾选与后台网络传输相关的选项（常见为 **Background fetch**；请以 Apple 文档
 *      *Downloading files in the background* / *URLSession* 后台任务说明为准）。
 *
 * 2. 会话标识符
 *    - 设置 `backgroundSessionIdentifier` 为 **应用内全局唯一、且进程重启后保持不变** 的字符串
 *      （例如 `com.yourcompany.yourapp.hxcvd.bg`）。系统通过该 identifier 在应用重启后重连会话。
 *
 * 3. 冷启动恢复（强烈建议）
 *    - 若用户曾开启后台下载，须在 **第一次** 调用 `+sharedManager` **之前**，把上一步的 identifier
 *      以及是否启用后台模式从 `NSUserDefaults` 等持久化读出，并调用：
 *      `+prepareLaunchConfigurationWithBackgroundSessionIdentifier:backgroundDownloadsEnabled:`。
 *    - 否则进程被系统杀死后再次启动时，无法与之前的后台传输正确重连，任务行为可能异常。
 *
 * 4. 运行时开关
 *    - 在合适的设置界面将 `backgroundDownloadsEnabled` 设为 `YES`（需已设置有效的
 *      `backgroundSessionIdentifier`）。
 *    - **切换前台/后台会话** 仅当当前 **没有正在进行的下载任务**（无占槽中的直链/HLS）时才会生效；
 *      有任务时修改开关会被忽略（可观察控制台 `[HXCVD]` 日志）。
 *
 * 5. 应用代理转发（iOS / macOS）
 *    - 在 `UIApplicationDelegate` / `NSApplicationDelegate` 中实现系统回调
 *      `application:handleEventsForBackgroundURLSession:completionHandler:`，
 *      并调用本类方法 `+notifyBackgroundURLSessionEventsForIdentifier:completionHandler:` 转发。
 *    - 本类在 `URLSessionDidFinishEventsForBackgroundURLSession:` 中调用系统传入的 completionHandler。
 *
 * 6. 行为与限制说明
 *    - 后台会话的 delegate 回调线程、调度时机与默认会话不同；本模块仍会把 UI 相关通知派发到主线程。
 *    - HLS 使用同一 `NSURLSession` 发起多个 data/download 任务；后台模式下同样受系统策略约束。
 *    - macOS 与 iOS 后台策略不同，以各平台文档为准。
 *
 * 7. 进程被强杀 / 崩溃后再次启动
 *    - Core Data 里可能仍为 `Running`，但内存中的任务字典已清空。启动时本管理器会将这些任务一律恢复为 **`Paused`**：
 *      直链若系统里仍有 `NSURLSessionDownloadTask`，会先 `cancelByProducingResumeData` 写入断点再置为暂停；
 *      **不会**自动加入下载队列，是否继续由用户调用 `resumeTaskId:` 决定。
 *    - **用户从多任务界面划掉应用时，`applicationWillTerminate` 往往不会执行**，不要依赖退出回调做关键持久化；
 *      应依赖上述启动对账与平时的进度写入。
 * -------------------------------------------------------------------------------------------------
 */

#import <Foundation/Foundation.h>
#import "HXCVDownloadTypes.h"

@class HXCVDownloadManager;
@class HXCVDownloadItem;

NS_ASSUME_NONNULL_BEGIN

@protocol HXCVDownloadManagerDelegate <NSObject>
@optional

/// 任务状态变化（主线程回调）。`item` 为当前库中数据的快照（新构造的 HXCVDownloadItem 实例，见头文件说明）。
/// `previousState` 与 `item.state` 相同时表示状态未变但任务仍有更新（例如新任务入队等待、仅队列顺序变化），可用于刷新列表。
- (void)downloadManager:(HXCVDownloadManager *)manager didChangeStateForItem:(HXCVDownloadItem *)item previousState:(HXCVDownloadState)previousState;

/// 下载失败（主线程回调）。`item.state == HXCVDownloadStateFailed`；`error` 由 `item.errorMessage` 等构造，可与 `didChangeStateForItem:previousState:` 先后收到。
- (void)downloadManager:(HXCVDownloadManager *)manager didFailDownloadForItem:(HXCVDownloadItem *)item error:(NSError *)error;

/// 下载进度更新（主线程回调）。`item` 为快照；仅在 `Running` 且进度/字节变化时调用，纯状态切换（如暂停/恢复）不会走此回调。
- (void)downloadManager:(HXCVDownloadManager *)manager didUpdateProgressForItem:(HXCVDownloadItem *)item;

/// 任务下载成功完成（主线程回调）。`item.state == HXCVDownloadStateCompleted`；可与 `didChangeStateForItem:previousState:` 先后收到。
- (void)downloadManager:(HXCVDownloadManager *)manager didCompleteDownloadForItem:(HXCVDownloadItem *)item;

@end

@interface HXCVDownloadManager : NSObject

@property (nonatomic, weak, nullable) id<HXCVDownloadManagerDelegate> delegate;

/// 最多同时进行多少个下载任务（直链与 HLS 各计 1）；超出部分保持 `HXCVDownloadStateWaiting` 并按入队顺序启动。默认 3，最小为 1。
@property (nonatomic, assign) NSInteger maxConcurrentDownloads;

/// 是否使用后台 `NSURLSession`（`backgroundSessionConfigurationWithIdentifier:`）。默认 NO。切换仅在没有活动下载任务时生效；开启前必须先设置有效的 `backgroundSessionIdentifier`。
@property (nonatomic, assign) BOOL backgroundDownloadsEnabled;

/// 后台会话 identifier，须全局唯一且在应用重装前保持不变；与 Xcode Capability、`prepareLaunchConfiguration...` 及系统回调中的 identifier 一致。未使用后台模式时可不设置。
@property (nonatomic, copy, nullable) NSString *backgroundSessionIdentifier;

/// 当前下载根目录（file URL），新任务文件写入其下 taskId 子目录
@property (nonatomic, copy, readonly) NSURL *downloadRootDirectoryURL;

/// 须在首次 `+sharedManager` 之前调用（例如 `main` 或 `application:didFinishLaunching...` 最前）。用于从 UserDefaults 等恢复后台会话配置，否则杀进程后无法与未完成的后台传输重连。`identifier` 可为 nil（表示未启用后台或暂不恢复）。
+ (void)prepareLaunchConfigurationWithBackgroundSessionIdentifier:(nullable NSString *)identifier
                                   backgroundDownloadsEnabled:(BOOL)enabled;

+ (instancetype)sharedManager;

/// 将 `application:handleEventsForBackgroundURLSession:completionHandler:` 中系统传入的 `identifier` 与 `completionHandler` 交给管理器；若 identifier 与当前 `backgroundSessionIdentifier` 不一致，会立即调用 completionHandler。
+ (void)notifyBackgroundURLSessionEventsForIdentifier:(NSString *)identifier completionHandler:(void (^)(void))completionHandler;

/// 设置下载根目录（须为本地目录；不存在则创建）。建议在首任务开始前调用；更改后旧任务的 localRelativePath 仍相对历史根目录。
- (BOOL)setDownloadRootDirectoryURL:(NSURL *)directoryURL error:(NSError **)outError;

/// 入队下载；根据 URL 自动判断类型：路径或完整链接中含 `.m3u8`（不区分大小写）则按 HLS 处理，否则为直链。返回 taskId（用户标识为 default）。
- (nullable NSString *)enqueueDownloadWithURL:(NSURL *)url error:(NSError **)outError;

/// 入队下载（自动判断 HLS / 直链）；`identifier` 为空或仅空白时按 @"default" 持久化
- (nullable NSString *)enqueueDownloadWithURL:(NSURL *)url identifier:(nullable NSString *)identifier error:(NSError **)outError;


- (NSArray<HXCVDownloadItem *> *)allTasks;

/// `identifier` 为空或仅空白时，仅返回 default 桶（含旧库 identifier 为 nil 的任务）
- (NSArray<HXCVDownloadItem *> *)allTasksForIdentifier:(nullable NSString *)identifier;

/// 按任务状态获取列表（按 updatedAt 降序，与 allTasks 排序一致）
- (NSArray<HXCVDownloadItem *> *)tasksWithState:(HXCVDownloadState)state;

/// 同上，并按用户标识过滤
- (NSArray<HXCVDownloadItem *> *)tasksWithState:(HXCVDownloadState)state
                                      identifier:(nullable NSString *)identifier;

/// 已完成任务对应的本地 file URL（直链为媒体文件，HLS 为 `local.m3u8`）。`localRelativePath` 无效或文件不存在时返回 nil。
- (nullable NSURL *)playableFileURLForCompletedItem:(HXCVDownloadItem *)item;

- (BOOL)pauseTaskId:(NSString *)taskId error:(NSError **)outError;
- (BOOL)resumeTaskId:(NSString *)taskId error:(NSError **)outError;
- (BOOL)cancelTaskId:(NSString *)taskId error:(NSError **)outError;
/// 删除记录；removeFiles=YES 时同时删除本地目录
- (BOOL)deleteTaskId:(NSString *)taskId removeFiles:(BOOL)removeFiles error:(NSError **)outError;

@end

NS_ASSUME_NONNULL_END
