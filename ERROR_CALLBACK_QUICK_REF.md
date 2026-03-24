# 错误回调快速参考

## 三类错误回调

### 🔴 立即回调（不重试）

```cpp
// 这些错误会立即调用 emit_error() 并返回
AVERROR(ENOENT)          // 文件不存在
AVERROR(EACCES)          // 权限拒绝
AVERROR_INVALIDDATA      // 无效数据
AVERROR_PATCHWELCOME     // 功能未实现
```

**外层处理**：显示错误，不建议重试

---

### 🟡 重试后回调（重试3次）

```cpp
// 这些错误会自动重试 3 次，失败后才回调
AVERROR(ETIMEDOUT)           // 连接超时
AVERROR(ECONNREFUSED)        // 连接被拒绝
AVERROR(ENETUNREACH)         // 网络不可达
AVERROR(EIO)                 // I/O 错误
AVERROR(EAGAIN)              // 资源暂时不可用
AVERROR_HTTP_BAD_REQUEST     // HTTP 400
AVERROR_HTTP_SERVER_ERROR    // HTTP 5xx
```

**外层处理**：提示网络问题，建议手动重试

---

### 🔵 用户取消（立即回调）

```cpp
abort_request_ == true   // 用户调用 stop()
→ emit_error(AVERROR_EXIT, "用户取消了播放操作")
→ set_state(PlayerState::Stopped)
```

**外层处理**：不显示错误提示

---

## 错误消息格式

```
不可恢复: [错误类型]: [url] (错误码: [code], [detail])
重试失败: 重试 3 次后仍然失败: [错误提示] (错误码: [code], [detail])
用户取消: 用户取消了播放操作
```

---

## 外层处理模板

```objc
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    if ([message hasPrefix:@"文件不存在"] ||
        [message hasPrefix:@"无权限"] ||
        [message hasPrefix:@"文件格式无效"] ||
        [message hasPrefix:@"不支持的格式"]) {
        // 🔴 不可恢复错误 - 显示错误，不建议重试
        [self showAlert:@"错误" message:message retry:NO];
    }
    else if ([message hasPrefix:@"重试"]) {
        // 🟡 重试失败 - 提示网络问题，允许手动重试
        [self showAlert:@"网络错误" message:@"请检查网络后重试" retry:YES];
    }
    else if ([message isEqualToString:@"用户取消了播放操作"]) {
        // 🔵 用户取消 - 不显示错误
        NSLog(@"用户取消");
    }
}
```

---

## 状态转换

| 错误类型 | 状态 |
|---------|------|
| 🔴 不可恢复 | `Error` |
| 🟡 重试失败 | `Error` |
| 🔵 用户取消 | `Stopped` |

---

## 重试配置

```cpp
const int MAX_RETRY_COUNT = 3;      // 最大重试 3 次
const int RETRY_DELAY_MS = 1000;    // 间隔 1 秒
```

**总尝试次数**：4 次（1次初始 + 3次重试）

---

## 日志识别

### 立即回调的日志

```
[ERROR] 检测到不可恢复错误：[类型]，立即停止
```

### 重试的日志

```
[INFO] 检测到网络错误，将进行重试
[WARNING] 正在重试打开文件... (第 N/3 次)
[ERROR] 重试 3 次后仍然失败
```

### 用户取消的日志

```
[INFO] 用户取消操作，停止重试
```

---

## 常见错误码

| 错误码 | 含义 | 是否重试 |
|-------|------|---------|
| -2 | 文件不存在 | ❌ |
| -5 | I/O 错误 | ✅ |
| -13 | 权限拒绝 | ❌ |
| -110 | 连接超时 | ✅ |
| -111 | 连接被拒绝 | ✅ |
| -1094995529 | 无效数据 | ❌ |
| -1163346256 | 功能未实现 | ❌ |
| -1414092869 | 用户退出 | ❌ |
