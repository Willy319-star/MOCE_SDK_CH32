param(
    [string]$ProjectPath = "",
    [string]$Port = "",
    [int]$DurationSeconds = 15,
    [int]$BaudRate = 115200
)

$ErrorActionPreference = "Stop"

$ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
if ($ports.Count -eq 0) {
    Write-Error "No serial ports found. Please connect a USB-UART adapter or select a COM port."
    exit 1
}

if ([string]::IsNullOrWhiteSpace($Port) -or -not ($ports -contains $Port)) {
    $oldPort = $Port
    $Port = $ports[0]
    if (-not [string]::IsNullOrWhiteSpace($oldPort)) {
        Write-Host "[MONITOR] requested port $oldPort does not exist; using $Port instead. Available: $($ports -join ', ')"
    }
}

Write-Host "[MONITOR] project=$ProjectPath port=$Port baud=$BaudRate duration=${DurationSeconds}s"

$serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 200
$serial.NewLine = "`n"

try {
    $serial.Open()
    $deadline = (Get-Date).AddSeconds($DurationSeconds)
    while ((Get-Date) -lt $deadline) {
        $chunk = $serial.ReadExisting()
        if ($chunk.Length -gt 0) {
            Write-Host -NoNewline $chunk
        }
        Start-Sleep -Milliseconds 100
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
