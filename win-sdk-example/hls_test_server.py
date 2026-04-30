#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
HLS AES-128 本地测试服务器
用途：在 Windows 本地快速验证 HXCPlayer SecureHLS 解密链路

服务内容：
  GET /stream.m3u8  → 包含 EXT-X-KEY 的 m3u8 播放列表
  GET /seg{n}.ts    → AES-128-CBC 加密的 TS 分片（由彩色测试帧生成）
  GET /key          → 返回 16 字节原始二进制密钥（FFmpeg 需要的格式）
  GET /key_bad      → 返回 Base64 字符串（故意错误，用于复现解密失败）
  GET /key_json     → 返回 JSON（故意错误，用于复现解密失败）
  GET /status       → 服务状态与诊断信息

依赖：pip install cryptography  (用于 AES 加密)
      FFmpeg 命令行（用于生成测试 TS 分片），可选

运行：python hls_test_server.py
      python hls_test_server.py --port 8765 --bad-key  # 测试错误 key 格式
"""

import argparse
import base64
import json
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

PORT = 8765
HOST = "127.0.0.1"

# 固定 16 字节 AES-128 密钥（生产环境应随机生成并安全存储）
AES_KEY = bytes.fromhex("0123456789abcdef0123456789abcdef")
# IV：使用分片序号（与 FFmpeg 默认行为一致）
SEGMENT_COUNT = 4
SEGMENT_DURATION = 3  # 秒

# 全局：key 接口返回模式
BAD_KEY_MODE = None   # None=正常, "base64", "json", "hex"

# ==================== TS 分片生成 ====================

def try_generate_ts_with_ffmpeg(seg_index: int, out_path: str) -> bool:
    """用 FFmpeg 生成彩色测试视频 TS 分片（若 FFmpeg 不在 PATH 则跳过）"""
    colors = ["red", "green", "blue", "yellow"]
    color = colors[seg_index % len(colors)]
    cmd = [
        "ffmpeg", "-y",
        "-f", "lavfi",
        "-i", f"color=c={color}:size=640x360:rate=25:duration={SEGMENT_DURATION}",
        "-f", "lavfi", "-i", "sine=frequency=440:duration=" + str(SEGMENT_DURATION),
        "-c:v", "libx264", "-profile:v", "baseline", "-level", "3.0",
        "-c:a", "aac", "-ar", "44100",
        "-f", "mpegts",
        out_path
    ]
    try:
        result = subprocess.run(
            cmd, capture_output=True, timeout=30
        )
        return result.returncode == 0
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False


def generate_minimal_ts() -> bytes:
    """
    生成最小合法的 MPEG-TS 数据（188 字节 PAT + 188 字节 PMT + 若干空包）
    用于 FFmpeg 不可用时的降级方案。
    播放器会报解码错误（因为没有真实视频），但解密链路可以被完整验证。
    """
    # PAT packet
    pat = bytearray(188)
    pat[0] = 0x47          # sync byte
    pat[1] = 0x40          # PUSI=1, PID high=0
    pat[2] = 0x00          # PID low (PID=0 → PAT)
    pat[3] = 0x10          # no scrambling, payload only, CC=0
    pat[4] = 0x00          # pointer field
    # PAT section
    pat[5] = 0x00          # table_id = 0 (PAT)
    pat[6] = 0xB0
    pat[7] = 0x0D          # section_length = 13
    pat[8] = 0x00; pat[9] = 0x01    # transport_stream_id
    pat[10] = 0xC1         # version + current_next
    pat[11] = 0x00; pat[12] = 0x00  # section number / last section
    # program: program_number=1, PMT PID=0x100
    pat[13] = 0x00; pat[14] = 0x01  # program_number
    pat[15] = 0xE1; pat[16] = 0x00  # PMT PID = 0x100
    # CRC32 (dummy, FFmpeg will handle)
    pat[17:21] = b'\x00\x00\x00\x00'
    for i in range(21, 188):
        pat[i] = 0xFF

    # Null packets × 8 (padding)
    null_pkt = bytearray(188)
    null_pkt[0] = 0x47
    null_pkt[1] = 0x1F; null_pkt[2] = 0xFF   # PID=0x1FFF (null)
    null_pkt[3] = 0x10
    for i in range(4, 188):
        null_pkt[i] = 0xFF

    return bytes(pat) + bytes(null_pkt) * 8


def encrypt_ts(plaintext: bytes, seg_index: int) -> bytes:
    """AES-128-CBC 加密 TS 分片，IV = 分片序号（大端 16 字节）"""
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.backends import default_backend

    iv = seg_index.to_bytes(16, byteorder='big')
    # PKCS7 padding
    pad_len = 16 - (len(plaintext) % 16)
    plaintext += bytes([pad_len] * pad_len)

    cipher = Cipher(
        algorithms.AES(AES_KEY),
        modes.CBC(iv),
        backend=default_backend()
    )
    encryptor = cipher.encryptor()
    return encryptor.update(plaintext) + encryptor.finalize()


# ==================== 预生成分片缓存 ====================

_segment_cache: dict[int, bytes] = {}
_cache_lock = threading.Lock()

def get_segment(seg_index: int) -> bytes:
    with _cache_lock:
        if seg_index in _segment_cache:
            return _segment_cache[seg_index]

    # 尝试用 FFmpeg 生成
    with tempfile.NamedTemporaryFile(suffix=".ts", delete=False) as f:
        tmp_path = f.name

    ok = try_generate_ts_with_ffmpeg(seg_index, tmp_path)
    if ok and os.path.getsize(tmp_path) > 0:
        with open(tmp_path, "rb") as f:
            raw_ts = f.read()
        print(f"[server] seg{seg_index}: FFmpeg 生成 {len(raw_ts)} 字节 TS")
    else:
        raw_ts = generate_minimal_ts()
        print(f"[server] seg{seg_index}: 使用最小 TS ({len(raw_ts)} 字节，FFmpeg 不可用)")
    try:
        os.unlink(tmp_path)
    except Exception:
        pass

    # 加密
    encrypted = encrypt_ts(raw_ts, seg_index)
    with _cache_lock:
        _segment_cache[seg_index] = encrypted
    return encrypted


# ==================== m3u8 生成 ====================

def build_m3u8() -> str:
    lines = [
        "#EXTM3U",
        "#EXT-X-VERSION:3",
        f"#EXT-X-TARGETDURATION:{SEGMENT_DURATION}",
        "#EXT-X-MEDIA-SEQUENCE:0",
        # KEY URI 指向本服务器 /key 接口
        f'#EXT-X-KEY:METHOD=AES-128,URI="http://{HOST}:{PORT}/key",IV=0x00000000000000000000000000000000',
    ]
    for i in range(SEGMENT_COUNT):
        lines.append(f"#EXTINF:{SEGMENT_DURATION}.000,")
        lines.append(f"http://{HOST}:{PORT}/seg{i}.ts")
    lines.append("#EXT-X-ENDLIST")
    return "\n".join(lines) + "\n"


def build_m3u8_bad_key() -> str:
    """m3u8 使用错误 key 接口（返回非16字节），用于复现解密失败"""
    lines = [
        "#EXTM3U",
        "#EXT-X-VERSION:3",
        f"#EXT-X-TARGETDURATION:{SEGMENT_DURATION}",
        "#EXT-X-MEDIA-SEQUENCE:0",
        f'#EXT-X-KEY:METHOD=AES-128,URI="http://{HOST}:{PORT}/key_bad",IV=0x00000000000000000000000000000000',
    ]
    for i in range(SEGMENT_COUNT):
        lines.append(f"#EXTINF:{SEGMENT_DURATION}.000,")
        lines.append(f"http://{HOST}:{PORT}/seg{i}.ts")
    lines.append("#EXT-X-ENDLIST")
    return "\n".join(lines) + "\n"


# ==================== HTTP Handler ====================

class HLSHandler(BaseHTTPRequestHandler):
    # 请求计数（用于诊断）
    _request_log: list = []
    _log_lock = threading.Lock()

    def log_message(self, format, *args):
        # 替换默认日志，记录到列表供 /status 显示
        msg = f"[{time.strftime('%H:%M:%S')}] {self.path} → " + (format % args)
        print(msg)
        with HLSHandler._log_lock:
            HLSHandler._request_log.append(msg)
            if len(HLSHandler._request_log) > 200:
                HLSHandler._request_log = HLSHandler._request_log[-200:]

    def do_GET(self):
        path = self.path.split("?")[0]

        if path == "/stream.m3u8":
            self._serve_m3u8()
        elif path == "/stream_bad.m3u8":
            self._serve_m3u8_bad()
        elif path.startswith("/seg") and path.endswith(".ts"):
            self._serve_ts(path)
        elif path == "/key":
            self._serve_key_correct()
        elif path == "/key_bad":
            self._serve_key_bad()
        elif path == "/key_json":
            self._serve_key_json()
        elif path == "/status":
            self._serve_status()
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Not Found")

    def _headers_as_dict(self) -> dict:
        return dict(self.headers)

    def _serve_m3u8(self):
        body = build_m3u8().encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/vnd.apple.mpegurl")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _serve_m3u8_bad(self):
        body = build_m3u8_bad_key().encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/vnd.apple.mpegurl")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_ts(self, path: str):
        try:
            # 从路径提取序号：/seg0.ts → 0
            name = path.lstrip("/").removesuffix(".ts")  # "seg0"
            idx = int(name.replace("seg", ""))
        except ValueError:
            self.send_response(400)
            self.end_headers()
            return

        if idx < 0 or idx >= SEGMENT_COUNT:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"segment not found")
            return

        # 检查鉴权头（演示：仅打印，不强制拒绝）
        session = self.headers.get("X-Playback-Session", "")
        file_id = self.headers.get("X-File-Id", "")
        print(f"[server] seg{idx} 请求  session={session!r}  file_id={file_id!r}")

        data = get_segment(idx)
        self.send_response(200)
        self.send_header("Content-Type", "video/mp2t")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(data)

    def _serve_key_correct(self):
        """✅ 正确格式：16 字节原始二进制 key"""
        # 同时识别 X-* 和 P-HX-* 两种 header 前缀（Apple SDK 实际透传 P-HX-*，文档写的是 X-*）
        session  = self.headers.get("X-Playback-Session", "") or self.headers.get("P-HX-Session", "")
        file_id  = self.headers.get("X-File-Id", "")          or self.headers.get("P-HX-FileId", "")
        secret   = self.headers.get("X-Secret-Id", "")        or self.headers.get("P-HX-SecretID", "")
        sign     = self.headers.get("X-Sign", "")             or self.headers.get("P-HX-Sign", "")
        terminal = self.headers.get("P-HX-Terminal-Type", "")
        print(f"[server] /key 请求（正确）")
        print(f"  session={session!r}  file_id={file_id!r}")
        print(f"  secret={secret!r}  sign={sign!r}  terminal={terminal!r}")
        print(f"  → 返回 16 字节原始 key: {AES_KEY.hex()}")

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", "16")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(AES_KEY)

    def _serve_key_bad(self):
        """❌ 错误格式：Base64 字符串（复现解密失败）"""
        b64 = base64.b64encode(AES_KEY).decode()
        body = b64.encode("utf-8")
        print(f"[server] /key_bad 请求（Base64字符串，会导致解密失败）: {b64}")

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_key_json(self):
        """❌ 错误格式：JSON 包装（复现解密失败）"""
        body = json.dumps({"key": AES_KEY.hex(), "iv": "0" * 32}).encode()
        print(f"[server] /key_json 请求（JSON格式，会导致解密失败）")

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_status(self):
        with HLSHandler._log_lock:
            logs = list(HLSHandler._request_log[-30:])

        has_crypto = False
        try:
            from cryptography.hazmat.primitives.ciphers import Cipher
            has_crypto = True
        except ImportError:
            pass

        status = {
            "server": f"http://{HOST}:{PORT}",
            "urls": {
                "m3u8_correct": f"http://{HOST}:{PORT}/stream.m3u8",
                "m3u8_bad_key": f"http://{HOST}:{PORT}/stream_bad.m3u8",
                "key_correct": f"http://{HOST}:{PORT}/key",
                "key_bad_base64": f"http://{HOST}:{PORT}/key_bad",
                "key_bad_json": f"http://{HOST}:{PORT}/key_json",
            },
            "aes_key_hex": AES_KEY.hex(),
            "cryptography_installed": has_crypto,
            "segment_count": SEGMENT_COUNT,
            "header_prefix_note": (
                "Apple SDK 实际透传 P-HX-SecretID/P-HX-FileId/P-HX-Sign/P-HX-Terminal-Type，"
                "文档写的是 X-Secret-Id/X-File-Id/X-Sign。"
                "本服务器两种前缀都接受，生产后端须与 SDK 实际透传的前缀对齐。"
            ),
            "backend_response_note": (
                "Apple SDK 解析鉴权响应字段：play_url / encrypt_type / secure_headers。"
                "文档 HLS_AES128_BACKEND_API.md 写的是 m3u8_url/play_session_id，两者不一致，需确认后端实际字段名。"
            ),
            "recent_requests": logs,
        }
        body = json.dumps(status, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


# ==================== 入口 ====================

def check_deps():
    """检查依赖，给出安装提示"""
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher
        print("[OK] cryptography 已安装")
    except ImportError:
        print("[ERROR] 缺少 cryptography 库，请先运行：")
        print("        pip install cryptography")
        print("        或: py -m pip install cryptography")
        sys.exit(1)


def print_banner():
    key_hex = AES_KEY.hex()
    print("=" * 60)
    print("  HXCPlayer HLS AES-128 本地测试服务器")
    print("=" * 60)
    print(f"  地址       : http://{HOST}:{PORT}")
    print(f"  正确 m3u8  : http://{HOST}:{PORT}/stream.m3u8")
    print(f"  错误 m3u8  : http://{HOST}:{PORT}/stream_bad.m3u8  (key返回Base64)")
    print(f"  key 接口   : http://{HOST}:{PORT}/key  (16字节raw binary ✅)")
    print(f"  AES Key    : {key_hex}")
    print(f"  诊断页面   : http://{HOST}:{PORT}/status")
    print("=" * 60)
    print()
    print("  在播放器 SDK 中使用 SecureHLS 测试按钮，填入上方 m3u8 地址")
    print("  Header 示例：")
    print("    X-Playback-Session: ps_test_local_123")
    print("    X-File-Id: f_demo_001")
    print()
    print("  ⚠️  解密失败验证：改用 /stream_bad.m3u8 可复现 key 格式错误导致的失败")
    print()
    print("  按 Ctrl+C 停止服务器")
    print("=" * 60)


def main():
    global PORT
    parser = argparse.ArgumentParser(description="HLS AES-128 测试服务器")
    parser.add_argument("--port", type=int, default=PORT, help="监听端口 (默认 8765)")
    args = parser.parse_args()

    check_deps()

    PORT = args.port

    # 预生成第一个分片（触发 FFmpeg 检测）
    print("[server] 正在预生成测试分片...")
    try:
        get_segment(0)
        print("[server] 分片预生成完成")
    except Exception as e:
        print(f"[server] 分片预生成失败（将在请求时生成）: {e}")

    print_banner()

    server = HTTPServer((HOST, PORT), HLSHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[server] 服务器已停止")


if __name__ == "__main__":
    main()
