# HLS AES-128 服务端接口规范

## 1. 目标

用于支撑以下播放链路：

- 业务入参：`appID + fileId + sign`
- SDK 先调用播放鉴权接口拿到 `m3u8_url`、`play_session_id`、`expire_at`
- FFmpeg 拉取 m3u8 后，读取 `EXT-X-KEY` 的 `URI` 请求密钥
- 密钥接口通过请求头鉴权，返回 16 字节二进制 key
- FFmpeg 自动完成 ts 下载、解密、解复用、解码

---

## 2. 播放鉴权接口

### 2.1 接口定义

- 方法：`POST`
- 路径示例：`/api/v1/play/auth`
- 说明：校验 `appID/fileId/sign` 后返回播放会话

### 2.2 请求体

```json
{
  "app_id": "10001",
  "file_id": "f_20260427_abc123",
  "sign": "xxxxxxxxxxxxxxxx",
  "device_id": "ios_4f3b2c1d",
  "nonce": "n_1714188000_8f2a",
  "timestamp": 1714188000123
}
```

### 2.3 成功响应

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "m3u8_url": "https://cdn.example.com/vod/f_20260427_abc123/index.m3u8",
    "play_session_id": "ps_9f3c2d5a7b4e",
    "expire_at": 1714188300123,
    "trace_id": "tr_20260427_xxxx"
  }
}
```

### 2.4 失败响应（示例）

```json
{
  "code": 40301,
  "message": "signature invalid",
  "data": null
}
```

### 2.5 推荐错误码

- `40101`：鉴权过期
- `40301`：鉴权失败
- `40302`：重放拦截
- `40001`：参数非法
- `50001`：服务异常

---

## 3. m3u8 内容要求

m3u8 必须包含如下 key 声明：

```text
#EXT-X-KEY:METHOD=AES-128,URI="https://api.example.com/api/v1/play/key?file_id=f_20260427_abc123"
```

要求：

- `METHOD` 必须是 `AES-128`
- `URI` 指向密钥鉴权接口
- 如使用 `IV`，需与加密时一致
- ts 资源建议同样受鉴权保护

---

## 4. 密钥接口（EXT-X-KEY URI）

### 4.1 接口定义

- 方法：`GET`
- 路径示例：`/api/v1/play/key`

### 4.2 请求参数（可选）

- `file_id`

### 4.3 请求头（由 SDK 透传，FFmpeg 自动携带）

推荐至少支持：

- `X-Playback-Session: <play_session_id>`

兼容方式：

- `X-App-Id`
- `X-File-Id`
- `X-Sign`
- `X-Expire-At`

### 4.4 成功响应（关键）

- HTTP `200`
- 响应体：**16 字节二进制原始 key（raw bytes）**
- 可选 `Content-Type: application/octet-stream`

注意：

- 不能直接返回 JSON、hex、base64 字符串给 FFmpeg
- FFmpeg 需要直接拿到 16 字节密钥内容

### 4.5 失败响应

- `401`：会话失效
- `403`：无权限
- `404`：资源不存在

---

## 5. ts 资源接口建议

- 对 ts 请求同样执行会话鉴权
- 推荐优先校验 `X-Playback-Session`
- 防止只保护 key 而 ts 被绕过直连

---

## 6. 安全建议

- 全链路 HTTPS
- 会话 TTL 建议 2~5 分钟
- 会话绑定 `file_id + device_id + app_id`
- 增加 nonce/timestamp 防重放
- key 接口限频
- 日志脱敏，不落 key/sign 明文

---

## 7. 联调验收项

1. 播放鉴权接口正确返回 `m3u8_url/play_session_id/expire_at`
2. m3u8 中存在合法 `EXT-X-KEY`
3. 服务端日志可看到 m3u8/ts/key 请求都带鉴权头
4. key 接口返回长度严格为 16 字节
5. 会话过期时返回 `401/403`，客户端可感知并处理
