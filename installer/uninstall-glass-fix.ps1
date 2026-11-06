Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
function File-Sha256([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}
try {
    $expected = [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\TabletProRadial\@APP_VERSION@'))
    $actual = [IO.Path]::GetFullPath($PSScriptRoot)
    if (-not $actual.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) { throw 'Run the uninstaller from the installed app folder.' }
    if (Get-Process -Name tabletpro_radial -ErrorAction SilentlyContinue) { throw 'Close Tablet Pro Radial before uninstalling.' }
    if ([Windows.Forms.MessageBox]::Show('Uninstall Tablet Pro Radial @APP_VERSION@? Your saved menus and settings will be kept.', 'Tablet Pro Radial', 'YesNo', 'Question') -ne 'Yes') { exit 0 }
    $manifest = Join-Path $actual 'SHA256SUMS.txt'
    foreach ($line in [IO.File]::ReadAllLines($manifest)) {
        if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Invalid uninstall manifest.' }
        $hash = $Matches[1]; $relative = $Matches[2]
        if ([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':')) { throw 'Invalid uninstall path.' }
        $target = [IO.Path]::GetFullPath((Join-Path $actual $relative))
        if (-not $target.StartsWith($actual + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Uninstall path is outside the app folder.' }
        $cursor = $target
        while ($cursor.Length -ge $actual.Length) {
            if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Uninstall does not follow links or junctions.' }
            $cursor = Split-Path -Parent $cursor
        }
        if ($relative.EndsWith('.radial.json')) { continue }
        if ((Test-Path -LiteralPath $target -PathType Leaf) -and ((File-Sha256 $target) -eq $hash)) { Remove-Item -LiteralPath $target }
    }
    Remove-Item -LiteralPath $manifest
    $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Tablet Pro Radial @APP_VERSION@'
    foreach ($name in @('Radial menu.lnk','Docked rail.lnk','Editor.lnk')) { Remove-Item -LiteralPath (Join-Path $menu $name) -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Tablet Pro Radial @APP_VERSION@.lnk') -ErrorAction SilentlyContinue
    # Do not traverse user-added directories or junctions during cleanup.
    # Keep nested folders alongside preserved profiles; remove only empty roots.
    foreach ($dir in @($menu,$actual)) { if ((Test-Path -LiteralPath $dir) -and -not (Get-ChildItem -LiteralPath $dir -Force | Select-Object -First 1)) { [IO.Directory]::Delete($dir) } }
    Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TabletProRadial-@APP_VERSION@'
} catch { [Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Uninstall could not finish', 'OK', 'Error') | Out-Null; exit 1 }
