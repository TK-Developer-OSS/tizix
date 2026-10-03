# hwbridge.ps1 - 実機の回帰用の中継(Windows 側)。シリアルの実機と rocky9 の python/hwrelay.py をつなぐ。
#
#   powershell -ExecutionPolicy Bypass -File hwbridge.ps1 -Port COM4 -Server rocky9 -TcpPort 47001
#
#   rocky9 側でテストが hwrelay.py を起動して待ち受けると、ここがつなぎに行き、
#   実機をリセット(RTS で EN を Low → High。DTR は Low のままで IO0 = 通常起動)してから、
#   シリアルとソケットを素通しする。テストが終わってソケットが閉じたら、次の接続を待つ。
#   止めるときは Ctrl+C(またはこのプロセスを終了)。
#   ESP32 の DevKit の USB シリアル(RTS = EN、DTR = IO0 の自動リセット回路)を前提にしている。
param(
    [string]$Port = "COM4",
    [int]$Baud = 115200,
    [string]$Server = "rocky9",
    [int]$TcpPort = 47001
)

Add-Type -AssemblyName System
$sp = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 1
$sp.Open()
$sp.DtrEnable = $false
$buf = New-Object byte[] 4096

while ($true) {
    $c = $null
    try {
        $c = New-Object System.Net.Sockets.TcpClient
        $c.NoDelay = $true
        $c.Connect($Server, $TcpPort)
    } catch {
        if ($c) { $c.Dispose() }
        Start-Sleep -Milliseconds 300
        continue
    }
    $ns = $c.GetStream()
    # リセット: 前の回の残りを捨ててから EN を叩く
    $sp.DiscardInBuffer()
    $sp.RtsEnable = $true; Start-Sleep -Milliseconds 120; $sp.RtsEnable = $false
    Write-Host ("[{0:HH:mm:ss}] 接続、リセットした" -f (Get-Date))
    try {
        while ($c.Connected) {
            $idle = $true
            if ($sp.BytesToRead -gt 0) {
                $n = $sp.Read($buf, 0, [Math]::Min($buf.Length, $sp.BytesToRead))
                if ($n -gt 0) { $ns.Write($buf, 0, $n); $idle = $false }
            }
            if ($ns.DataAvailable) {
                $n = $ns.Read($buf, 0, $buf.Length)
                if ($n -le 0) { break }
                $sp.Write($buf, 0, $n); $idle = $false
            } elseif ($c.Client.Poll(0, [System.Net.Sockets.SelectMode]::SelectRead) -and $c.Client.Available -eq 0) {
                break   # 相手が閉じた
            }
            if ($idle) { Start-Sleep -Milliseconds 1 }
        }
    } catch {
        Write-Host "切断: $_"
    }
    $c.Close()
    Write-Host ("[{0:HH:mm:ss}] 切断" -f (Get-Date))
}
