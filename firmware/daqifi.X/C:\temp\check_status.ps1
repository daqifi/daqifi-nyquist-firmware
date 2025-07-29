$port = New-Object System.IO.Ports.SerialPort
$port.PortName = "COM3"
$port.BaudRate = 115200
$port.Open()
$port.WriteLine("SYSTem:Info?")
Start-Sleep -Milliseconds 1000
while ($port.BytesToRead -gt 0) {
    Write-Host $port.ReadLine()
}
$port.Close()