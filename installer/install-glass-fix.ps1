param([switch]$VerifyOnly, [string]$Destination)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

function File-Sha256([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}

function Safe-Child([string]$Root, [string]$Relative) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':')) { throw 'Invalid package path.' }
    $child = [IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if (-not $child.StartsWith($rootPath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Package path escapes its folder.' }
    $cursor = $child
    while ($cursor.Length -ge $rootPath.Length) {
        if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Installation paths cannot contain links or junctions.' }
        $cursor = Split-Path -Parent $cursor
    }
    return $child
}

try {
    if ($VerifyOnly) {
        if (-not $Destination -or (Test-Path -LiteralPath $Destination)) { throw 'Verification requires a new destination folder.' }
        $stage = [IO.Path]::GetFullPath($Destination)
    } else {
        if (Get-Process -Name tabletpro_radial -ErrorAction SilentlyContinue) { throw 'Close Tablet Pro Radial, then run this installer again.' }
        $stage = Join-Path $PSScriptRoot 'unpacked'
    }
    New-Item -ItemType Directory -Path $stage | Out-Null
    $archive = [IO.Compression.ZipFile]::OpenRead((Join-Path $PSScriptRoot 'payload.zip'))
    try {
        foreach ($entry in $archive.Entries) {
            $path = Safe-Child $stage $entry.FullName
            if (-not $entry.Name) { New-Item -ItemType Directory -Path $path -Force | Out-Null; continue }
            New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path, $false)
        }
    } finally { $archive.Dispose() }
    $files = @()
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $stage 'SHA256SUMS.txt'))) {
        if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Invalid package manifest.' }
        $expected = $Matches[1]; $relative = $Matches[2]
        $path = Safe-Child $stage $relative
        if ((File-Sha256 $path) -ne $expected) { throw "Package verification failed: $relative" }
        $files += $relative
    }
    if (-not ($files -contains 'tabletpro_radial.exe')) { throw 'The package is missing the app.' }
    if ($VerifyOnly) { Write-Output "Verified $($files.Count) packaged files."; exit 0 }

    $installRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\TabletProRadial'
    $installDir = Safe-Child $installRoot '@APP_VERSION@'
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    foreach ($relative in ($files + 'SHA256SUMS.txt')) {
        $target = Safe-Child $installDir $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        if ($relative.EndsWith('.radial.json') -and (Test-Path -LiteralPath $target)) { continue }
        Copy-Item -LiteralPath (Safe-Child $stage $relative) -Destination $target -Force
    }
    $shell = New-Object -ComObject WScript.Shell
    $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Tablet Pro Radial @APP_VERSION@'
    New-Item -ItemType Directory -Path $menu -Force | Out-Null
    $shortcuts = @(
        @{Path=(Join-Path $menu 'Radial menu.lnk'); Args='--radial'},
        @{Path=(Join-Path $menu 'Docked rail.lnk'); Args='--docked'},
        @{Path=(Join-Path $menu 'Editor.lnk'); Args='--editor'},
        @{Path=(Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Tablet Pro Radial @APP_VERSION@.lnk'); Args='--radial'}
    )
    foreach ($item in $shortcuts) {
        $shortcut = $shell.CreateShortcut($item.Path)
        $shortcut.TargetPath = Join-Path $installDir 'tabletpro_radial.exe'
        $shortcut.IconLocation = (Join-Path $installDir 'tabletpro_radial.exe') + ',0'
        $shortcut.Arguments = $item.Args; $shortcut.WorkingDirectory = $installDir; $shortcut.Save()
    }
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TabletProRadial-@APP_VERSION@'
    New-Item -Path $key -Force | Out-Null
    $powerShellExe = Join-Path ([Environment]::GetFolderPath('System')) 'WindowsPowerShell\v1.0\powershell.exe'
    $properties = @{
        DisplayName='Tablet Pro Radial @APP_VERSION@'; DisplayVersion='@APP_VERSION@'; Publisher='Tablet Pro'; InstallLocation=$installDir
        DisplayIcon=(Join-Path $installDir 'tabletpro_radial.exe')
        UninstallString=('"' + $powerShellExe + '" -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + (Join-Path $installDir 'Uninstall.ps1') + '"')
    }
    foreach ($name in $properties.Keys) { New-ItemProperty -Path $key -Name $name -Value $properties[$name] -PropertyType String -Force | Out-Null }
    foreach ($name in @('NoModify','NoRepair')) { New-ItemProperty -Path $key -Name $name -Value 1 -PropertyType DWord -Force | Out-Null }
} catch { Write-Error $_; exit 1 }
