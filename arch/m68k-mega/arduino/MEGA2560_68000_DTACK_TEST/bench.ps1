# bench.ps1 - measure how long tizix/68000 takes per shell command (115200 bps).
# ASCII only (PS 5.1 reads BOM-less UTF-8 as cp932 and breaks on non-ASCII).
#
#   .\bench.ps1                          boot, then time "pwd" and "ls /bin"
#   .\bench.ps1 -Cmds "ls /bin","ps"     time your own commands
#   .\bench.ps1 -Log $env:USERPROFILE\bench.txt
#
# Opening the port resets the Mega, so every run starts from the SRAM transfer.
param(
    [string]  $Port    = "COM3",
    [int]     $Boot    = 40,
    [string[]]$Cmds    = @("pwd", "ls /bin"),
    [int]     $Timeout = 180,
    [string]  $Log     = ""
)

$PROMPT = "]# "

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, "One"
$sp.ReadTimeout  = 100
$sp.DtrEnable    = $true
try { $sp.Open() } catch { Write-Host "cannot open $Port : $_" -ForegroundColor Red; exit 1 }

$all = New-Object System.Text.StringBuilder

# read until $until appears in the new text, or $secs elapse. returns seconds.
function ReadUntil([string]$until, [int]$secs) {
    $t0  = Get-Date
    $tail = ""
    while (((Get-Date) - $t0).TotalSeconds -lt $secs) {
        try {
            $n = $sp.BytesToRead
            if ($n -gt 0) {
                $buf = New-Object byte[] $n
                $got = $sp.Read($buf, 0, $n)
                $txt = [System.Text.Encoding]::ASCII.GetString($buf, 0, $got)
                [void]$all.Append($txt)
                Write-Host -NoNewline $txt
                $tail += $txt
                if ($tail.Length -gt 4096) { $tail = $tail.Substring($tail.Length - 4096) }
                if ($until -ne "" -and $tail.Contains($until)) {
                    return ((Get-Date) - $t0).TotalSeconds
                }
            } else {
                Start-Sleep -Milliseconds 5
            }
        } catch { Start-Sleep -Milliseconds 5 }
    }
    return -1
}

Write-Host "---- waiting for shell prompt ----" -ForegroundColor Cyan
$t = ReadUntil $PROMPT $Boot
if ($t -lt 0) { Write-Host "`nno prompt within $Boot s" -ForegroundColor Red }
else          { Write-Host "`nboot -> prompt : $([math]::Round($t,2)) s" -ForegroundColor Green }

$results = @()
foreach ($c in $Cmds) {
    Start-Sleep -Milliseconds 300
    Write-Host "`n---- sending: $c ----" -ForegroundColor Cyan
    $sp.DiscardInBuffer()
    $t0 = Get-Date
    $sp.Write($c + "`r")
    $el = ReadUntil $PROMPT $Timeout
    if ($el -lt 0) { Write-Host "`n[$c] TIMEOUT" -ForegroundColor Red; $results += ,@($c, -1) }
    else           { $results += ,@($c, [math]::Round($el, 2)) }
}

Start-Sleep -Milliseconds 500
[void](ReadUntil "" 1)
$sp.Close()

Write-Host ""
Write-Host "==== results ====" -ForegroundColor Yellow
foreach ($r in $results) { "{0,-24} {1,8} s" -f $r[0], $r[1] | Write-Host }
Write-Host "---- closed ----" -ForegroundColor Cyan

if ($Log -ne "") {
    $all.ToString() | Out-File -FilePath $Log -Encoding ascii
    Write-Host "log -> $Log"
}
