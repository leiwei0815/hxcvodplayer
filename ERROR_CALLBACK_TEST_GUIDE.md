# 错误回调测试指南

## 测试场景

### 1. 不可恢复错误（立即回调）

#### 场景 1.1：文件不存在

**测试代码**：
```objc
// 测试本地不存在的文件
[self.player open:@"/path/to/nonexistent.mp4"];

// 预期回调
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    // errorCode: -2 (ENOENT)
    // message: "文件不存在: /path/to/nonexistent.mp4 (错误码: -2, No such file or directory)"
}
```

**预期行为**：
- ✅ **不重试**，立即回调错误
- ✅ 错误消息包含 "文件不存在"
- ✅ 状态变为 `Error`
- ✅ 日志显示 "检测到不可恢复错误：文件不存在"

---

#### 场景 1.2：远程文件 404

**测试代码**：
```objc
// 测试 404 链接
[self.player open:@"https://example.com/nonexistent-video.mp4"];

// 预期回调
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    // message 包含 "文件不存在"
}
```

**预期行为**：
- ✅ 不重试，立即回调错误
- ✅ 错误消息提示文件不存在

---

#### 场景 1.3：权限拒绝

**测试代码**：
```objc
// 测试无权限文件（需要提前创建一个无读权限的文件）
[self.player open:@"/path/to/no-permission.mp4"];

// 预期回调
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    // errorCode: -13 (EACCES)
    // message: "无权限访问: /path/to/no-permission.mp4 (错误码: -13, Permission denied)"
}
```

**预期行为**：
- ✅ 不重试，立即回调错误
- ✅ 错误消息包含 "无权限访问"
- ✅ 状态变为 `Error`

---

#### 场景 1.4：无效数据格式

**测试代码**：
```objc
// 测试非视频文件（如文本文件）
[self.player open:@"/path/to/text-file.txt"];

// 预期回调
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    // message: "文件格式无效或损坏: /path/to/text-file.txt (错误码: -1094995529, Invalid data found when processing input)"
}
```

**预期行为**：
- ✅ 不重试，立即回调错误
- ✅ 错误消息包含 "文件格式无效或损坏"
- ✅ 状态变为 `Error`

---

### 2. 可重试错误（重试后回调）

#### 场景 2.1：连接超时

**测试方法**：
1. 关闭 Wi-Fi 和蜂窝数据
2. 尝试打开网络视频

**测试代码**：
```objc
// 先关闭网络，再测试
[self.player open:@"https://example.com/video.mp4"];

// 预期回调（重试 3 次后）
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    // errorCode: -110 (ETIMEDOUT)
    // message: "重试 3 次后仍然失败: 连接超时，请检查网络连接 (错误码: -110, Connection timed out)"
}
```

**预期行为**：
- ✅ 自动重试 **3 次**（总共 4 次尝试）
- ✅ 每次重试间隔 **1 秒**
- ✅ 日志显示：
  ```
  [ERROR] 打开文件失败 (尝试 1/4)
  [INFO] 检测到网络错误，将进行重试
  [WARNING] 正在重试打开文件... (第 1/3 次)
  [ERROR] 打开文件失败 (尝试 2/4)
  ...
  [ERROR] 重试 3 次后仍然失败
  ```
- ✅ 最终回调错误消息包含 "重试 3 次后仍然失败"

---

#### 场景 2.2：网络不可达

**测试方法**：
1. 开启飞行模式
2. 尝试打开网络视频

**测试代码**：
```objc
// 开启飞行模式后测试
[self.player open:@"https://example.com/video.mp4"];

// 预期回调
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    // message 包含 "网络不可达，请检查网络设置"
}
```

**预期行为**：
- ✅ 重试 3 次
- ✅ 错误消息提示 "网络不可达"

---

#### 场景 2.3：服务器拒绝连接

**测试方法**：
使用一个已关闭的服务器地址

**测试代码**：
```objc
// 测试无法连接的服务器
[self.player open:@"https://192.168.1.999:9999/video.mp4"];

// 预期回调
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    // message 包含 "服务器拒绝连接"
}
```

**预期行为**：
- ✅ 重试 3 次
- ✅ 错误消息提示 "服务器拒绝连接"

---

