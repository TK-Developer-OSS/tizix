param([Parameter(Mandatory=$true)][string]$Ssid, [Parameter(Mandatory=$true)][string]$Psk, [switch]$Clear)
# WiFi 実験の接続試験。flash 0x3F000 に "ssid=…\npsk=…\n" を書いて(-Clear なら消して)起動し、ログを出す。
#   使い方: powershell -ExecutionPolicy Bypass -File wtwifi.ps1 -Ssid "ネットワーク名" -Psk "パスワード"
#   実験像(wt)を書いて起動する(tizix のカーネルは上書きされる。戻すときは同じフォルダの README の手順)。
#   資格情報は flash に平文で残るので、試したあと -Clear で消すこと(-Ssid / -Psk には何か入れる)。
$s = Split-Path -Parent $MyInvocation.MyCommand.Path
$e = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"
$tmp = Join-Path $env:TEMP "wt_creds.bin"
$bytes = New-Object byte[] 4096
for ($i = 0; $i -lt 4096; $i++) { $bytes[$i] = 0xFF }
if (-not $Clear) {
    $txt = [System.Text.Encoding]::UTF8.GetBytes("ssid=$Ssid`npsk=$Psk`n")
    [Array]::Copy($txt, $bytes, $txt.Length)
}
[System.IO.File]::WriteAllBytes($tmp, $bytes)
& $e --chip esp32 -p COM4 -b 460800 write-flash 0x3F000 $tmp 2>&1 | Select-String 'Wrote|rror' | ForEach-Object { $_.Line }
Remove-Item $tmp
$r = & "$s\wtflash.ps1" -Wait 30000     # 実験像(rocky9 ~/tmp/20261002/wt)を書いて起動。tizix のカーネル(0x1000)は上書きされる
($r -split "`n") | Select-String -Pattern '^wifi:|event:|connected|disconnected|APs|EXC|Fatal|epc' | ForEach-Object { $_.Line }
