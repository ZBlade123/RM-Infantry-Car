# 把本目录推到 GitHub（本机 hosts 把 github.com 指向 127.0.0.1，所以先起本地隧道）
# 用法：powershell -ExecutionPolicy Bypass -File .\gh_push.ps1 -Message "改了什么"
#       首次：加 -Init；认证由 Git Credential Manager 或 SSH 提供
param(
    [string]$Message = "update",
    [switch]$Init,
    [string]$Repo = "ZBlade123/RM-Infantry-Car",
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tunnel = Join-Path $PSScriptRoot 'gh_tunnel.py'
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { $python = (Get-Command py -ErrorAction SilentlyContinue).Source }
if (-not $python) { throw '找不到 Python，请先安装 Python 或手动启动 GitHub 隧道' }

function Test-Tunnel {
    try { $c = New-Object Net.Sockets.TcpClient; $c.Connect('127.0.0.1', 443); $c.Close(); return $true }
    catch { return $false }
}

if (-not (Test-Tunnel)) {
    Start-Process -FilePath $python -ArgumentList $tunnel -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root '_tunnel.log') -RedirectStandardError (Join-Path $root '_tunnel.err')
    Start-Sleep 2
    if (-not (Test-Tunnel)) { throw "隧道没起来，看 _tunnel.err" }
    Write-Host "隧道已启动 127.0.0.1:443 -> github.com"
}

Push-Location $root
try {
    if ($Init) {
        if (-not (Test-Path '.git')) { git init -b main }
        git config core.autocrlf true
        git remote remove origin 2>$null
        git remote add origin "https://github.com/$Repo.git"
    }

    git add -A
    $changed = git status --porcelain
    if ($changed) { git commit -q -m $Message; Write-Host "已提交：$Message" } else { Write-Host "没有改动，只推送" }

    git push -u origin main
    Write-Host "完成：https://github.com/$Repo"
}
finally {
    Pop-Location
}
