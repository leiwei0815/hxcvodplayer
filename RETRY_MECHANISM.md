# avformat_open_input 重试机制实现说明

## 为什么不在 interrupt_callback 中实现？

### interrupt_callback 的作用

`interrupt_callback` 是用来**中断**长时间阻塞操作的，比如：
- 用户点击取消按钮
- 网络请求超时需要强制中断
- 应用切换到后台需要暂停操作

它的回调函数返回值：
- `0`: 继续操作
- `非0`: 中断操作

### 为什么不适合实现重试？

1. **时机不对**: `interrupt_callback` 在操作**进行中**被调用，而不是操作**失败后**
2. **无法控制重试**: 你无法在回调中重新分配资源或修改参数
3. **职责不符**: 它是用来"终止"操作的，而不是"重试"操作的

---

## 正确的重试实现

### 实现位置

在 `avformat_open_input` **调用后**，根据返回的错误码判断是否需要重试。

### 实现逻辑

```cpp
// 1. 配置重试参数
const int MAX_RETRY_COUNT = 3;
const int RETRY_DELAY_MS = 1000;

// 2. 循环尝试
while (retry_count <= MAX_RETRY_COUNT) {
    // 3. 重试前的准备工作
    if (retry_count > 0) {
        // - 等待一段时间
        // - 检查用户是否取消
        // - 重新分配资源
        // - 重新设置参数
    }
    
    // 4. 尝试打开
    ret = avformat_open_input(...);
    
    // 5. 成功则退出
    if (ret == 0) break;
    
    // 6. 失败，分析错误类型
    // 7. 判断是否应该重试
    // 8. 决定是否继续循环
}

// 9. 最终结果处理
if (ret < 0) {
    // 发送错误回调给外层
    emit_error(...);
}
```

---

## 具体实现细节

### 1. 可重试的错误类型

```cpp
// 网络错误 - 应该重试
AVERROR(ETIMEDOUT)       // 超时
AVERROR(ECONNREFUSED)    // 连接被拒绝
AVERROR(ENETUNREACH)     // 网络不可达
AVERROR(EIO)             // I/O 错误
AVERROR(EAGAIN)          // 资源暂时不可用

// 服务器错误 - 可以重试
AVERROR_HTTP_BAD_REQUEST
AVERROR_HTTP_SERVER_ERROR
```

### 2. 不应重试的错误类型

```cpp
AVERROR(ENOENT)          // 文件不存在
AVERROR(EACCES)          // 权限拒绝
AVERROR_INVALIDDATA      // 无效数据
AVERROR_PATCHWELCOME     // 功能未实现
```

### 3. 重试流程

```
第1次尝试 → 失败（网络超时）
    ↓
等待 1 秒
    ↓
第2次尝试 → 失败（连接被拒绝）
    ↓
等待 1 秒
    ↓
第3次尝试 → 成功！
```

### 4. 重试时的资源管理

每次重试前需要：

```cpp
// 1. 清理旧的 format_ctx
if (format_ctx_) {
    avformat_close_input(&format_ctx_);
    format_ctx_ = nullptr;
}

// 2. 重新分配
format_ctx_ = avformat_alloc_context();

// 3. 重新设置中断回调
format_ctx_->interrupt_callback.callback = ...;
format_ctx_->interrupt_callback.opaque = this;
```

**为什么要重新分配？**
- `avformat_open_input` 失败后，`format_ctx` 可能处于不一致状态
- 重新分配确保干净的状态

---

## 配置参数

### 可调整的参数

```cpp
const int MAX_RETRY_COUNT = 3;      // 最大重试次数（0-5 合理）
const int RETRY_DELAY_MS = 1000;    // 重试间隔（500-5000ms 合理）
```

### 建议配置

| 场景 | 重试次数 | 重试间隔 | 说明 |
|------|---------|---------|------|
| 本地文件 | 0 | 0 | 不需要重试 |
| 稳定网络 | 1-2 | 500ms | 快速失败 |
| 不稳定网络 | 3-5 | 1000ms | 给网络恢复时间 |
| 移动网络 | 3 | 2000ms | 适应网络切换 |

---

## 日志输出

### 成功场景

