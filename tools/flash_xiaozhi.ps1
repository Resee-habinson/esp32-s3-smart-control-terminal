[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port,

    [ValidateSet(115200, 230400, 460800, 921600)]
    [int]$Baud = 460800,

    [ValidateSet('Performance', 'Official')]
    [string]$Edition = 'Performance'
)

$ErrorActionPreference = 'Stop'

$firmwareConfig = @{
    Performance = @{
        RelativePath = '..\firmware\xiaozhi-v2.4.0-waveshare-esp32-s3-touch-lcd-3.5-ui-performance\merged-binary.bin'
        Sha256 = '8C66E95FD2D41A97139C2B6C74E9969E18C596010AD83235A74EE7FFFC4AC454'
    }
    Official = @{
        RelativePath = '..\firmware\xiaozhi-v2.4.0-waveshare-esp32-s3-touch-lcd-3.5\merged-binary.bin'
        Sha256 = 'C8BCA538C6855763CBBEDCC55CE7F5CEC3AE2888C3DCD8B3231E388DDFEFBCAF'
    }
}
$selectedFirmware = $firmwareConfig[$Edition]
$firmwarePath = Join-Path $PSScriptRoot $selectedFirmware.RelativePath
$expectedSha256 = $selectedFirmware.Sha256

if (-not (Test-Path -LiteralPath $firmwarePath -PathType Leaf)) {
    throw "Firmware image not found: $firmwarePath"
}

$firmware = Get-Item -LiteralPath $firmwarePath
$actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $firmware.FullName).Hash
if ($actualSha256 -ne $expectedSha256) {
    throw "Firmware SHA-256 mismatch. Expected $expectedSha256, got $actualSha256."
}

$serialPort = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
    Where-Object DeviceID -eq $Port
if ($null -eq $serialPort) {
    throw "Serial port $Port was not found. Reconnect the board and check Device Manager."
}
if ($serialPort.PNPDeviceID -like 'BTHENUM\*') {
    throw "$Port is a Bluetooth serial port, not the ESP32-S3 USB port."
}

$esptool = Get-Command esptool -ErrorAction SilentlyContinue
if ($null -eq $esptool) {
    $esptool = Get-Command esptool.py -ErrorAction SilentlyContinue
}
if ($null -eq $esptool) {
    throw 'esptool was not found. Install it with: py -m pip install esptool'
}

Write-Host "Board:    Waveshare ESP32-S3-Touch-LCD-3.5 with OV2640/OV5640 camera support"
Write-Host "Edition:  $Edition"
Write-Host "Port:     $Port"
Write-Host "Firmware: $($firmware.FullName)"
Write-Host "SHA-256:  $actualSha256"
Write-Host ''
Write-Host 'Erasing the complete flash to avoid v1/v2 partition conflicts...'

& $esptool.Source --chip esp32s3 --port $Port --baud $Baud `
    --before default-reset --after hard-reset erase-flash
if ($LASTEXITCODE -ne 0) {
    throw "Flash erase failed with exit code $LASTEXITCODE."
}

Write-Host ''
Write-Host 'Writing the complete merged image at address 0x0...'
& $esptool.Source --chip esp32s3 --port $Port --baud $Baud `
    --before default-reset --after hard-reset write-flash 0x0 $firmware.FullName
if ($LASTEXITCODE -ne 0) {
    throw "Firmware write failed with exit code $LASTEXITCODE."
}

Write-Host ''
Write-Host 'Flash completed and verified. The board has been reset.'