### 3. 用户取消操作

#### 场景 3.1：加载过程中取消

**测试代码**：
```objc
// 开始播放一个慢速链接
[self.player open:@"https://slow-server.com/large-video.mp4"];

// 2 秒后取消
dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    [self.player stop];
});

// 预期回调
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    // errorCode: AVERROR_EXIT
    // message: "用户取消了播放操作"
}
```

**预期行为**：
- ✅ 立即停止当前尝试
- ✅ 不再重试
- ✅ 状态变为 `Stopped`
- ✅ 错误消息为 "用户取消了播放操作"
- ✅ 日志显示 "用户取消操作，停止重试"

---

#### 场景 3.2：重试过程中取消

**测试代码**：
```objc
// 先关闭网络，触发重试
[self.player open:@"https://example.com/video.mp4"];

// 在第 2 次重试时取消
dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    [self.player stop];
});

// 预期回调
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    // message: "用户取消了播放操作"
}
```

**预期行为**：
- ✅ 停止当前重试
- ✅ 不再继续后续重试
- ✅ 立即回调错误

---

### 4. 重试成功（不回调错误）

#### 场景 4.1：第 2 次重试成功

**测试方法**：
1. 使用网络抓包工具（如 Charles）模拟第 1 次失败
2. 第 2 次允许通过

**预期行为**：
- ✅ 第 1 次失败，触发重试
- ✅ 第 2 次成功
- ✅ **不回调错误**
- ✅ 日志显示 "重试成功！文件已打开"
- ✅ 正常开始播放

---

## 外层错误处理示例

### Objective-C 实现

```objc
#pragma mark - PlayerCore Callbacks

- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    NSLog(@"[播放器错误] 错误码: %d, 消息: %@", errorCode, message);
    
    // 隐藏加载指示器
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.activityIndicator stopAnimating];
    });
    
    // 根据错误类型显示不同的提示
    NSString *title = @"播放失败";
    NSString *buttonTitle = @"确定";
    
    if ([message containsString:@"用户取消"]) {
        // 用户主动取消，不显示错误弹窗
        NSLog(@"用户主动取消播放");
        return;
    }
    else if ([message containsString:@"网络"] || 
             [message containsString:@"超时"] ||
             [message containsString:@"连接"]) {
        title = @"网络错误";
        message = @"网络连接失败，请检查网络设置后重试";
        buttonTitle = @"重试";
    }
    else if ([message containsString:@"不存在"] || 
             [message containsString:@"404"]) {
        title = @"文件不存在";
        message = @"该视频不存在或已被删除";
    }
    else if ([message containsString:@"权限"] || 
             [message containsString:@"403"] ||
             [message containsString:@"401"]) {
        title = @"无权访问";
        message = @"您没有权限访问该视频";
    }
    else if ([message containsString:@"格式"] || 
             [message containsString:@"无效"]) {
        title = @"格式不支持";
        message = @"该视频格式不支持或文件已损坏";
    }
    else if ([message containsString:@"重试"] && [message containsString:@"次"]) {
        // 已经重试多次失败
        title = @"播放失败";
        // 保持原始错误消息
    }
    
    // 显示错误提示
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *alert = [UIAlertController 
            alertControllerWithTitle:title
            message:message
            preferredStyle:UIAlertControllerStyleAlert];
        
        UIAlertAction *action;
        if ([buttonTitle isEqualToString:@"重试"]) {
            action = [UIAlertAction actionWithTitle:buttonTitle
                                             style:UIAlertActionStyleDefault
                                           handler:^(UIAlertAction * _Nonnull action) {
                // 重新尝试播放
                [self retryPlayback];
            }];
        } else {
            action = [UIAlertAction actionWithTitle:buttonTitle
                                             style:UIAlertActionStyleDefault
                                           handler:nil];
        }
        
        [alert addAction:action];
        [self presentViewController:alert animated:YES completion:nil];
    });
    
    // 记录错误统计
    [self logError:errorCode message:message];
}

- (void)retryPlayback {
    // 重新加载当前 URL
    if (self.currentURL) {
        [self.activityIndicator startAnimating];
        [self.player open:self.currentURL];
    }
}

- (void)logError:(int)errorCode message:(NSString *)message {
    // 记录到分析平台（如 Firebase Analytics）
    // [Analytics logEvent:@"player_error" parameters:@{
    //     @"error_code": @(errorCode),
    //     @"error_message": message
    // }];
}
```