```
[INFO] 调用 avformat_open_input，URL: https://...
[INFO] 文件打开成功
```

### 重试成功场景

```
[INFO] 调用 avformat_open_input，URL: https://...
[ERROR] 打开文件失败 (尝试 1/4)
[ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[INFO] 检测到网络错误，将进行重试
[WARNING] 正在重试打开文件... (第 1/3 次)
[INFO] 重试成功！文件已打开
```

### 最终失败场景

```
[INFO] 调用 avformat_open_input，URL: https://...
[ERROR] 打开文件失败 (尝试 1/4)
[ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[INFO] 检测到网络错误，将进行重试
[WARNING] 正在重试打开文件... (第 1/3 次)
[ERROR] 打开文件失败 (尝试 2/4)
[ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[WARNING] 正在重试打开文件... (第 2/3 次)
[ERROR] 打开文件失败 (尝试 3/4)
[ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[WARNING] 正在重试打开文件... (第 3/3 次)
[ERROR] 打开文件失败 (尝试 4/4)
[ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[ERROR] 重试 3 次后仍然失败
[ERROR] 无法打开文件: https://...
```

---

## 错误回调

### 回调时机

重试机制会在以下情况下**立即**调用 `emit_error`:

#### 1. 不可恢复的错误（立即回调，不重试）

| 错误类型 | 错误码 | 回调消息 | 说明 |
|---------|--------|---------|------|
| 文件不存在 | `AVERROR(ENOENT)` | `文件不存在: [url]` | 本地文件不存在或远程 404 |
| 权限拒绝 | `AVERROR(EACCES)` | `无权限访问: [url]` | 没有读取权限 |
| 无效数据 | `AVERROR_INVALIDDATA` | `文件格式无效或损坏: [url]` | 不是有效的媒体文件 |
| 功能未实现 | `AVERROR_PATCHWELCOME` | `不支持的格式或协议: [url]` | FFmpeg 不支持该格式 |

#### 2. 用户取消操作

| 触发条件 | 错误码 | 回调消息 | 状态 |
|---------|--------|---------|------|
| 用户点击取消 | `AVERROR_EXIT` | `用户取消了播放操作` | `Stopped` |

#### 3. 重试失败（所有重试用尽后回调）

| 错误类型 | 错误码 | 回调消息 | 说明 |
|---------|--------|---------|------|
| 连接超时 | `AVERROR(ETIMEDOUT)` | `重试 N 次后仍然失败: 连接超时，请检查网络连接` | 网络超时 |
| 连接被拒绝 | `AVERROR(ECONNREFUSED)` | `重试 N 次后仍然失败: 服务器拒绝连接` | 服务器不可用 |
| 网络不可达 | `AVERROR(ENETUNREACH)` | `重试 N 次后仍然失败: 网络不可达，请检查网络设置` | 无网络连接 |
| I/O 错误 | `AVERROR(EIO)` | `重试 N 次后仍然失败: I/O 错误，可能是网络问题` | 通用 I/O 错误 |
| HTTP 400 | `AVERROR_HTTP_BAD_REQUEST` | `重试 N 次后仍然失败: HTTP 请求错误（400）` | 错误的请求 |
| HTTP 404 | `AVERROR_HTTP_NOT_FOUND` | `重试 N 次后仍然失败: 文件不存在（404）` | 资源不存在 |
| HTTP 5xx | `AVERROR_HTTP_SERVER_ERROR` | `重试 N 次后仍然失败: 服务器内部错误（5xx）` | 服务器错误 |
| HTTP 401 | `AVERROR_HTTP_UNAUTHORIZED` | `重试 N 次后仍然失败: 需要身份验证（401）` | 需要登录 |
| HTTP 403 | `AVERROR_HTTP_FORBIDDEN` | `重试 N 次后仍然失败: 访问被禁止（403）` | 无权访问 |

### 回调内容

```cpp
void emit_error(int error_code, const std::string& error_message);
```

**参数说明**：
- `error_code`: FFmpeg 原始错误码（负数）
- `error_message`: 用户友好的错误消息（中文）

### 外层处理示例

