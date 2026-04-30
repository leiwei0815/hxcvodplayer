# HLS key 接口快速诊断脚本
# 用途：验证 key 接口是否返回正确的 16 字节原始二进制密钥
# 运行：右键 -> 用 PowerShell 运行，或在 PowerShell 中执行
#
# 这是 HLS AES-128 解密失败最常见的原因检测点

param(
    [string]$KeyUrl = "http://127.0.0.1:8765/key",
    [string]$Session = "ps_test_local_123",
    [string]$FileId  = "f_demo_001"
)

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  HXCPlayer HLS key 接口诊断工具" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  目标 URL : $KeyUrl"
Write-Host "  Headers  : X-Playback-Session=$Session"
Write-Host "             X-File-Id=$FileId"
Write-Host ""
Write-Host "⚠️  重要提示：" -ForegroundColor Yellow
Write-Host "  Apple SDK (HXCPlayerControl.mm) 实际透传的 header 前缀是 P-HX-*"
Write-Host "  例如：P-HX-SecretID / P-HX-FileId / P-HX-Sign / P-HX-Terminal-Type"
Write-Host "  文档 HLS_AES128_SDK_INTEGRATION.md 写的是 X-Secret-Id / X-File-Id / X-Sign"
Write-Host "  → 后端 key 接口必须认 P-HX-* 前缀，否则鉴权失败 → 拿不到 key → 解密失败！"
Write-Host ""

$urls = @(
    @{ Label = "正确（16字节binary）"; Url = $KeyUrl },
    @{ Label = "错误（Base64字符串）"; Url = $KeyUrl -replace "/key$", "/key_bad" },
    @{ Label = "错误（JSON包装）";     Url = $KeyUrl -replace "/key$", "/key_json" }
)

foreach ($item in $urls) {
    Write-Host "------------------------------------------------------------"
    Write-Host "  测试: $($item.Label)" -ForegroundColor Yellow
    Write-Host "  URL : $($item.Url)"

    try {
        # 同时带上 P-HX-* 和 X-* 两种前缀（模拟 Apple SDK 实际透传）
        $headers = @{
            "X-Playback-Session" = $Session
            "X-File-Id"          = $FileId
            "P-HX-SecretID"      = "sk_test_001"
            "P-HX-FileId"        = $FileId
            "P-HX-Sign"          = $Session
            "P-HX-Terminal-Type" = "iOS"
        }
        $resp = Invoke-WebRequest -Uri $item.Url -Headers $headers `
                    -Method GET -TimeoutSec 5 -ErrorAction Stop

        $len  = $resp.Content.Length
        $ct   = $resp.Headers["Content-Type"]
        $body = $resp.Content   # byte[]

        Write-Host "  HTTP状态  : $($resp.StatusCode)" -ForegroundColor Green
        Write-Host "  Content-Type : $ct"
        Write-Host "  响应字节数   : $len"

        if ($len -eq 16) {
            $hex = ($body | ForEach-Object { "{0:x2}" -f $_ }) -join ""
            Write-Host "  ✅ 正确！16字节 raw key: $hex" -ForegroundColor Green
            Write-Host "     FFmpeg 可以直接使用此 key 解密。" -ForegroundColor Green
        } else {
            # 尝试以 UTF-8 显示文本内容
            $text = [System.Text.Encoding]::UTF8.GetString($body)
            Write-Host "  ❌ 错误！字节数=$len（必须=16）" -ForegroundColor Red
            Write-Host "  响应内容（前100字符）: $($text.Substring(0, [Math]::Min(100, $text.Length)))" -ForegroundColor Red
            Write-Host ""
            Write-Host "  原因分析：" -ForegroundColor Red
            if ($text -match "^[A-Za-z0-9+/=]+$" -and $len -gt 16) {
                Write-Host "    → 看起来是 Base64 编码的密钥，服务端应直接返回 raw bytes" -ForegroundColor Red
            } elseif ($text.TrimStart().StartsWith("{")) {
                Write-Host "    → 看起来是 JSON 格式，服务端不能包装 JSON，直接返回 raw bytes" -ForegroundColor Red
            } else {
                Write-Host "    → 未知格式，请确认服务端返回 Content-Type: application/octet-stream" -ForegroundColor Red
            }
        }
    } catch {
        Write-Host "  ❌ 请求失败: $($_.Exception.Message)" -ForegroundColor Red
        if ($item.Url -match "127.0.0.1:8765") {
            Write-Host "     请先运行 start_test_server.bat 启动本地服务器" -ForegroundColor Yellow
        }
    }
    Write-Host ""
}

Write-Host "============================================================"
Write-Host ""
Write-Host "诊断要点：" -ForegroundColor Cyan
Write-Host "  1. key 接口必须返回 16 字节原始二进制（响应字节数=16）"
Write-Host "  2. Content-Type 建议 application/octet-stream"
Write-Host "  3. 不能返回 Base64 字符串、Hex 字符串、JSON 等格式"
Write-Host "  4. FFmpeg 请求 key 时携带 SDK 设置的 headers"
Write-Host "     Apple SDK 实际透传: P-HX-SecretID / P-HX-FileId / P-HX-Sign / P-HX-Terminal-Type"
Write-Host "     后端 key 接口必须认 P-HX-* 这套前缀！" -ForegroundColor Red
Write-Host "  5. 后端鉴权响应字段名：SDK 期望 play_url/encrypt_type，文档写 m3u8_url/play_session_id"
Write-Host "     → 需要和后端确认实际使用哪套字段名" -ForegroundColor Red
Write-Host "  6. IV 若未指定，FFmpeg 默认使用分片序号（0x00..01, 0x00..02 …）"
Write-Host ""
Write-Host "日志文件位置：%TEMP%\hxcplayer_logs\" -ForegroundColor Gray
Write-Host ""

Read-Host "按回车键退出"
