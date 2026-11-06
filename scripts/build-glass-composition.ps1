param([string]$OutputDirectory='build\windows\lib')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputPath=if([IO.Path]::IsPathRooted($OutputDirectory)){[IO.Path]::GetFullPath($OutputDirectory)}else{[IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))}
if(-not $outputPath.StartsWith($projectRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside the project.'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$compiler=@(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\cl.exe') | Sort-Object | Select-Object -Last 1
if(-not $compiler){throw 'Visual Studio C++ x64 build tools are required for the Windows composition bridge.'}
$toolRoot=[IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $compiler) '..\..\..'))
$kitRoot=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk=Get-ChildItem -LiteralPath (Join-Path $kitRoot 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'cppwinrt\winrt\Windows.UI.Composition.h') } | Sort-Object Name | Select-Object -Last 1
if(-not $sdk){throw 'A Windows SDK with C++/WinRT headers is required.'}
$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
try {
    $env:INCLUDE=(Join-Path $toolRoot 'include')+';'+((@('ucrt','um','shared','winrt','cppwinrt') | ForEach-Object {Join-Path $sdk.FullName $_}) -join ';')
    $env:LIB=(Join-Path $toolRoot 'lib\x64')+';'+(Join-Path $kitRoot ('Lib\'+$sdk.Name+'\ucrt\x64'))+';'+(Join-Path $kitRoot ('Lib\'+$sdk.Name+'\um\x64'))
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
    & $compiler /nologo /std:c++20 /EHsc /MT /O2 /LD /utf-8 (Join-Path $projectRoot 'platform\GlassComposition.cpp') ('/Fo'+(Join-Path $outputPath 'GlassComposition.obj')) ('/Fe'+(Join-Path $outputPath 'TabletProGlass.dll')) /link windowsapp.lib d2d1.lib d3d11.lib dxguid.lib dwmapi.lib coremessaging.lib ole32.lib ('/IMPLIB:'+(Join-Path $outputPath 'TabletProGlass.lib'))
    if($LASTEXITCODE -ne 0){throw "Composition bridge build failed ($LASTEXITCODE)."}
} finally {$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
