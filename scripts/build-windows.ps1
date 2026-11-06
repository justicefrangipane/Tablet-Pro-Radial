#requires -Version 5.1
<#
.SYNOPSIS
Build, test, and package the Windows x64 editor and runtime overlay.
.EXAMPLE
.\scripts\build-windows.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$QtRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-CheckedNative {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    & $Executable @Arguments
    $nativeExitCode = $LASTEXITCODE
    if ($nativeExitCode -ne 0) {
        throw "Native command failed with exit code ${nativeExitCode}: $Executable"
    }
}

function Assert-ProjectPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $rootPrefix = $projectRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing an output path outside this project: $fullPath"
    }
    # Existing output junctions/symlinks must not redirect writes elsewhere.
    $cursor = $fullPath
    while ($cursor.Length -ge $projectRoot.Length) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Output paths may not pass through a junction or symbolic link: $cursor"
            }
        }
        if ($cursor.Equals($projectRoot, [StringComparison]::OrdinalIgnoreCase)) { break }
        $cursor = Split-Path -Parent $cursor
    }
}

if ($env:OS -ne 'Windows_NT') {
    throw 'This script requires Windows and Visual Studio 2022 with the C++ x64 tools.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedQt = (Resolve-Path -LiteralPath $QtRoot).ProviderPath
$qtBin = Join-Path $resolvedQt 'bin'
$deployTool = Join-Path $qtBin 'windeployqt.exe'
$qtConfig = Join-Path $resolvedQt 'lib\cmake\Qt6\Qt6Config.cmake'
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf) -or
    -not (Test-Path -LiteralPath $qtConfig -PathType Leaf)) {
    throw 'QtRoot must identify an installed Qt 6 MSVC x64 kit with windeployqt and CMake files.'
}

$cmake = (Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
    throw 'ctest.exe must be installed alongside cmake.exe.'
}
$versionOutput = (Invoke-CheckedNative -Executable $cmake -Arguments @('--version')) -join "`n"
$versionMatch = [regex]::Match($versionOutput, '(?m)^cmake version (\d+\.\d+\.\d+)')
if (-not $versionMatch.Success -or [version]($versionMatch.Groups[1].Value) -lt [version]'3.21.0') {
    throw 'CMake 3.21 or newer is required for the Visual Studio 2022 generator.'
}

$buildDir = Join-Path $projectRoot 'build\windows-vs2022-x64'
$distRoot = Join-Path $projectRoot 'dist'
Assert-ProjectPath -Path $buildDir
Assert-ProjectPath -Path $distRoot
$packageName = 'TabletPro-Radial-win-x64-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$packageDir = Join-Path $distRoot $packageName
$archive = Join-Path $distRoot ($packageName + '.zip')
Assert-ProjectPath -Path $packageDir
Assert-ProjectPath -Path $archive

$previousPath = $env:PATH
$previousPluginPath = $env:QT_PLUGIN_PATH
$previousQmlPath = $env:QML2_IMPORT_PATH
try {
    $env:PATH = $qtBin + [IO.Path]::PathSeparator + $previousPath
    $env:QT_PLUGIN_PATH = Join-Path $resolvedQt 'plugins'
    $env:QML2_IMPORT_PATH = Join-Path $resolvedQt 'qml'

    Write-Host 'Configuring Visual Studio 2022 x64...'
    Invoke-CheckedNative -Executable $cmake -Arguments @(
        '-S', $projectRoot, '-B', $buildDir,
        '-G', 'Visual Studio 17 2022', '-A', 'x64', '-T', 'v143,host=x64',
        "-DCMAKE_PREFIX_PATH=$resolvedQt",
        '-DTABLETPRO_BUILD_DESKTOP=ON', '-DBUILD_TESTING=ON'
    )
    Write-Host 'Building Release application and tests...'
    Invoke-CheckedNative -Executable $cmake -Arguments @(
        '--build', $buildDir, '--config', 'Release', '--parallel',
        '--target', 'tabletpro_radial', 'radial_core_tests', 'radial_app_tests', 'radial_stylus_tests', 'radial_backdrop_tests', 'radial_glass_visual_tests', 'radial_update_tests'
    )
    Write-Host 'Running the portable core and editor/format tests...'
    Invoke-CheckedNative -Executable $ctest -Arguments @(
        '--test-dir', $buildDir, '-C', 'Release', '--output-on-failure', '--no-tests=error'
    )

    $builtExe = Join-Path $buildDir 'Release\tabletpro_radial.exe'
    if (-not (Test-Path -LiteralPath $builtExe -PathType Leaf)) {
        throw "The expected Release executable was not produced: $builtExe"
    }
    New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
    # A fresh unique folder avoids stale DLLs and never deletes an earlier package.
    New-Item -ItemType Directory -Path $packageDir | Out-Null
    $packagedExe = Join-Path $packageDir 'tabletpro_radial.exe'
    Copy-Item -LiteralPath $builtExe -Destination $packagedExe
    $hidLibrary = Join-Path $projectRoot 'vendor\tabletpro\TabletProHIDx64.dll'
    New-Item -ItemType Directory -Path (Join-Path $packageDir 'lib') | Out-Null
    Copy-Item -LiteralPath $hidLibrary -Destination (Join-Path $packageDir 'lib')
    Copy-Item -LiteralPath (Join-Path $buildDir 'Release\lib\TabletProGlass.dll') -Destination (Join-Path $packageDir 'lib')
    Write-Host 'Collecting Qt libraries, plugins, and QML imports...'
    Invoke-CheckedNative -Executable $deployTool -Arguments @(
        '--release', '--no-compiler-runtime',
        '--qmldir', (Join-Path $projectRoot 'qml'),
        '--dir', $packageDir, $packagedExe
    )
    foreach ($relative in @('Qt6Core.dll', 'Qt6Quick.dll', 'platforms\qwindows.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $packageDir $relative) -PathType Leaf)) {
            throw "Deployment did not produce a required runtime file: $relative"
        }
    }
    $readme = @'
Tablet Pro Radial - Windows x64 development build

Extract the entire folder, then open tabletpro_radial.exe.
Keep the DLL, QML, and plugin folders beside the executable.
The destination PC needs the Microsoft Visual C++ x64 runtime for Visual Studio 2022.
This package does not install runtimes or request elevation.

The menu opens near your cursor. Press the selected stylus side button or
Ctrl+Alt+Space to show/hide it. Use Dock to move it to the screen edge, or Edit
to configure your profile and Pen button settings. Release the shortcut
modifiers before completing a gesture. Close the original Tablet Pro Studio
while testing to prevent both versions responding to the same pen input.

The automated core/editor tests passed during this package build. That result
does not validate Windows focus, pen hardware, SendInput, or mixed-DPI behavior.
Complete WINDOWS-VALIDATION.md before treating this as a release.
'@
    Set-Content -LiteralPath (Join-Path $packageDir 'README.txt') -Value $readme -Encoding UTF8
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\WINDOWS-VALIDATION.md') -Destination $packageDir
    Compress-Archive -LiteralPath $packageDir -DestinationPath $archive -CompressionLevel Optimal
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf) -or (Get-Item -LiteralPath $archive).Length -eq 0) {
        throw 'The package archive was not created successfully.'
    }
    Write-Host "Package folder: $packageDir"
    Write-Host "Package ZIP:    $archive"
} catch {
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    exit 1
} finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPluginPath
    $env:QML2_IMPORT_PATH = $previousQmlPath
}
