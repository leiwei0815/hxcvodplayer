# 删除指定 GitHub 仓库的全部 Actions Artifacts，释放 Artifact 存储配额
# 用法:
#   $env:GITHUB_TOKEN = "你的PAT"
#   .\delete-github-artifacts.ps1 -Owner leiwei0815 -Repo hxcvodplayer
#
# PAT 要求（Fine-grained）:
#   - Repository access: 勾选目标仓库 hxcvodplayer
#   - Permissions → Actions: Read and write
# 或 Classic PAT: 勾选 repo

param(
    [string]$Owner = "leiwei0815",
    [string]$Repo = "hxcvodplayer",
    [string]$Token = $env:GITHUB_TOKEN
)

if ([string]::IsNullOrWhiteSpace($Token)) {
    Write-Error "请设置环境变量 GITHUB_TOKEN，或通过 -Token 传入 PAT"
    exit 1
}

$repoFull = "$Owner/$Repo"
$headers = @{
    Authorization            = "Bearer $Token"
    Accept                   = "application/vnd.github+json"
    "X-GitHub-Api-Version"   = "2022-11-28"
}

Write-Host "检查仓库访问: $repoFull"
try {
    $repoInfo = Invoke-RestMethod -Uri "https://api.github.com/repos/$repoFull" -Headers $headers
    Write-Host "OK: $($repoInfo.full_name) (private=$($repoInfo.private))"
} catch {
    Write-Error @"
无法访问仓库 $repoFull。常见原因:
  1) Fine-grained PAT 未勾选该仓库 → GitHub Settings → Developer settings → 编辑 Token → Repository access 添加 $Repo
  2) 未授予 Actions: Read and write 权限
  3) 仓库名/所有者错误
原始错误: $($_.Exception.Message)
"@
    exit 1
}

$all = @()
$page = 1
do {
    $uri = "https://api.github.com/repos/$repoFull/actions/artifacts?per_page=100&page=$page"
    $resp = Invoke-RestMethod -Uri $uri -Headers $headers
    if ($resp.artifacts) { $all += $resp.artifacts }
    $page++
} while ($resp.artifacts.Count -eq 100)

Write-Host "共找到 $($all.Count) 个 artifact"
if ($all.Count -eq 0) {
    Write-Host "无需删除。若 CI 仍报 quota，请等 6-12 小时配额重算，或在 Billing 页查看其它仓库占用。"
    exit 0
}

$deleted = 0
$bytes = 0L
foreach ($a in $all) {
    $delUri = "https://api.github.com/repos/$repoFull/actions/artifacts/$($a.id)"
    try {
        Invoke-RestMethod -Uri $delUri -Headers $headers -Method Delete | Out-Null
        $deleted++
        $bytes += $a.size_in_bytes
        $mb = [math]::Round($a.size_in_bytes / 1MB, 2)
        Write-Host "已删除: $($a.name) id=$($a.id) ${mb}MB ($($a.created_at))"
    } catch {
        Write-Warning "删除失败 id=$($a.id): $($_.Exception.Message)"
    }
    Start-Sleep -Milliseconds 150
}

Write-Host "完成: 删除 $deleted 个 artifact, 约 $([math]::Round($bytes/1MB,2)) MB"

# Actions Cache 也计入 Storage 配额（常见占满原因，比 artifact 更大）
Write-Host "清理 Actions Cache..."
$cacheDeleted = 0
$cacheBytes = 0L
$page = 1
do {
    $cresp = Invoke-RestMethod -Uri "https://api.github.com/repos/$repoFull/actions/caches?per_page=100&page=$page" -Headers $headers
    $caches = $cresp.actions_caches
    if (-not $caches) { break }
    foreach ($c in $caches) {
        $q = "https://api.github.com/repos/$repoFull/actions/caches?key=$([uri]::EscapeDataString($c.key))"
        if ($c.ref) { $q += "&ref=$([uri]::EscapeDataString($c.ref))" }
        try {
            Invoke-RestMethod -Uri $q -Headers $headers -Method Delete | Out-Null
            $cacheDeleted++
            $cacheBytes += $c.size_in_bytes
            Write-Host "已删除 cache: $($c.key) $([math]::Round($c.size_in_bytes/1MB,2)) MB"
        } catch {
            Write-Warning "cache 删除失败 $($c.key): $($_.Exception.Message)"
        }
        Start-Sleep -Milliseconds 100
    }
    $page++
} while ($caches.Count -eq 100)

Write-Host "Cache: 删除 $cacheDeleted 个, 约 $([math]::Round($cacheBytes/1MB,2)) MB"
Write-Host "配额更新可能有数小时延迟；artifact + cache 清空后请重新跑 Actions。"
