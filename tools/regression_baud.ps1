<#
  频偏回归：生成带"发射端时钟偏差"的 APRS 音频，分别用软判决/单点硬判决跑 modem 回归。

  背景：真实链路（手机播放、SDR、声控线）的发射时钟与接收端不同源，偏差几百 ppm 到 ±2% 很常见。
  现有测试音频默认零频偏，只走 16 相位路径；只有带偏差时才走 TR（跳变对齐）路径，
  所以这张表是 TR 路径的看门狗（2026-09-17 的 TR 重对齐 bug 就是它抓出来的）。

  用法（在仓库根目录）：
    powershell -ExecutionPolicy Bypass -File tools\regression_baud.ps1
    powershell -ExecutionPolicy Bypass -File tools\regression_baud.ps1 -Bias "0,0.5,-0.5,2,-2"

  输出：每个偏差一行，含软判决/硬判决的总帧数与唯一包数，以及 TR 路径的贡献。
#>
param(
  [string]$Bias = "0,0.5,-0.5,1,-1,2,-2,2.5,-2.5,3,-3,-4"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$tcc  = Join-Path $root 'third_party\tcc\tcc\x86_64-win32-tcc.exe'
$py   = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $py) { throw '找不到 python（需要它生成测试音频）' }
$tmp  = Join-Path $env:TEMP 'bbcall_baud_reg'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$srcs = @(
  (Join-Path $root 'tools\regression_modem.c'),
  (Join-Path $root 'firmware-stm32porject\Core\Src\modem.c'),
  (Join-Path $root 'firmware-stm32porject\Core\Src\ax25.c')
)
$inc = '-I' + (Join-Path $root 'firmware-stm32porject\Core\Inc')

Write-Host '[reg] 编译回归工具（软判决 / 硬判决）'
& $tcc -o (Join-Path $tmp 'reg_soft.exe') @srcs $inc | Out-Null
& $tcc -DBBCALL_MODEM_SOFT_DECISION=0 -o (Join-Path $tmp 'reg_hard.exe') @srcs $inc | Out-Null

function Get-Result([string]$exe, [string]$wav) {
  $out = & $exe $wav 2>&1
  $hexes = $out | Where-Object { $_ -match 'hex=([0-9a-f]+)' } | ForEach-Object { $_ -replace '.*hex=', '' }
  $stat = ($out | Select-String 'STAT').ToString()
  $ph = 0; $tr = 0
  if ($stat -match 'PATH phase=(\d+) tr=(\d+)') { $ph = [int]$Matches[1]; $tr = [int]$Matches[2] }
  return [pscustomobject]@{
    Total = $hexes.Count
    Uniq  = ($hexes | Sort-Object -Unique).Count
    Phase = $ph
    Tr    = $tr
  }
}

Write-Host ''
Write-Host ('{0,-8} {1,-30} {2,-26} {3}' -f 'bias', 'soft (phase/TR)', 'hard', 'unique')
foreach ($b in ($Bias -split ',')) {
  $b = $b.Trim()
  if ($b -eq '') { continue }
  $tag = $b -replace '-', 'm' -replace '\.', 'p'
  $wav = Join-Path $tmp ("bias_{0}.wav" -f $tag)
  & $py.Source (Join-Path $root 'tools\gen_afsk_wav.py') --src BG5BLB-12 --dest APRS --pos `
      --lat 39.8748 --lon 120.4758 --comment "有内鬼 停止交易" --seed 20260917 `
      --baud-bias $b -o $wav | Out-Null

  $s = Get-Result (Join-Path $tmp 'reg_soft.exe') $wav
  $h = Get-Result (Join-Path $tmp 'reg_hard.exe') $wav
  $verdict = ''
  if ($s.Uniq -lt $h.Uniq) { $verdict = '  <== soft worse, roll back' }
  elseif ($s.Uniq -gt $h.Uniq) { $verdict = '  <== soft better' }
  Write-Host ('{0,-8} total={1,-4} (phase{2,-3} TR{3,-3}) total={4,-4} (TR{5,-3}) soft={6} hard={7}{8}' -f `
      $b, $s.Total, $s.Phase, $s.Tr, $h.Total, $h.Tr, $s.Uniq, $h.Uniq, $verdict)
}
Write-Host ''
Write-Host '[reg] unique = hard metric; total includes multi-phase duplicates, reference only.'