﻿<#
  BBcall_APRS 串口桥（供 Codex/脚本 直接对话设备用）

  作用：常驻打开串口，把设备输出实时打印到标准输出；同时监听"命令文件"——
        文件一出现就把里面的每一行作为命令发到串口，然后删除文件。

  用法:
    powershell -ExecutionPolicy Bypass -File tools\serial_bridge.ps1 -Port COM5
    # 另开一个进程/窗口发命令（示例）:
    "STAT?" | Out-File -Encoding ascii "$env:TEMP\bbcall_tx.txt"
  参数:
    -Port COM5          串口（默认 COM5）
    -Baud 115200        波特率
    -CmdFile <路径>     命令文件（默认 %TEMP%\bbcall_tx.txt）
    -LogFile <路径>     可选：把设备输出同时落盘
    -DurationSec 0      0 = 一直跑，>0 = 跑 N 秒后自动退出

  注意：串口是独占的 —— 运行时请关掉串口助手（SSCOM 等）。
#>
[CmdletBinding()]
param(
  [string]$Port = 'COM5',
  [int]$Baud = 115200,
  [string]$CmdFile = (Join-Path $env:TEMP 'bbcall_tx.txt'),
  [string]$LogFile = '',
  [int]$DurationSec = 0
)

$ErrorActionPreference = 'Stop'

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 50
$sp.WriteTimeout = 1000
$sp.Open()
$sp.DiscardInBuffer()

$log = $null
if ($LogFile) { $log = New-Object System.IO.StreamWriter($LogFile, $true) }

Write-Host ("[bridge] {0} @ {1} 已打开；命令文件: {2}" -f $Port, $Baud, $CmdFile) -ForegroundColor Cyan
Write-Host "[bridge] 把命令写进该文件即发送（每行一条，发完自动删除）；Ctrl+C 退出" -ForegroundColor Cyan

$sw = [System.Diagnostics.Stopwatch]::StartNew()
try {
  while ($true) {
    if ($DurationSec -gt 0 -and $sw.Elapsed.TotalSeconds -ge $DurationSec) { break }

    $chunk = ''
    try { $chunk = $sp.ReadExisting() } catch { $chunk = '' }
    if ($chunk) {
      [Console]::Out.Write($chunk)
      [Console]::Out.Flush()
      if ($log) { $log.Write($chunk); $log.Flush() }
    }

    if (Test-Path -LiteralPath $CmdFile) {
      $raw = ''
      try { $raw = Get-Content -LiteralPath $CmdFile -Raw -ErrorAction Stop } catch { $raw = '' }
      Remove-Item -LiteralPath $CmdFile -Force -ErrorAction SilentlyContinue
      foreach ($line in ($raw -split "`r?`n")) {
        $cmd = $line.Trim()
        if ($cmd.Length -eq 0) { continue }
        [Console]::Out.Write(">>> TX: $cmd`n")
        [Console]::Out.Flush()
        $sp.Write($cmd + "`r`n")
        # 实测：一次性连发多条时，设备端偶尔会把第一条读坏（DMA 环 + 阻塞打印）；
        # 每条之间留 200ms，等设备把上一条解析/回复完再收下一条。
        Start-Sleep -Milliseconds 200
      }
    }

    Start-Sleep -Milliseconds 40
  }
} finally {
  if ($log) { $log.Close() }
  $sp.Close()
  Write-Host "`n[bridge] $Port 已关闭" -ForegroundColor Cyan
}