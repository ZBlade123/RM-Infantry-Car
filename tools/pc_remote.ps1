<#
  pc_remote.ps1 —— 电脑键盘开车（蓝牙串口 → RM 步兵战车固件文本指令）
  零依赖：只用 Windows 自带 .NET SerialPort + user32.dll 读按键状态。

  用法：
    powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -List
    powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -Port COM7
    powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -Probe    # 不知道哪个 COM 能通时用
    powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -Dry      # 只打印不发

  键位：W/S 前后  A/D 横移  Q/E 自转  Shift 加速  方向键 云台  空格 三连发
        X 急停  R 回中  G 状态  T 遥测  Esc 退出（退出自动急停）
#>
param(
    [string]$Port = '',
    [int]$Baud = 9600,
    [int]$Hz = 20,
    [switch]$Dry,
    [switch]$List,
    [switch]$Probe,         # 挨个串口问一句 p，看哪根能通
    [int]$Seconds = 0          # 0 = 一直跑；测试时可以给 3
)

$ErrorActionPreference = 'Stop'

function Get-PortInfo {
    $out = @()
    foreach ($n in [System.IO.Ports.SerialPort]::GetPortNames()) {
        $friendly = ''
        try {
            $friendly = (Get-CimInstance Win32_SerialPort -Filter "DeviceID='$n'" -ErrorAction SilentlyContinue).Name
        } catch { }
        $out += [pscustomobject]@{ Port = $n; Name = $friendly }
    }
    return $out
}

if ($List) {
    $info = Get-PortInfo
    if (-not $info) { "没有找到任何串口。先配对 HC-05（PIN 1234），再回来看设备管理器。" }
    foreach ($i in $info) { "{0,-8} {1}" -f $i.Port, $i.Name }
    "提示：用名字带【传出 / Outgoing】的那个 COM。"
    return
}

if ($Probe) {
    $info = Get-PortInfo
    if (-not $info) { "没有找到任何串口。先配对 HC-05（PIN 1234）。"; return }
    "挨个问一遍（每个串口发 p，等 400ms）..."
    foreach ($i in $info) {
        $p = $i.Port
        try {
            $t = New-Object System.IO.Ports.SerialPort $p, $Baud, 'None', 8, 'One'
            $t.NewLine = "`r`n"; $t.ReadTimeout = 300; $t.WriteTimeout = 500; $t.DtrEnable = $true
            $t.Open(); Start-Sleep -Milliseconds 200
            $t.DiscardInBuffer()
            $t.WriteLine('p')
            Start-Sleep -Milliseconds 400
            $got = ''
            while ($t.BytesToRead -gt 0) { $got += $t.ReadLine().Trim() + ' | ' }
            $t.Close()
            if ($got) { "{0,-8} << {1}  [能通，就用它]" -f $p, $got }
            else      { "{0,-8} (没回应)" -f $p }
        } catch { "{0,-8} 打不开：{1}" -f $p, $_.Exception.Message }
    }
    return
}

if (-not $Port -and -not $Dry) {
    $info = Get-PortInfo
    "没有指定 -Port。当前串口："
    foreach ($i in $info) { "{0,-8} {1}" -f $i.Port, $i.Name }
    "用法：-Port COM7   （想只看不发：-Dry）"
    return
}

# ---- 按键状态（异步，能检测"按住"） ----
if (-not ('Kb' -as [type])) {
    Add-Type -Namespace Win -Name Kb -MemberDefinition @'
[DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vKey);
'@
}
function Get-Key([int]$vk) { return (( [Win.Kb]::GetAsyncKeyState($vk) -band 0x8000) -ne 0) }

$VK = @{
    W = 0x57; S = 0x53; A = 0x41; D = 0x44; Q = 0x51; E = 0x45
    Shift = 0x10; Left = 0x25; Right = 0x27; Up = 0x26; Down = 0x28
    Space = 0x20; X = 0x58; R = 0x52; G = 0x47; T = 0x54; Esc = 0x1B
}

$sp = $null
if (-not $Dry) {
    $sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
    $sp.NewLine = "`r`n"
    $sp.ReadTimeout = 50
    $sp.WriteTimeout = 500
    $sp.DtrEnable = $true
    $sp.Open()
}

function Send-Line([string]$line) {
    if ($Dry) { ">> $line"; return }
    try {
        $sp.WriteLine($line)
    } catch {
        "发送失败：$($_.Exception.Message)"
    }
}

