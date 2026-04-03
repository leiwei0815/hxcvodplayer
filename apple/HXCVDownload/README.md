# HXCVD 视频下载模块

基于 **NSURLSession** 的 Objective-C 下载组件，类名与文件名统一 **`HXCVD`** 前缀；任务记录使用 **Core Data**（模型由代码创建，不依赖 `.xcdatamodeld` 编译步骤）。

## 功能概览

| 能力 | 说明 |
|------|------|
| 单文件（直链）下载 | `NSURLSessionDownloadTask`，支持 **断点续传**（`resumeData`） |
| HTTP 重定向（含 302） | 系统默认跟随；在 `willPerformHTTPRedirection` 中传入 `newRequest` 继续请求 |
| HLS（m3u8） | 解析 **Master** → 取首个 **Variant** → 下载 **Media Playlist** 中的分片，生成本地 **`local.m3u8`** |
| 任务管理 | 入队、列表、暂停、继续、取消、删除（可选删本地目录） |
| 持久化 | Core Data 存任务元数据与 `resumeData`（仅直链暂停） |

## 目录与源文件

```
apple/HXCVDownload/
├── HXCVDownload.h                 # 模块总头
├── HXCVDownloadTypes.h            # 枚举：下载类型、任务状态
├── HXCVDownloadItem.h / .m        # 对外只读任务快照
├── HXCVDownloadItem+Internal.h    # 模块内部构造 Item（业务勿引用）
├── HXCVDPersistenceController.h/m # Core Data 栈与 CRUD
├── HXCVDFileStore.h/m             # 沙盒目录：Application Support/HXCVDownload/<taskId>/
├── HXCVDPlaylistParser.h/m        # m3u8 解析（Master / Media、分片 URL）
├── HXCVDHLSDownloadEngine.h/m     # HLS 分片顺序下载与本地 playlist 生成
└── HXCVDownloadManager.h/m        # 单例入口、NSURLSession 委托、任务调度
```

## 依赖框架

- Foundation、**Core Data**
- 工程需链接：**CoreData.framework**（CMake 已配置 macOS / iOS 示例）

## 核心类型

### `HXCVDownloadType`

- `HXCVDownloadTypeProgressive`：单 URL 文件下载  
- `HXCVDownloadTypeHLS`：入口为 m3u8（Master 或 Media）

### `HXCVDownloadState`

`Waiting` → `Running` → `Paused` / `Completed` / `Failed` / `Cancelled`

### `HXCVDownloadItem`（只读）

常用字段：`taskId`、`urlString`、`finalURLString`、`downloadType`、`state`、`progress`、`bytesWritten`、`totalBytes`、`localRelativePath`（相对 **HXCVDownload 根目录** 的路径）、`errorMessage`、`createdAt`、`updatedAt`。

**快照语义**：每次从 Core Data 映射都会生成**新的** `HXCVDownloadItem` 对象；同一逻辑任务在不同回调里**指针不同**是预期行为。业务侧应以 **`taskId`** 关联行/模型并替换为新快照，或依赖 `HXCVDownloadManagerDelegate` 刷新；不要用 KVO 监听「同一个 item 实例」跨进度更新。

**列表排序**：`allTasks` 等接口按 **`updatedAt` 降序**，进度会频繁更新 `updatedAt`，若整表 `reloadData` 容易造成行序跳动与闪烁；示例界面改为按 **`createdAt`** 稳定排序后，仅对变更任务 `reloadRows`，可减少闪烁。

## API 说明

### 入口：`HXCVDownloadManager`

```objc
#import "HXCVDownload.h"

HXCVDownloadManager *mgr = [HXCVDownloadManager sharedManager];
mgr.delegate = self;   // 可选，见下文「回调」
```

| 方法 | 说明 |
|------|------|
| `-enqueueDownloadWithURL:error:` | 入队下载；根据 URL 自动识别 HLS（含 `.m3u8`）或直链，返回 `taskId` |
| `-enqueueDownloadWithURL:identifier:error:` | 同上，并指定用户标识 |
| `-enqueueDownloadWithURL:type:error:` | 显式指定类型（无扩展名等场景） |
| `-allTasks` | 全部任务（按 `updatedAt` 降序） |
| `-pauseTaskId:error:` | 直链：取消并保存 `resumeData`；HLS：从运行集合移除，分片间停止 |
| `-resumeTaskId:error:` | 直链：`resumeData` 或重新 `downloadTaskWithURL:`；HLS：重新从入口 URL 拉取 |
| `-cancelTaskId:error:` | 取消并标记 `Cancelled` |
| `-deleteTaskId:removeFiles:error:` | 删除 Core Data 记录；`removeFiles==YES` 时删除任务目录 |

