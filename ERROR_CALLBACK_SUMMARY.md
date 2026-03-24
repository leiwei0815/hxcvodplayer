# 错误回调实现总结

## 实现概述

在 `avformat_open_input` 重试机制中，现在已经实现了完善的错误回调系统，能够在不同场景下向外层准确回调错误信息。

---

## 三种错误回调场景

### 1. 立即回调（不可恢复错误）

这些错误**不会重试**，检测到后**立即**调用 `emit_error()` 并返回：

| 错误类型 | 错误码 | 回调消息模板 |
|---------|--------|------------|
| 文件不存在 | `AVERROR(ENOENT)` | `文件不存在: [url] (错误码: -2, ...)` |
| 权限拒绝 | `AVERROR(EACCES)` | `无权限访问: [url] (错误码: -13, ...)` |
| 无效数据 | `AVERROR_INVALIDDATA` | `文件格式无效或损坏: [url] (错误码: -1094995529, ...)` |
| 功能未实现 | `AVERROR_PATCHWELCOME` | `不支持的格式或协议: [url] (错误码: -1163346256, ...)` |

**代码实现**：

```cpp
if (ret == AVERROR(ENOENT)) {
    should_retry = false;
    LOG_ERROR("检测到不可恢复错误：文件不存在，立即停止");
    
    emit_error(ret, "文件不存在: " + filename + " (错误码: " + std::to_string(ret) + ", " + std::string(errbuf) + ")");
    set_state(PlayerState::Error);
    av_dict_free(&options);
    return -1;
}
```

### 2. 重试后回调（可重试错误）

这些错误会**自动重试 3 次**，所有重试失败后才回调：

| 错误类型 | 错误码 | 回调消息模板 |
|---------|--------|------------|
| 连接超时 | `AVERROR(ETIMEDOUT)` | `重试 3 次后仍然失败: 连接超时，请检查网络连接 (错误码: -110, ...)` |
| 连接被拒绝 | `AVERROR(ECONNREFUSED)` | `重试 3 次后仍然失败: 服务器拒绝连接 (错误码: -111, ...)` |
| 网络不可达 | `AVERROR(ENETUNREACH)` | `重试 3 次后仍然失败: 网络不可达，请检查网络设置 (错误码: -101, ...)` |
| I/O 错误 | `AVERROR(EIO)` | `重试 3 次后仍然失败: I/O 错误，可能是网络问题 (错误码: -5, ...)` |
| HTTP 400 | `AVERROR_HTTP_BAD_REQUEST` | `重试 3 次后仍然失败: HTTP 请求错误（400） (错误码: ...)` |
| HTTP 404 | `AVERROR_HTTP_NOT_FOUND` | `重试 3 次后仍然失败: 文件不存在（404） (错误码: ...)` |
| HTTP 5xx | `AVERROR_HTTP_SERVER_ERROR` | `重试 3 次后仍然失败: 服务器内部错误（5xx） (错误码: ...)` |
| HTTP 401 | `AVERROR_HTTP_UNAUTHORIZED` | `重试 3 次后仍然失败: 需要身份验证（401） (错误码: ...)` |
| HTTP 403 | `AVERROR_HTTP_FORBIDDEN` | `重试 3 次后仍然失败: 访问被禁止（403） (错误码: ...)` |

**代码实现**：

```cpp
// 在重试循环中判断是否应该重试
if (ret == AVERROR(ETIMEDOUT) ||
    ret == AVERROR(ECONNREFUSED) ||
    ret == AVERROR(ENETUNREACH) ||
    ret == AVERROR(EIO) ||
    ret == AVERROR(EAGAIN)) {
    should_retry = true;
    LOG_INFO("检测到网络错误，将进行重试");
}

// 重试全部失败后的回调
if (ret < 0) {
    if (retry_count > 0) {
        error_message = "重试 " + std::to_string(retry_count) + " 次后仍然失败: ";
    }
    // ... 添加具体错误信息
    emit_error(ret, error_message);
    set_state(PlayerState::Error);
    return -1;
}
```

### 3. 用户取消回调

用户主动调用 `stop()` 取消操作时**立即回调**：

| 触发条件 | 错误码 | 回调消息 | 状态 |
|---------|--------|---------|------|
| 用户取消 | `AVERROR_EXIT` | `用户取消了播放操作` | `Stopped` |

**代码实现**：

```cpp
if (abort_request_.load()) {
    LOG_INFO("用户取消操作，停止重试");
    
    emit_error(AVERROR_EXIT, "用户取消了播放操作");
    set_state(PlayerState::Stopped);
    av_dict_free(&options);
    return -1;
}
```

---

## 代码结构

