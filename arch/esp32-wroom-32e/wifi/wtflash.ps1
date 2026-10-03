param([int]$Wait = 6000, [switch]$NoFlash)
# WiFi 実験(rocky9 ~/tmp/20261002/wt)の像を実機へ書いて、起動ログを読む
$s = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = "\\rocky9\tk\tmp\20261002\wt"
$e = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"
if (-not $NoFlash) {
    foreach ($f in 'wt.img','drom.bin','irom.bin') { Copy-Item "$src\$f" "$s\wt_$f" -Force }
    $a = @('--chip','esp32','-p','COM4','-b','921600','write-flash','--flash-mode','dio','--flash-freq','40m','--flash-size','4MB','0x1000',"$s\wt_wt.img")
    if ((Get-Item "$s\wt_drom.bin").Length -gt 0) { $a += @('0x40000', "$s\wt_drom.bin") }
    if ((Get-Item "$s\wt_irom.bin").Length -gt 0) { $a += @('0x70000', "$s\wt_irom.bin") }
    & $e @a 2>&1 | Select-String -Pattern 'Wrote|error|Error|fatal' | ForEach-Object { $_.Line }
}
Add-Type -AssemblyName System
$sp = [System.IO.Ports.SerialPort]::new("COM4", 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.Open()
$sp.DtrEnable = $false; $sp.RtsEnable = $true; Start-Sleep -Milliseconds 120; $sp.RtsEnable = $false
$out = New-Object System.Text.StringBuilder
$t = [Environment]::TickCount
while ([Environment]::TickCount - $t -lt $Wait) {
    try { $x = $sp.ReadExisting(); if ($x) { [void]$out.Append($x) } } catch {}
    Start-Sleep -Milliseconds 50
}
$sp.Close()
$out.ToString() -replace "`r", ""
