param(
  [Parameter(Mandatory=$true)][string]$Port,
  [string]$LogName = 'nidec-bench-first.csv',
  [string[]]$Commands = @('0:S','300:P 2000 800','1600:S'),
  [int]$CaptureMs = 2300
)
$ErrorActionPreference = 'Stop'
$serial = [System.IO.Ports.SerialPort]::new($Port,115200)
$serial.DtrEnable = $true
$serial.NewLine = "`n"
$serial.WriteTimeout = 1000
$buffer = [System.Text.StringBuilder]::new()
$events = @($Commands | ForEach-Object {
  $pair = $_.Split(':',2)
  [pscustomobject]@{ Time = [int]$pair[0]; Command = $pair[1] }
})
$logPath = [IO.Path]::GetFullPath($LogName)
try {
  $serial.Open()
  $timer = [System.Diagnostics.Stopwatch]::StartNew()
  $next = 0
  while ($timer.ElapsedMilliseconds -lt $CaptureMs) {
    while ($next -lt $events.Count -and $timer.ElapsedMilliseconds -ge $events[$next].Time) {
      $serial.WriteLine($events[$next].Command)
      $next++
    }
    [void]$buffer.Append($serial.ReadExisting())
    Start-Sleep -Milliseconds 10
  }
} finally {
  if ($serial.IsOpen) {
    $serial.WriteLine('S')
    $serial.Close()
  }
  $serial.Dispose()
  $header = 'host_tick_us,t_us,speed_t_us,count,substeps,speed,stopped,mode,demand,output,current_ma,filtered_ma,samples,missed,max_read_us,max_work_us,i2c_errors,fault'
  # Discard partial USB rows, keeping the raw capture alongside parsed CSV.
  $raw = $buffer.ToString()
  [System.IO.File]::WriteAllText($logPath + '.raw', $raw)
  $rows = @($raw -split "`n" | Where-Object { ($_ -split ',').Count -eq 18 -and $_ -match '^\d+,' })
  @($header) + $rows | Set-Content -LiteralPath $logPath
  $Commands | Set-Content -LiteralPath ($logPath + '.commands')
}
$data = @(Import-Csv -LiteralPath $logPath)
Write-Output "Log: $logPath; rows: $($data.Count)"
if ($data.Count) {
  $data | Select-Object -First 2 | ConvertTo-Json -Compress
  $data | Select-Object -Last 2 | ConvertTo-Json -Compress
  $speed = @($data | ForEach-Object { [double]$_.speed })
  $count = @($data | ForEach-Object { [long]$_.count })
  [pscustomobject]@{
    MinSpeed = ($speed | Measure-Object -Minimum).Minimum
    MaxSpeed = ($speed | Measure-Object -Maximum).Maximum
    MinCount = ($count | Measure-Object -Minimum).Minimum
    MaxCount = ($count | Measure-Object -Maximum).Maximum
    Faults = ($data.fault | Sort-Object -Unique) -join ','
    FinalMode = $data[-1].mode
    FinalOutput = $data[-1].output
    FinalStopped = $data[-1].stopped
  } | ConvertTo-Json -Compress
}
