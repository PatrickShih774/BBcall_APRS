#!/usr/bin/env pwsh
# 在主机编译并运行 AX.25/APRS 解码测试（不依赖 HAL）。
# 优先用工程内 third_party/tcc，否则用 PATH 里的 gcc。

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$inc  = Join-Path $root "firmware\Inc"
$src  = Join-Path $root "firmware\Src"
$test = Join-Path $root "firmware\tests"
$out  = Join-Path $env:TEMP "check_ax25.exe"

$tcc = Join-Path $root "third_party\tcc\tcc\tcc.exe"

Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue

if (Test-Path $tcc) {
    & $tcc -I $inc (Join-Path $test "host_test_ax25.c") (Join-Path $src "ax25.c") (Join-Path $src "aprs.c") -o $out
} else {
    # 回退到系统 gcc（部分环境有）
    & gcc -I $inc (Join-Path $test "host_test_ax25.c") (Join-Path $src "ax25.c") (Join-Path $src "aprs.c") -o $out
}

Write-Host "=== 运行 ==="
& $out
Write-Host "exit=$LASTEXITCODE"