function Read-Replies {
    if ($Dry -or $null -eq $sp) { return }
    try {
        while ($sp.BytesToRead -gt 0) {
            $s = $sp.ReadLine()
            if ($s) { "  << $($s.Trim())" }
        }
    } catch { }
}

# ---- 云台角度缓存（方向键按一次 ±2°） ----
$yaw10 = 900
$pitch10 = 900
$telem = 0

$lastV = ''
$lastFire = 0
$lastYawKey = 0
$lastPitchKey = 0
$running = $true

if ($Dry) { "演练模式（不发数据，只看按键映射）。Esc 退出。" } else { "串口 $Port 已打开（$Baud）。W/S/A/D 开车，Esc 退出。" }
Send-Line 'p'
Start-Sleep -Milliseconds 300
Read-Replies

$stopAt = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { $null }
$period = [int](1000 / [Math]::Max(1, $Hz))

while ($running) {
    $t0 = Get-Date

    if (Get-Key $VK.Esc) { break }

    # ---- 底盘 ----
    $speed = if (Get-Key $VK.Shift) { 600 } else { 300 }
    $s = 0; $vy = 0; $wz = 0
    if (Get-Key $VK.W) { $s += $speed }
    if (Get-Key $VK.S) { $s -= $speed }
    if (Get-Key $VK.A) { $vy += $speed }
    if (Get-Key $VK.D) { $vy -= $speed }
    if (Get-Key $VK.Q) { $wz += $speed }
    if (Get-Key $VK.E) { $wz -= $speed }

    $vLine = "v $s $vy $wz"
    if ($vLine -ne $lastV) {
        Send-Line $vLine
        $lastV = $vLine
    }

    # ---- 云台（每 150ms 最多走一步，避免一次按出 20°） ----
    $now = [Environment]::TickCount
    if ((Get-Key $VK.Left) -and ($now - $lastYawKey -gt 150)) {
        $yaw10 = [Math]::Min(1800, $yaw10 + 20); Send-Line "y $yaw10"; $lastYawKey = $now
    }
    if ((Get-Key $VK.Right) -and ($now - $lastYawKey -gt 150)) {
        $yaw10 = [Math]::Max(0, $yaw10 - 20); Send-Line "y $yaw10"; $lastYawKey = $now
    }
    if ((Get-Key $VK.Up) -and ($now - $lastPitchKey -gt 150)) {
        $pitch10 = [Math]::Min(1800, $pitch10 + 20); Send-Line "s $pitch10"; $lastPitchKey = $now
    }
    if ((Get-Key $VK.Down) -and ($now - $lastPitchKey -gt 150)) {
        $pitch10 = [Math]::Max(200, $pitch10 - 20); Send-Line "s $pitch10"; $lastPitchKey = $now
    }

    # ---- 单次动作（边沿触发） ----
    if ((Get-Key $VK.Space) -and ($now - $lastFire -gt 500)) {
        Send-Line 'f 3'; $lastFire = $now
    }
    if (Get-Key $VK.X) {
        Send-Line 'v 0 0 0'; Send-Line 'f 0'; $lastV = 'v 0 0 0'
        Start-Sleep -Milliseconds 250
    }
    if (Get-Key $VK.R) {
        $yaw10 = 900; $pitch10 = 900
        Send-Line 'y 900'; Send-Line 's 900'
        Start-Sleep -Milliseconds 250
    }
    if (Get-Key $VK.G) { Send-Line 'g'; Start-Sleep -Milliseconds 250 }
    if (Get-Key $VK.T) {
        $telem = 1 - $telem
        Send-Line "t $telem"
        "遥测 $(if ($telem) {'开'} else {'关'})"
        Start-Sleep -Milliseconds 250
    }

    Read-Replies

    if ($stopAt -and ((Get-Date) -gt $stopAt)) { $running = $false }

    $spent = ((Get-Date) - $t0).TotalMilliseconds
    $wait = [int]($period - $spent)
    if ($wait -gt 0) { Start-Sleep -Milliseconds $wait }
}

# ---- 退出必停 ----
Send-Line 'v 0 0 0'
Send-Line 'f 0'
if (-not $Dry -and $null -ne $sp) {
    Start-Sleep -Milliseconds 100
    $sp.Close()
}
"已退出，已发急停（v 0 0 0 / f 0）。"