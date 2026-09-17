<#
  BBcall_APRS RTC 对时：把 PC 当前系统时间写进 STM32 片内 RTC（串口 USART3, 115200 8N1）。

  用法（仓库根目录）:
    powershell -ExecutionPolicy Bypass -File tools\set_rtc_time.ps1              # 自动找串口并对时
    powershell -ExecutionPolicy Bypass -File tools\set_rtc_time.ps1 -Port COM5   # 指定串口
    powershell -ExecutionPolicy Bypass -File tools\set_rtc_time.ps1 -Query       # 只回读设备时间
    powershell -ExecutionPolicy Bypass -File tools\set_rtc_time.ps1 -List        # 列出可用串口
    powershell -ExecutionPolicy Bypass -File tools\set_rtc_time.ps1 -DryRun      # 只打印要发的命令

  说明:
  - 串口助手（SSCOM 等）必须先关掉，否则串口被占用打不开；
  - 设备回复形如 [RTC] set 2026-09-18 22:30:00 Fri src=HSE/128；
  - 掉电（没有 VBAT 电池）后设备时间会丢，重新上电再跑一次本脚本即可。
#>
[CmdletBinding()]
param(
  [string]$Port,
  [switch]$Query,
  [switch]$List,
  [switch]$DryRun,
  [int]$Baud = 115200,
  [int]$TimeoutMs = 2500
)

$ErrorActionPreference = 'Stop'

function Open-Console([string]$name) {
  $sp = New-Object System.IO.Ports.SerialPort($name, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
  $sp.ReadTimeout = 250
  $sp.WriteTimeout = 500
  $sp.Open()
  Start-Sleep -Milliseconds 80
  $sp.DiscardInBuffer()
  return $sp
}

function Send-Line($sp, [string]$line, [int]$waitMs, [switch]$UntilRtc) {
  $sp.DiscardInBuffer()
  $sp.Write($line + "`r`n")
  $sb = New-Object System.Text.StringBuilder
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $waitMs) {
    $chunk = ''
    try { $chunk = $sp.ReadExisting() } catch { $chunk = '' }
    if ($chunk) {
      [void]$sb.Append($chunk)
      if ($UntilRtc -and ($sb.ToString() -match '\[RTC\]')) { break }
    }
    Start-Sleep -Milliseconds 30
  }
  return $sb.ToString()
}

function Get-RtcLines([string]$text) {
  $hit = @($text -split "`r?`n" | Where-Object { $_ -match '\[RTC\]' })
  if ($hit.Count -eq 0) { return '' }
  return ($hit -join "`n").Trim()
}

$line = 'TIME=' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
if ($DryRun) {
  Write-Host "会发送: $line"
  exit 0
}

$ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
if ($List) {
  if ($ports.Count -eq 0) { Write-Host '没有可用串口（USB-TTL 驱动装了吗？）'; exit 1 }
  Write-Host ("可用串口: " + ($ports -join ', '))
  exit 0
}
if ($ports.Count -eq 0) {
  Write-Host '没有可用串口：检查 USB-TTL 是否插好、驱动是否装好。' -ForegroundColor Red
  exit 1
}

$cands = if ($Port) { @($Port) } else { $ports }
$spDev = $null
$portName = ''
foreach ($c in $cands) {
  try {
    $sp = Open-Console $c
  } catch {
    Write-Host "[$c] 打不开（被串口助手占用？）: $($_.Exception.Message)" -ForegroundColor Yellow
    continue
  }
  if ($Port) { $spDev = $sp; $portName = $c; break }
  $resp = Send-Line $sp 'TIME?' 800 -UntilRtc
  if ($resp -match '\[RTC\]') { $spDev = $sp; $portName = $c; break }
  Write-Host "[$c] 没有 [RTC] 回复，跳过"
  $sp.Close()
}

if (-not $spDev) {
  Write-Host '没找到 BBcall_APRS 设备：确认串口线接的是 PB10(TX)/PB11(RX)、固件是带 RTC 对时的版本。' -ForegroundColor Red
  exit 3
}

$exitCode = 0
try {
  if ($Query) {
    $out = Get-RtcLines (Send-Line $spDev 'TIME?' 1500 -UntilRtc)
    if ($out) { Write-Host "[$portName] $out" } else { Write-Host "[$portName] 没有回读" -ForegroundColor Red; $exitCode = 3 }
  } else {
    Write-Host "[$portName] 发送: $line"
    $out = Get-RtcLines (Send-Line $spDev $line 2000 -UntilRtc)
    if ($out) { Write-Host "[$portName] $out" } else { Write-Host "[$portName] 没有回复" -ForegroundColor Red; $exitCode = 3 }
    if ($out -match '\[RTC\] err') { $exitCode = 2 }
    $chk = Get-RtcLines (Send-Line $spDev 'TIME?' 1500 -UntilRtc)
    if ($chk) { Write-Host "[$portName] 回读: $chk" }
  }
} finally {
  $spDev.Close()
}
exit $exitCode