```objc
// iOS 端接收错误回调
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    NSLog(@"播放错误: [%d] %@", errorCode, message);
    
    // 根据错误类型显示不同的提示
    if ([message containsString:@"网络"]) {
        [self showAlert:@"网络错误" message:@"请检查网络连接后重试"];
    } else if ([message containsString:@"不存在"]) {
        [self showAlert:@"文件不存在" message:@"该视频已被删除或链接失效"];
    } else if ([message containsString:@"权限"]) {
        [self showAlert:@"无权访问" message:@"您没有权限访问该视频"];
    } else if ([message containsString:@"格式"]) {
        [self showAlert:@"格式不支持" message:@"该视频格式暂不支持"];
    } else if ([message containsString:@"用户取消"]) {
        // 用户主动取消，不显示错误
        NSLog(@"用户取消播放");
    } else {
        [self showAlert:@"播放失败" message:message];
    }
    
    // 清理 UI 状态
    [self.activityIndicator stopAnimating];
    [self.playButton setEnabled:YES];
}
```

### 错误回调流程图

```
┌─────────────────────────┐
│  调用 avformat_open_input │
└───────────┬─────────────┘
            │
            ▼
      ┌─────────┐
      │ 成功？   │
      └────┬────┘
           │
    ┌──────┴──────┐
    │             │
   成功          失败
    │             │
    ▼             ▼
  继续播放    ┌────────────┐
              │ 分析错误类型 │
              └──────┬──────┘
                     │
         ┌───────────┼───────────┐
         │           │           │
    不可恢复      可重试      用户取消
         │           │           │
         ▼           ▼           ▼
    立即回调    尝试重试    立即回调
    emit_error      │       emit_error
         │      ┌───┴───┐       │
         │     成功    失败      │
         │      │       │       │
         │      ▼       ▼       │
         │   继续播放  回调错误  │
         │            emit_error │
         │               │       │
         └───────────────┴───────┘
                     │
                     ▼
              ┌──────────┐
              │ 外层接收  │
              │ 错误回调  │
              └──────────┘
                     │
                     ▼
              ┌──────────┐
              │ 显示错误  │
              │ 提示用户  │
              └──────────┘
```

### 完整的错误处理示例

#### 场景 1：文件不存在（立即回调）

```
[2026-02-24 10:30:15] [INFO] 调用 avformat_open_input，URL: /path/to/missing.mp4
[2026-02-24 10:30:15] [ERROR] 打开文件失败 (尝试 1/4)
[2026-02-24 10:30:15] [ERROR] FFmpeg 错误码: -2, 错误信息: No such file or directory
[2026-02-24 10:30:15] [ERROR] 检测到不可恢复错误：文件不存在，立即停止

→ 立即回调: emit_error(-2, "文件不存在: /path/to/missing.mp4 (错误码: -2, No such file or directory)")
→ 状态: Error
```

#### 场景 2：网络超时（重试后回调）

```
[2026-02-24 10:30:20] [INFO] 调用 avformat_open_input，URL: https://slow-server.com/video.mp4
[2026-02-24 10:30:35] [ERROR] 打开文件失败 (尝试 1/4)
[2026-02-24 10:30:35] [ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[2026-02-24 10:30:35] [INFO] 检测到网络错误，将进行重试
[2026-02-24 10:30:35] [WARNING] 正在重试打开文件... (第 1/3 次)
[2026-02-24 10:30:51] [ERROR] 打开文件失败 (尝试 2/4)
[2026-02-24 10:30:51] [ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[2026-02-24 10:30:51] [WARNING] 正在重试打开文件... (第 2/3 次)
[2026-02-24 10:31:07] [ERROR] 打开文件失败 (尝试 3/4)
[2026-02-24 10:31:07] [ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[2026-02-24 10:31:07] [WARNING] 正在重试打开文件... (第 3/3 次)
[2026-02-24 10:31:23] [ERROR] 打开文件失败 (尝试 4/4)
[2026-02-24 10:31:23] [ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[2026-02-24 10:31:23] [ERROR] 已达到最大重试次数，停止重试
[2026-02-24 10:31:23] [ERROR] 重试 3 次后仍然失败

→ 回调: emit_error(-110, "重试 3 次后仍然失败: 连接超时，请检查网络连接 (错误码: -110, Connection timed out)")
→ 状态: Error
```

