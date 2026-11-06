# rm_send.ps1 -- send one command to the car over a serial port and print the reply.
# Usage (ASCII only on purpose: Windows PowerShell 5 mis-parses non-ASCII .ps1):
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\rm_send.ps1 -Port COM5 -Cmd "p"
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\rm_send.ps1 -Port COM5 -Cmd "v 300 0 0"
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\rm_send.ps1 -Port COM5 -Cmd "cal" -WaitSec 3
param(
    [string]$Port = "COM5",
    [int]$Baud = 9600,
    [string]$Cmd = "p",
    [int]$WaitSec = 2,
    [switch]$Hex,
    [switch]$NoNewline
)

$sp = New-Object System.IO.Ports.SerialPort
$sp.PortName  = $Port
$sp.BaudRate  = $Baud
$sp.DataBits  = 8
$sp.Parity    = [System.IO.Ports.Parity]::None
$sp.StopBits  = [System.IO.Ports.StopBits]::One
$sp.ReadTimeout = 200
$sp.NewLine = "`r`n"

try {
    $sp.Open()
    Start-Sleep -Milliseconds 150
    $sp.DiscardInBuffer()

    if ($Hex) {
        $bytes = ($Cmd -split '\s+') | ForEach-Object { [Convert]::ToByte($_, 16) }
        $sp.Write([byte[]]$bytes, 0, $bytes.Count)
    } elseif ($NoNewline) {
        $sp.Write($Cmd)
    } else {
        $sp.WriteLine($Cmd)
    }

    Write-Host ("[tx] " + $Cmd)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $all = New-Object System.Collections.Generic.List[byte]
    while ($sw.Elapsed.TotalSeconds -lt $WaitSec) {
        try {
            $b = $sp.ReadByte()
            if ($b -ge 0) { $all.Add([byte]$b) }
        } catch {
            Start-Sleep -Milliseconds 30
        }
    }
    if ($all.Count -eq 0) {
        Write-Host "[rx] (nothing)"
        exit 2
    }
    if ($Hex) {
        Write-Host ("[rx] " + (($all | ForEach-Object { $_.ToString("X2") }) -join ' '))
    } else {
        Write-Host ("[rx] " + [System.Text.Encoding]::ASCII.GetString($all.ToArray()))
    }
    exit 0
} finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
}