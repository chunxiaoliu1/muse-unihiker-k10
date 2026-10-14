param(
    [ValidateSet('build', 'flash', 'monitor')][string]$Action = 'build',
    [string]$Port = 'COM10'
)
$ErrorActionPreference = 'Stop'
if (-not $env:IDF_PATH) {
    $idfPythonScripts = Join-Path $env:USERPROFILE '.espressif/python_env/idf6.0_py3.13_env/Scripts'
    if (Test-Path (Join-Path $idfPythonScripts 'python.exe')) {
        $env:PATH = $idfPythonScripts + ';' + $env:PATH
    }
    . 'C:\Espressif\frameworks\esp-idf-v6.0.1\export.ps1'
}
$projectPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $projectPath
try {
    if ($Action -eq 'monitor') {
        python tools/muse/monitor.py $Port 30
    } else {
        $idfArgs = @('-B', 'build-muse-dfrobot-unihiker-k10', '-DIDF_TARGET=esp32s3',
            '-DSDKCONFIG=build-muse-dfrobot-unihiker-k10/sdkconfig',
            '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-dfrobot-unihiker-k10')
        if ($Action -eq 'flash') { $idfArgs += @('-p', $Port) }
        idf.py @idfArgs $Action
    }
    if ($LASTEXITCODE -ne 0) { throw "ESP-IDF $Action failed ($LASTEXITCODE)" }
} finally { Pop-Location }
