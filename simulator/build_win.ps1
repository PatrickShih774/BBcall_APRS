<#
  BBcall_APRS PC LCD 模拟器 - Windows 免安装构建脚本
  使用仓库自带 TinyCC + 内置 SDL2，不需要 MSVC / MinGW / CMake。

  用法:
    powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1
    powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1 -Selftest
    powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1 -Run
#>
[CmdletBinding()]
param(
  [switch]$Run,
  [switch]$Selftest,
  [ValidateRange(1,12)][int]$Scale = 4
)

$ErrorActionPreference = 'Stop'

$sim  = $PSScriptRoot
$root = Split-Path -Parent $sim
$fw   = Join-Path $root 'firmware-stm32porject\Core'
$out  = Join-Path $sim 'build-win'
$tcc64 = Join-Path $root 'third_party\tcc\tcc\x86_64-win32-tcc.exe'
$tcc32 = Join-Path $root 'third_party\tcc\tcc\i386-win32-tcc.exe'
if (Test-Path $tcc64) {
  $tcc = $tcc64
} elseif (Test-Path $tcc32) {
  # 32 位 TCC（官方 0.9.27 发行版）+ 32 位 SDL2，同样免安装
  $tcc = $tcc32
  $cands = @()
  if ($env:SDL2_DIR) { $cands += $env:SDL2_DIR }
  $cands += (Join-Path $root 'third_party\sdl2-32')
  $sdl = $cands | Where-Object {
    $_ -and (Test-Path (Join-Path $_ 'include\SDL2\SDL.h')) -and (Test-Path (Join-Path $_ 'bin\SDL2.dll'))
  } | Select-Object -First 1
  Write-Host '[sim] TCC  : 32-bit build (i386-win32-tcc + SDL2 i686)'
} else {
  throw "找不到 TinyCC: $tcc64 或 $tcc32"
}

# --- 定位 SDL2 开发库（需要 include/SDL2/SDL.h 与 bin/SDL2.dll）---
if (-not $sdl) {
  $cands = @()
  if ($env:SDL2_DIR) { $cands += $env:SDL2_DIR }
  $cands += (Join-Path $root 'third_party\sdl2')
  $cands += @(Get-ChildItem "$env:TEMP\bbcall_sim_tools\sdl2\SDL2-*\x86_64-w64-mingw32" -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
  $sdl = $cands | Where-Object {
    $_ -and (Test-Path (Join-Path $_ 'include\SDL2\SDL.h')) -and (Test-Path (Join-Path $_ 'bin\SDL2.dll'))
  } | Select-Object -First 1
}

if (-not $sdl) {
  throw ("找不到 SDL2。请下载 SDL2-devel-*-mingw.tar.gz 解压到 third_party\sdl2\，或设置环境变量 SDL2_DIR。`n" +
         "下载: https://github.com/libsdl-org/SDL/releases")
}

New-Item -ItemType Directory -Force -Path $out | Out-Null

$srcs = @(
  (Join-Path $sim 'src\main.c'),
  (Join-Path $sim 'src\sim_hal.c'),
  (Join-Path $sim 'src\lcd_sim.c'),
  (Join-Path $sim 'src\sim_feed.c'),
  (Join-Path $fw  'Src\strfmt.c'),
  (Join-Path $fw  'Src\ui_harness.c'),
  (Join-Path $fw  'Src\lcd_st7567.c'),
  (Join-Path $fw  'Src\ax25.c'),
  (Join-Path $fw  'Src\aprs.c'),
  (Join-Path $fw  'Src\modem.c')
)

# TCC (x86_64) 不认识 __cdecl 关键字，而 SDL 头文件里会用到，这里预定义为空。
$tccArgs = @(
  '-D__cdecl=',
  '-DLCD_SIM=1',
  "-I$(Join-Path $sim 'src')",
  "-I$(Join-Path $fw  'Inc')",
  "-I$(Join-Path $sdl 'include\SDL2')",
  '-o', (Join-Path $out 'bbcall_sim.exe')
) + $srcs + @( (Join-Path $sdl 'bin\SDL2.dll') )

Write-Host "[sim] TCC  : $tcc"
Write-Host "[sim] SDL2 : $sdl"

& $tcc @tccArgs
if ($LASTEXITCODE -ne 0) { throw "TinyCC 编译失败 (exit $LASTEXITCODE)" }

Copy-Item (Join-Path $sdl 'bin\SDL2.dll') $out -Force
Write-Host "[sim] OK -> $(Join-Path $out 'bbcall_sim.exe')"

if ($Selftest) {
  # 固定时钟/RTC/呼号/电量，保证自检截图可复现（14:26:00 / 周三 9/16 / BG5BLH / 电量高）
  Push-Location $out
  & .\bbcall_sim.exe --selftest --clock 51960 --wallclock 3,9,16 --mycall BG5BLH --batt 2 --demo
  $rc = $LASTEXITCODE
  Pop-Location
  if ($rc -ne 0) { throw "selftest 失败 (exit $rc)" }
}

if ($Run) {
  Start-Process -FilePath (Join-Path $out 'bbcall_sim.exe') -ArgumentList '--scale', $Scale -WorkingDirectory $out
  Write-Host "[sim] 已启动窗口 (scale=$Scale)，Esc 退出"
}