---

## 测试清单

### 基础测试

- [ ] **文件不存在**：立即回调，不重试
- [ ] **权限拒绝**：立即回调，不重试
- [ ] **无效格式**：立即回调，不重试
- [ ] **网络超时**：重试 3 次后回调
- [ ] **连接失败**：重试 3 次后回调

### 边界测试

- [ ] **第 1 次重试成功**：不回调错误
- [ ] **第 2 次重试成功**：不回调错误
- [ ] **第 3 次重试成功**：不回调错误
- [ ] **第 4 次（最后）失败**：回调错误

### 用户交互测试

- [ ] **加载时取消**：立即回调 "用户取消"
- [ ] **第 1 次重试时取消**：立即回调 "用户取消"
- [ ] **第 2 次重试时取消**：立即回调 "用户取消"

### 错误消息测试

- [ ] 错误消息包含文件路径
- [ ] 错误消息包含错误码
- [ ] 错误消息包含友好的中文提示
- [ ] 重试失败消息包含 "重试 N 次"

### 状态测试

- [ ] 不可恢复错误后状态为 `Error`
- [ ] 用户取消后状态为 `Stopped`
- [ ] 重试失败后状态为 `Error`

---

## 日志验证

### 不可恢复错误的日志

```
✅ 应包含：
[ERROR] 打开文件失败 (尝试 1/4)
[ERROR] FFmpeg 错误码: ...
[ERROR] 检测到不可恢复错误：[错误类型]，立即停止

❌ 不应包含：
[WARNING] 正在重试打开文件...
[ERROR] 重试 N 次后仍然失败
```

### 重试成功的日志

```
✅ 应包含：
[ERROR] 打开文件失败 (尝试 1/4)
[INFO] 检测到网络错误，将进行重试
[WARNING] 正在重试打开文件... (第 1/3 次)
[INFO] 重试成功！文件已打开

❌ 不应包含：
[ERROR] 重试 N 次后仍然失败
[ERROR] 无法打开文件
```

### 用户取消的日志

```
✅ 应包含：
[INFO] 用户取消操作，停止重试

❌ 不应包含：
[WARNING] 正在重试打开文件...（取消后）
[ERROR] 重试 N 次后仍然失败
```

---

## 性能测试

### 超时时间验证

```
测试配置：
- timeout: 15000000 (15秒)
- stimeout: 5000000 (5秒)
- retry_delay: 1000 (1秒)
- max_retry: 3

预期耗时（最坏情况）：
第 1 次尝试: 15秒 (超时)
等待: 1秒
第 2 次尝试: 15秒 (超时)
等待: 1秒
第 3 次尝试: 15秒 (超时)
等待: 1秒
第 4 次尝试: 15秒 (超时)

总计: 4 × 15 + 3 × 1 = 63 秒
```

**验证方法**：
```objc
NSDate *startTime = [NSDate date];
[self.player open:@"https://timeout-server.com/video.mp4"];

// 在错误回调中
- (void)onPlayerError:(int)errorCode message:(NSString *)message {
    NSTimeInterval elapsed = [[NSDate date] timeIntervalSinceDate:startTime];
    NSLog(@"总耗时: %.1f 秒", elapsed);
    
    // 应该在 60-65 秒之间
    XCTAssertGreaterThan(elapsed, 60);
    XCTAssertLessThan(elapsed, 65);
}
```

---

## 总结

### 错误回调的三种触发方式

1. **立即回调**（不可恢复错误）
   - 文件不存在
   - 权限拒绝
   - 无效数据
   - 功能未实现

2. **延迟回调**（重试失败）
   - 网络超时
   - 连接失败
   - 服务器错误

3. **用户取消**
   - 用户主动停止

### 关键验证点

✅ **错误消息清晰友好**  
✅ **区分不同错误类型**  
✅ **重试次数正确**（3 次）  
✅ **不该重试的不重试**  
✅ **用户取消立即生效**  
✅ **状态转换正确**  
✅ **日志完整清晰**  
