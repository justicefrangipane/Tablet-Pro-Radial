#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RuntimeTemplate,
    [string]$QtRoot = 'C:\Qt\6.11.1\mingw_64',
    [string]$MinGWBin = 'C:\Qt\Tools\mingw1310_64\bin',
    [string]$CMake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$Ninja = 'C:\Qt\Tools\Ninja\ninja.exe',
    [string]$Python = 'python',
    [string]$Repository = 'justicefrangipane/Tablet-Pro-Radial'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)." }
}
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version = [regex]::Match((Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(TabletProRadial VERSION (\d+\.\d+\.\d+)').Groups[1].Value
if (-not $version) { throw 'Missing project version.' }
& (Join-Path $PSScriptRoot 'update-history.ps1') -Version $version
$build = Join-Path $root 'build\windows'
$payload = Join-Path $root "dist\Tablet-Pro-Radial-$version"
$setup = Join-Path $root "dist\Tablet-Pro-Radial-$version-Windows-x64-Setup.exe"
if ((Test-Path -LiteralPath $payload) -or (Test-Path -LiteralPath $setup)) {
    throw 'This version already has build output. Increment CMakeLists.txt or archive the existing output first.'
}
$template = (Resolve-Path -LiteralPath $RuntimeTemplate).ProviderPath
foreach ($item in @('licenses','lib\msvcp140.dll','lib\vcruntime140.dll','lib\vcruntime140_1.dll','Qt6Core.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $template $item))) { throw "Runtime template is missing $item." }
}
$sourceQt = (Get-Item -LiteralPath (Join-Path $QtRoot 'bin\Qt6Core.dll')).VersionInfo.FileVersion
$templateQt = (Get-Item -LiteralPath (Join-Path $template 'Qt6Core.dll')).VersionInfo.FileVersion
if ($sourceQt -ne $templateQt) { throw 'Use a runtime template matching the Qt kit so notices and dependencies stay aligned.' }
$oldPath = $env:PATH
try {
    $env:PATH = "$QtRoot\bin;$MinGWBin;" + $oldPath
    Run $CMake @('-S',$root,'-B',$build,'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$Ninja",'-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_PREFIX_PATH=$QtRoot",'-DBUILD_TESTING=ON')
    Run $CMake @('--build',$build,'--parallel','4')
    Run (Join-Path (Split-Path -Parent $CMake) 'ctest.exe') @('--test-dir',$build,'--output-on-failure','--no-tests=error')
    New-Item -ItemType Directory -Path (Join-Path $payload 'lib') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $build 'tabletpro_radial.exe') -Destination $payload
    Copy-Item -LiteralPath (Join-Path $build 'lib\TabletProGlass.dll') -Destination (Join-Path $payload 'lib')
    Copy-Item -LiteralPath (Join-Path $root 'vendor\tabletpro\TabletProHIDx64.dll') -Destination (Join-Path $payload 'lib')
    foreach ($name in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
        Copy-Item -LiteralPath (Join-Path $template "lib\$name") -Destination (Join-Path $payload 'lib')
    }
    Copy-Item -LiteralPath (Join-Path $template 'licenses') -Destination $payload -Recurse
    Run (Join-Path $QtRoot 'bin\windeployqt.exe') @('--release','--verbose','0','--compiler-runtime','--qmldir',(Join-Path $root 'qml'),'--dir',$payload,(Join-Path $payload 'tabletpro_radial.exe'))
    Run $Python @((Join-Path $root 'installer\build-glass-fix.py'),'--payload',$payload,'--repository',$Repository)
    $verify = Join-Path $root ('build\verify-' + $version + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    $process = Start-Process -FilePath $setup -ArgumentList @('--verify',('"' + $verify + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Installer verification failed ($($process.ExitCode))." }
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $smoke = Join-Path $build 'release-smoke'
    $process = Start-Process -FilePath (Join-Path $verify 'tabletpro_radial.exe') -ArgumentList @('--smoke','--runtime-smoke','--output',('"' + $smoke + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Packaged app smoke check failed ($($process.ExitCode))." }
    $hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
    $hash + '  ' + [IO.Path]::GetFileName($setup) | Set-Content -LiteralPath (Join-Path $root "dist\Tablet-Pro-Radial-$version-SHA256SUMS.txt") -Encoding ASCII
    Write-Output "Verified release: $setup"
} finally { $env:PATH = $oldPath }
