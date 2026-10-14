param(
    [ValidateSet('build', 'flash', 'package')][string]$Action = 'build',
    [string]$Port = 'COM14'
)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
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
    $publicBuild = 'build-muse-k10-public'
    $idfArgs = @('-B', $publicBuild, '-DIDF_TARGET=esp32s3',
        "-DSDKCONFIG=$publicBuild/sdkconfig",
        '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-dfrobot-unihiker-k10;devices/sdkconfig.muse-k10-public')
    # Never silently reuse a private configuration in the public directory.
    if (Test-Path "$publicBuild/sdkconfig") {
        $configText = Get-Content "$publicBuild/sdkconfig" -Raw
        if ($configText -notmatch '(?m)^CONFIG_GADGET_SDK_RUNTIME_ONLY=y\r?$') {
            throw 'Public build directory has an incompatible config. Use a new public build directory.'
        }
    }
    idf.py @idfArgs build
    if ($LASTEXITCODE -ne 0) { throw "Public build failed ($LASTEXITCODE)" }
    python tools/muse/package_k10_public.py $publicBuild
    if ($LASTEXITCODE -ne 0) { throw 'Public firmware credential audit/package failed' }
    if ($Action -eq 'flash') {
        idf.py @idfArgs -p $Port flash
        if ($LASTEXITCODE -ne 0) { throw "Flash failed ($LASTEXITCODE)" }
    }
} finally { Pop-Location }
