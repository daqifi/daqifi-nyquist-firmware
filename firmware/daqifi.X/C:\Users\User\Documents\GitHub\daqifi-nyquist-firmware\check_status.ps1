# Open COM3 and check DAQiFi status
$port = New-Object System.IO.Ports.SerialPort
$port.PortName = "COM3"
$port.BaudRate = 115200
$port.Parity = "None"
$port.DataBits = 8
$port.StopBits = "One"
$port.ReadTimeout = 2000

try {
    $port.Open()
    Write-Host "Connected to COM3"
    
    # Send system info command
    $port.WriteLine("SYSTem:Info?")
    Start-Sleep -Milliseconds 500
    
    # Read response
    Write-Host "`n--- System Info ---"
    while ($port.BytesToRead -gt 0) {
        $line = $port.ReadLine()
        Write-Host $line
    }
    
    # Send log command
    Start-Sleep -Milliseconds 100
    $port.WriteLine("SYSTem:LOG?")
    Start-Sleep -Milliseconds 500
    
    # Read logs
    Write-Host "`n--- System Logs ---"
    $logCount = 0
    while ($port.BytesToRead -gt 0 -and $logCount -lt 50) {
        $line = $port.ReadLine()
        Write-Host $line
        $logCount++
    }
    
} catch {
    Write-Host "Error: $_"
} finally {
    if ($port.IsOpen) {
        $port.Close()
        Write-Host "`nPort closed"
    }
}