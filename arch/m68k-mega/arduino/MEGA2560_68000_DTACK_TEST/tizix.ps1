# tizix.ps1 - serial client for the TIZIX HOST firmware (115200 bps).
# ASCII only (PS 5.1 reads BOM-less UTF-8 as cp932 and breaks on non-ASCII).
#
#   .\tizix.ps1                        capture 10 s from power-on (opening the
#                                      port resets the Mega, so you always see
#                                      the SRAM transfer log from the start)
#   .\tizix.ps1 -Seconds 20
#   .\tizix.ps1 -Send "pwd`n"          boot, then type a line into the shell
#   .\tizix.ps1 -NoReset               attach without resetting the board
#
param(
    [string]$Port    = "COM3",
    [int]   $Seconds = 10,
    [string]$Send    = "",
    [int]   $After   = 4,
    [switch]$NoReset,
    [string]$Log     = ""
)

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, "One"
$sp.ReadTimeout  = 100
$sp.WriteTimeout = 2000
if ($NoReset) { $sp.DtrEnable = $false } else { $sp.DtrEnable = $true }

try { $sp.Open() } catch { Write-Host "cannot open $Port : $_" -ForegroundColor Red; exit 1 }

$sb = New-Object System.Text.StringBuilder

function Drain([int]$secs) {
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt $secs) {
        try {
            $n = $sp.BytesToRead
            if ($n -gt 0) {
                $buf = New-Object byte[] $n
                $got = $sp.Read($buf, 0, $n)
                $txt = [System.Text.Encoding]::ASCII.GetString($buf, 0, $got)
                [void]$sb.Append($txt)
                Write-Host -NoNewline $txt
            } else {
                Start-Sleep -Milliseconds 20
            }
        } catch { Start-Sleep -Milliseconds 20 }
    }
}

Drain $Seconds

if ($Send -ne "") {
    Write-Host ""
    Write-Host "---- sending ----" -ForegroundColor Cyan
    $sp.Write($Send)
    Drain $After
}

$sp.Close()
Write-Host ""
Write-Host "---- closed ----" -ForegroundColor Cyan

if ($Log -ne "") {
    $sb.ToString() | Out-File -FilePath $Log -Encoding ascii
    Write-Host "log -> $Log"
}