### 代理（可选）

```objc
@protocol HXCVDownloadManagerDelegate <NSObject>
@optional
/// 任务状态变化（主线程）。`previousState` 与 `item.state` 相同时表示状态未变但仍有更新（如新任务入队等待）。
- (void)downloadManager:(HXCVDownloadManager *)manager
 didChangeStateForItem:(HXCVDownloadItem *)item
          previousState:(HXCVDownloadState)previousState;
/// 下载失败（主线程）。可与上一方法先后收到。
- (void)downloadManager:(HXCVDownloadManager *)manager
 didFailDownloadForItem:(HXCVDownloadItem *)item
                  error:(NSError *)error;
/// 下载进度（主线程）。仅在 Running 且进度/字节变化时调用。
- (void)downloadManager:(HXCVDownloadManager *)manager
didUpdateProgressForItem:(HXCVDownloadItem *)item;
/// 下载成功完成（主线程）。可与状态变化先后收到。
- (void)downloadManager:(HXCVDownloadManager *)manager
didCompleteDownloadForItem:(HXCVDownloadItem *)item;
@end
```

## 本地存储布局

根目录：`Application Support/HXCVDownload/`（由 `HXCVDFileStore` 创建）。

- **直链**：`<taskId>/<文件名>`，文件名取自 URL `lastPathComponent`，缺省为 `download.bin`。  
- **HLS**：`<taskId>/seg_00000.ts` … + **`local.m3u8`**（相对路径分片名，便于 `AVPlayer` 播本地 HLS）。

`HXCVDownloadItem.localRelativePath` 为相对上述根目录的路径（例如 `UUID/video.mp4` 或 `UUID/local.m3u8`）。

## 行为与限制

1. **HLS 加密（`#EXT-X-KEY` / AES-128）**  
   当前检测到加密即返回错误；需后续接入密钥下载与解密后再写入分片。

2. **HLS 暂停 / 继续**  
   暂停在分片边界生效；**继续**当前实现为**从入口重新下载**（未做已下分片增量续传，可后续扩展）。

3. **直链断点**  
   依赖服务端 **Range / 206** 与系统 `resumeData`；不支持 Range 的服务器只能重下或失败，需业务侧兜底。

4. **后台下载**  
   当前为默认 Session；若需 App 被杀后续传，需改用 **Background `NSURLSessionConfiguration`** 并处理 `application:handleEventsForBackgroundURLSession:`（未在本模块内实现）。

5. **`HXCVDownloadItem+Internal.h`**  
   仅供模块内部构造 `HXCVDownloadItem`，**不要**在业务工程中 `#import`。

## CMake 集成（本仓库）

- **macOS**：`apple/macos/CMakeLists.txt` 已加入 `HXCVD_SOURCES` / 头文件路径，并链接 **CoreData**。  
- **iOS**：`apple/ios/CMakeLists.txt` 同样已加入并链接 **CoreData**。  
- 纯 `.m` 使用 `-x objective-c -fobjc-arc`，与播放器 `.mm` 分离，避免混用 `objective-c++`。

若你在 **独立 Xcode 工程** 中手动集成：将 `apple/HXCVDownload/` 下上述源文件加入 Target，并链接 **CoreData.framework**。

## 参考调用示例

```objc
NSError *err = nil;
NSString *tid = [[HXCVDownloadManager sharedManager]
    enqueueDownloadWithURL:[NSURL URLWithString:@"https://example.com/a.mp4"]
                       error:&err];

// 含 .m3u8 的地址会自动走 HLS
NSString *tid2 = [[HXCVDownloadManager sharedManager]
    enqueueDownloadWithURL:[NSURL URLWithString:@"https://example.com/master.m3u8"]
                       error:&err];
```

播放本地文件时，将 `localRelativePath` 与 `HXCVDFileStore` 的根目录拼接得到绝对路径，或扩展一个 `fullFileURLForItem:` 辅助方法（可按需在业务层封装）。

---

*文档与 `HXCVD` 模块目录：`apple/HXCVDownload/`。*
