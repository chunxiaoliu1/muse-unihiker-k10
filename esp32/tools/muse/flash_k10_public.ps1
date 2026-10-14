param([Parameter(Mandatory=$true)][string]$Port, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$manifest = Get-Content (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
$argsList = @('-m', 'esptool', '--chip', 'esp32s3', '--port', $Port,
    'write-flash', '--flash-mode', 'dio', '--flash-freq', '80m', '--flash-size', '16MB')
foreach ($item in $manifest.files) {
    $argsList += @($item.offset, (Join-Path $PSScriptRoot $item.file))
}
& $Python @argsList
if ($LASTEXITCODE -ne 0) { throw "Flashing failed ($LASTEXITCODE)" }
Write-Host 'Flashing complete. Release BOOT, press RESET once, then set your SDK Token using the USB configuration page.'