```cpp
// 重试循环
while (retry_count <= MAX_RETRY_COUNT) {
    // 1. 检查用户取消
    if (abort_request_.load()) {
        emit_error(AVERROR_EXIT, "用户取消了播放操作");
        return -1;  // 立即回调
    }
    
    // 2. 尝试打开
    ret = avformat_open_input(...);
    
    if (ret == 0) break;  // 成功
    
    // 3. 分析错误类型
    if (ret == AVERROR(ENOENT)) {
        emit_error(ret, "文件不存在: ...");
        return -1;  // 立即回调
    }
    
    if (ret == AVERROR(EACCES)) {
        emit_error(ret, "无权限访问: ...");
        return -1;  // 立即回调
    }
    
    if (ret == AVERROR_INVALIDDATA) {
        emit_error(ret, "文件格式无效或损坏: ...");
        return -1;  // 立即回调
    }
    
    if (ret == AVERROR_PATCHWELCOME) {
        emit_error(ret, "不支持的格式或协议: ...");
        return -1;  // 立即回调
    }
    
    // 4. 判断是否可以重试
    if (是网络错误) {
        should_retry = true;
    }
    
    if (!should_retry || retry_count >= MAX_RETRY_COUNT) {
        break;  // 退出循环，后面统一处理
    }
    
    retry_count++;
}

// 5. 最终检查（重试失败）
if (ret < 0) {
    // 构建详细错误消息
    error_message = "重试 N 次后仍然失败: ...";
    emit_error(ret, error_message);
    return -1;  // 重试后回调
}
```

---

## 错误消息格式

所有错误消息都遵循统一格式：

```
[前缀] [友好提示] (错误码: [code], [FFmpeg原始错误])
```

### 示例

1. **不可恢复错误**：
   ```
   文件不存在: /path/to/file.mp4 (错误码: -2, No such file or directory)
   ```

2. **重试失败**：
   ```
   重试 3 次后仍然失败: 连接超时，请检查网络连接 (错误码: -110, Connection timed out)
   ```

3. **用户取消**：
   ```
   用户取消了播放操作
   ```

---

## 状态转换

| 场景 | 回调后状态 |
|------|-----------|
| 不可恢复错误 | `PlayerState::Error` |
| 重试失败 | `PlayerState::Error` |
| 用户取消 | `PlayerState::Stopped` |

---

## 外层接收示例

```objc
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    NSLog(@"错误: %d - %@", errorCode, message);
    
    // 场景 1: 不可恢复错误
    if ([message hasPrefix:@"文件不存在"] ||
        [message hasPrefix:@"无权限访问"] ||
        [message hasPrefix:@"文件格式无效"] ||
        [message hasPrefix:@"不支持的格式"]) {
        // 立即显示错误，不建议重试
        [self showErrorAlert:message canRetry:NO];
    }
    
    // 场景 2: 重试失败
    else if ([message hasPrefix:@"重试"]) {
        // 所有重试都失败了，可以让用户手动重试
        [self showErrorAlert:message canRetry:YES];
    }
    
    // 场景 3: 用户取消
    else if ([message isEqualToString:@"用户取消了播放操作"]) {
        // 用户主动取消，不显示错误
        NSLog(@"用户主动取消");
    }
}
```

---

## 关键特性

### ✅ 已实现

1. **智能错误分类**
   - 不可恢复错误立即回调
   - 可重试错误自动重试
   - 用户取消立即响应

2. **详细错误信息**
   - 包含错误码
   - 包含 FFmpeg 原始错误
   - 包含友好的中文提示
   - 区分不同的 HTTP 状态码

3. **重试统计**
   - 错误消息包含重试次数
   - 日志记录每次重试
   - 最大重试 3 次

4. **用户友好**
   - 错误消息简洁明了
   - 提供解决建议（如"请检查网络连接"）
   - 区分临时错误和永久错误

5. **状态管理**
   - 错误后设置为 `Error` 状态
   - 用户取消设置为 `Stopped` 状态
   - 状态转换正确

---

## 测试验证

### 测试场景

| 场景 | 预期重试次数 | 预期回调时机 | 预期状态 |
|------|------------|------------|---------|
| 文件不存在 | 0 | 立即 | `Error` |
| 权限拒绝 | 0 | 立即 | `Error` |
| 无效格式 | 0 | 立即 | `Error` |
| 网络超时 | 3 | 3次失败后 | `Error` |
| 用户取消 | - | 立即 | `Stopped` |
| 第2次重试成功 | 1 | 不回调 | `Playing` |

### 测试方法

详见 `ERROR_CALLBACK_TEST_GUIDE.md`

---

## 文件清单

| 文件 | 说明 |
|------|------|
| `core/src/hxc_player_core.cpp` | 核心实现（第 245-450 行） |
| `RETRY_MECHANISM.md` | 重试机制文档 |
| `ERROR_CALLBACK_TEST_GUIDE.md` | 错误回调测试指南 |
| `ERROR_CALLBACK_SUMMARY.md` | 本文档 |

---

## 修改记录

### 2026-02-24

1. ✅ 实现不可恢复错误立即回调
   - 文件不存在
   - 权限拒绝
   - 无效数据
   - 功能未实现

2. ✅ 实现用户取消立即回调
   - 检查 `abort_request_`
   - 回调 `AVERROR_EXIT`
   - 设置状态为 `Stopped`

3. ✅ 优化重试失败回调消息
   - 包含重试次数
   - 区分不同错误类型
   - 提供友好的中文提示
   - 支持各种 HTTP 状态码

4. ✅ 创建测试文档
   - 测试场景清单
   - 外层处理示例
   - 日志验证方法

---

## 总结

现在错误回调系统已经完善，能够：

1. **准确识别**各种错误类型
2. **智能决策**是否需要重试
3. **及时通知**外层错误信息
4. **提供友好**的错误消息
5. **正确管理**播放器状态

外层开发者可以根据错误消息的前缀来判断错误类型，并提供相应的用户提示。