#### 场景 3：用户取消（立即回调）

```
[2026-02-24 10:32:00] [INFO] 调用 avformat_open_input，URL: https://example.com/video.mp4
[2026-02-24 10:32:05] [ERROR] 打开文件失败 (尝试 1/4)
[2026-02-24 10:32:05] [ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[2026-02-24 10:32:05] [INFO] 检测到网络错误，将进行重试
[2026-02-24 10:32:05] [WARNING] 正在重试打开文件... (第 1/3 次)
[2026-02-24 10:32:08] [INFO] 用户取消操作，停止重试

→ 立即回调: emit_error(-1414092869, "用户取消了播放操作")
→ 状态: Stopped
```

#### 场景 4：重试成功（不回调错误）

```
[2026-02-24 10:33:00] [INFO] 调用 avformat_open_input，URL: https://example.com/video.mp4
[2026-02-24 10:33:05] [ERROR] 打开文件失败 (尝试 1/4)
[2026-02-24 10:33:05] [ERROR] FFmpeg 错误码: -110, 错误信息: Connection timed out
[2026-02-24 10:33:05] [INFO] 检测到网络错误，将进行重试
[2026-02-24 10:33:05] [WARNING] 正在重试打开文件... (第 1/3 次)
[2026-02-24 10:33:07] [INFO] 重试成功！文件已打开

→ 不回调错误，继续正常播放
```

---

## 用户取消支持

### 中断回调的正确用法

```cpp
format_ctx_->interrupt_callback.callback = [](void* ctx) -> int {
    PlayerCore* player = static_cast<PlayerCore*>(ctx);
    return player->abort_request_.load() ? 1 : 0;
};
```

### 重试循环中的检查

```cpp
if (abort_request_.load()) {
    LOG_INFO("用户取消操作，停止重试");
    break;
}
```

### 工作流程

```
用户点击取消
    ↓
设置 abort_request_ = true
    ↓
① interrupt_callback 返回 1 → 中断当前的 avformat_open_input
② 重试循环检查 abort_request_ → 停止重试
    ↓
立即退出
```

---

## 性能考虑

### 重试开销

```
总耗时 = (尝试次数) × (超时时间 + 重试间隔)

示例：
- 超时时间: 5 秒
- 重试间隔: 1 秒
- 重试次数: 3

最坏情况: 4 × (5 + 1) = 24 秒
```

### 优化建议

1. **首次超时时间可以设长一些**（如 15 秒）
2. **重试时超时可以设短一些**（如 5 秒）
3. **使用指数退避**（如 1秒 → 2秒 → 4秒）

---

## 未来扩展

### 可以添加的功能

1. **指数退避**
   ```cpp
   RETRY_DELAY_MS = BASE_DELAY * (2 ^ retry_count)
   ```

2. **动态调整超时**
   ```cpp
   if (retry_count > 0) {
       av_dict_set(&options, "timeout", "5000000", 0);  // 重试时用短超时
   }
   ```

3. **记录重试统计**
   ```cpp
   struct RetryStats {
       int total_retries;
       int successful_retries;
       int failed_retries;
   };
   ```

4. **自适应重试**
   ```cpp
   // 根据网络类型调整重试策略
   if (is_mobile_network) {
       MAX_RETRY_COUNT = 5;
       RETRY_DELAY_MS = 2000;
   }
   ```

---

## 总结

### 关键点

1. ✅ **重试在调用后实现**，不是在 `interrupt_callback` 中
2. ✅ **根据错误类型判断**是否应该重试
3. ✅ **每次重试前重新分配资源**
4. ✅ **支持用户取消**操作
5. ✅ **失败后通过 `emit_error` 回调**给外层

### interrupt_callback 的正确用法

- ✅ 用于：中断长时间阻塞的操作
- ❌ 不用于：实现重试逻辑

### 重试机制的优点

- ✅ 提高网络不稳定时的成功率
- ✅ 用户体验更好（自动恢复）
- ✅ 减少因临时网络问题导致的播放失败
- ✅ 灵活可配置

---

**文件位置**: `core/src/hxc_player_core.cpp`  
**修改行数**: 约 245-350 行  
**状态**: 已实现 ✅
