# BUSPROBE client: send each argument as a command, print the reply up to the "> " prompt.
# (ASCII only: PowerShell 5.1 reads BOM-less UTF-8 as cp932 and eats line ends)
# usage: .\mega.ps1 p "r 0 8" "mv 0 100000"
# Replies longer than $MaxShow lines go to .\log\reply_NNN.txt and only the NG
# lines plus the tail are echoed, so a full 1MB march runs in one shot.
param([Parameter(Position = 0, ValueFromRemainingArguments = $true)][string[]]$Cmds)

$TimeoutSec = 3600
$MaxShow    = 200
$MaxNgShow  = 40

$p = New-Object System.IO.Ports.SerialPort 'COM3', 1000000, 'None', 8, 'One'
$p.DtrEnable = $false; $p.RtsEnable = $false
$p.Encoding = [System.Text.Encoding]::ASCII
$p.ReadBufferSize = 262144
$p.Open()
try {
    # opening does not reset the Mega (a stray NUL arrives; firmware drops it). drain leftovers.
    Start-Sleep -Milliseconds 200
    [void]$p.ReadExisting()

    $n = 0
    foreach ($c in $Cmds) {
        $n++
        $p.Write("$c`r")

        # StringBuilder: plain '+=' is O(n^2) and stalls on multi-MB replies.
        $sb = New-Object System.Text.StringBuilder
        $t0 = [DateTime]::Now
        $done = $false
        while (-not $done) {
            $s = $p.ReadExisting()
            if ($s) {
                [void]$sb.Append($s)
                if ($sb.Length -ge 2 -and $sb[$sb.Length - 1] -eq ' ' -and $sb[$sb.Length - 2] -eq '>') {
                    $done = $true
                }
            }
            else {
                Start-Sleep -Milliseconds 10
            }
            if (([DateTime]::Now - $t0).TotalSeconds -gt $TimeoutSec) {
                [void]$sb.Append("`n[TIMEOUT]")
                $p.Write("x")
                $done = $true
            }
        }
        $elapsed = ([DateTime]::Now - $t0).TotalSeconds

        $txt = ($sb.ToString() -replace "`r", '') -replace '> $', ''
        $lines = $txt -split "`n"

        if ($lines.Count -gt $MaxShow) {
            $dir = Join-Path $PSScriptRoot 'log'
            if (!(Test-Path $dir)) { [void](New-Item -ItemType Directory -Path $dir) }
            $f = Join-Path $dir ("reply_{0:000}.txt" -f $n)
            Set-Content -Path $f -Value $txt -Encoding ASCII
            Write-Output ("[{0} lines, {1:N1}s -> {2}]" -f $lines.Count, $elapsed, $f)
            $ngs = @($lines | Where-Object { $_ -match ' NG' })
            if ($ngs.Count -gt 0) {
                Write-Output ("NG lines: {0}" -f $ngs.Count)
                $ngs | Select-Object -First $MaxNgShow | ForEach-Object { Write-Output $_ }
                if ($ngs.Count -gt $MaxNgShow) {
                    Write-Output ("... and {0} more NG lines (see the file)" -f ($ngs.Count - $MaxNgShow))
                }
            }
            else {
                Write-Output "no NG lines"
            }
            $lines | Where-Object { $_ -ne '' } | Select-Object -Last 4 | ForEach-Object { Write-Output $_ }
        }
        else {
            Write-Output $txt.TrimEnd()
        }
        Write-Output '----'
    }
}
finally { $p.Close() }
