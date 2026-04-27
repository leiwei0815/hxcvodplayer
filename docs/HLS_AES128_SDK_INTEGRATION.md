# HLS AES-128 SDK 接入说明

## 1. 总体流程

1. 业务层传入：`appID + fileId + sign`
2. SDK 调后端播放鉴权接口，获取：
   - `m3u8_url`
   - `play_session_id`
   - `expire_at`
3. SDK 组装并透传 FFmpeg 请求头（含 fileId/sign/session 等）
4. FFmpeg 打开 m3u8，自动请求：
   - m3u8
   - EXT-X-KEY 的 URI（取 key）
   - ts 分片
5. FFmpeg 自动完成：ts 解密、解复用、解码

---

## 2. 透传请求头建议

建议使用以下头字段：

- `X-App-Id`
- `X-File-Id`
- `X-Sign`
- `X-Expire-At`
- `X-Playback-Session`

示例（字符串形式，CRLF 分隔）：

```text
X-App-Id: 10001\r\n
X-File-Id: f_20260427_abc123\r\n
X-Sign: xxxxxxxxxxxxxxxx\r\n
X-Expire-At: 1714188300123\r\n
X-Playback-Session: ps_9f3c2d5a7b4e\r\n
```

---

## 3. iOS 接入示例（Objective-C）

```objc
NSDictionary *resp = ...; // 业务网络层请求播放鉴权接口
NSString *m3u8URL = resp[@"data"][@"m3u8_url"];
NSString *sessionId = resp[@"data"][@"play_session_id"];
NSNumber *expireAt = resp[@"data"][@"expire_at"];

HXCSecureHLSOptions *opt = [[HXCSecureHLSOptions alloc] init];
opt.playSessionID = sessionId;
opt.sessionExpireAtMs = expireAt.longLongValue;
opt.secureHeaders =
    [NSString stringWithFormat:
     @"X-App-Id: %@\r\n"
     @"X-File-Id: %@\r\n"
     @"X-Sign: %@\r\n"
     @"X-Expire-At: %@\r\n"
     @"X-Playback-Session: %@\r\n",
     appID, fileID, sign, expireAt, sessionId];

BOOL ok = [player openSecureHLSWithURL:m3u8URL
                              authToken:sign
                                videoID:fileID
                                options:opt];
if (ok) {
    [player play];
}
```

---

## 4. Android 接入示例（Kotlin）

```kotlin
val m3u8Url = authResp.data.m3u8Url
val sessionId = authResp.data.playSessionId
val expireAt = authResp.data.expireAt

val options = HXCPlayerControl.SecureHLSOptions().apply {
    playSessionID = sessionId
    sessionExpireAtMs = expireAt
    secureHeaders =
        "X-App-Id: $appId\r\n" +
        "X-File-Id: $fileId\r\n" +
        "X-Sign: $sign\r\n" +
        "X-Expire-At: $expireAt\r\n" +
        "X-Playback-Session: $sessionId\r\n"
}

val ok = player.openSecureHLS(
    url = m3u8Url,
    authToken = sign,
    videoID = fileId,
    options = options
)
if (ok) {
    player.play()
}
```

---

## 5. 关键注意点

- key 接口必须返回 16 字节二进制 raw key
- FFmpeg 会自动处理 key 请求、ts 下载、解密、解封装、解码
- 不要在 SDK 再做手工 ts 解密（模式 B）
- 过期处理建议：播放失败时触发一次重新鉴权再重开

---

## 6. 联调检查清单

1. m3u8 请求带鉴权头
2. key URI 请求带相同鉴权头
3. ts 请求带相同鉴权头
4. key 返回 16 字节时可正常播放
5. key 返回字符串时应失败（用于验证链路）
6. 会话过期时应返回 401/403 并正确上报错误
