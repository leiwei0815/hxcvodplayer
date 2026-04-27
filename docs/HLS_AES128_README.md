# HLS AES-128 加密视频播放落地方案（README）

## 1. 目标与边界

- 支持 `appID + fileId + sign` 播放前鉴权，获取播放会话信息。
- 播放链路：拉流 -> AES-128 解密 -> FFmpeg 解复用解码 -> 外层渲染。
- 缓存能力由上层终端实现，SDK 提供可扩展回调与统一协议。
- 不改动现有渲染架构，仅新增 SecureHLS 数据接入能力。

---

## 2. 现状与改造方向

- 现有 `CustomHTTP` 对 `m3u8` 场景会降级，无法统一控制 HLS 多请求。
- 已新增独立 `SecureHLS` 模式，避免与单资源 `CustomHTTP` 混用。
- 通过 FFmpeg `headers` 实现 m3u8/ts/key 请求头透传。

---

## 3. 总体流程（模式 B）

1. 业务侧传入 `appID + fileId + sign`。
2. SDK 请求播放鉴权接口，获取：
   - `m3u8_url`
   - `play_session_id`
   - `expire_at`
3. SDK 设置 FFmpeg 请求头（如 `X-File-Id/X-Sign/X-Playback-Session`）。
4. FFmpeg 解析 m3u8，读取 `EXT-X-KEY` 的 `URI`。
5. FFmpeg 自动携带请求头请求 key 接口。
6. 服务端返回 16 字节二进制密钥。
7. FFmpeg 自动执行 ts 下载、解密、解复用、解码。

---

## 4. 服务端与客户端职责拆分

### 服务端

- 提供播放鉴权接口，返回 `m3u8_url/play_session_id/expire_at`。
- m3u8 中正确生成 `#EXT-X-KEY:METHOD=AES-128,URI="..."`
- key 接口鉴权后返回 **16 字节二进制 raw key**。
- ts 接口建议同样做会话鉴权。

### 客户端（SDK）

- 负责鉴权前置请求与请求头透传。
- 负责会话生命周期管理（过期、重试、错误上报）。
- 不手工解密 ts，依赖 FFmpeg 原生 AES-128 处理。

---

## 5. 已落地能力（本项目）

- Core 新增 `SecureHLS` 模式与安全会话结构。
- C Bridge / iOS / Android / Windows SDK 均已打通 SecureHLS 接口。
- 外层仅需 `playWithModel/openWithPlayModel`；SecureHLS 鉴权与会话组装通过模型参数 + SDK 鉴权回调完成。
- 支持无鉴权回调时的 Header 透传退化路径。
- 预留缓存回调能力，支持后续上层终端扩展。

---

## 6. 文档导航

- 服务端接口与返回规范：[HLS_AES128_BACKEND_API.md](./HLS_AES128_BACKEND_API.md)
- SDK 接入与代码示例：[HLS_AES128_SDK_INTEGRATION.md](./HLS_AES128_SDK_INTEGRATION.md)

---

## 7. 联调验收标准（摘要）

- m3u8/key/ts 请求均携带鉴权头。
- key 接口返回体严格 16 字节。
- 会话过期时返回 401/403，客户端可感知并处理。
- 弱网情况下可恢复，不出现卡死与崩溃。
