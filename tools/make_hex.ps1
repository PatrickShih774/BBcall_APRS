# 将 STM32CubeIDE 生成的 ELF 转成 Intel HEX / BIN
# 用法：
#   powershell -File tools\make_hex.ps1
#   powershell -File tools\make_hex.ps1 -Elf firmware-stm32porject\Release\BBCall_APRS.elf
param(
    [string]$Elf = "$PSScriptRoot\..\firmware-stm32porject\Debug\BBCall_APRS.elf"
)

if (-not (Test-Path $Elf)) {
    Write-Error "找不到 ELF：$Elf（请先在 CubeIDE 里 Build）"
    exit 1
}
$Elf = (Resolve-Path $Elf).Path

$objcopy = (Get-Command arm-none-eabi-objcopy -ErrorAction SilentlyContinue).Source
if (-not $objcopy) {
    $objcopy = (Get-ChildItem 'C:\ST' -Recurse -Filter arm-none-eabi-objcopy.exe -ErrorAction SilentlyContinue |
                Select-Object -First 1 -ExpandProperty FullName)
}
if (-not $objcopy) {
    Write-Error "找不到 arm-none-eabi-objcopy（请确认已安装 STM32CubeIDE）"
    exit 1
}

$base = [IO.Path]::ChangeExtension($Elf, $null)
& $objcopy -O ihex   $Elf "$base.hex"
& $objcopy -O binary $Elf "$base.bin"

Write-Host "objcopy : $objcopy"
Write-Host "HEX     : $base.hex"
Write-Host "BIN     : $base.bin"
Get-Item "$base.hex", "$base.bin" | Select-Object FullName, Length
