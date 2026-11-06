#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Version, [string]$OutputFile)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$history = Get-Content -LiteralPath (Join-Path $root 'docs\update-history.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$releases = @($history.releases)
if ($releases.Count -eq 0 -or $releases[0].version -cne $Version) {
    throw "Add an update-log entry for $Version at the top of docs/update-history.json before building or publishing."
}
$previous = $null
foreach ($entry in $releases) {
    if ($entry.version -notmatch '^\d+\.\d+\.\d+$' -or [string]::IsNullOrWhiteSpace($entry.title)) { throw 'Each update needs a version and a title.' }
    $parsed = [version]$entry.version
    if ($null -ne $previous -and $parsed -ge $previous) { throw 'Update-log versions must be unique and ordered newest first.' }
    $previous = $parsed
    $date = [datetime]::MinValue
    if (-not [datetime]::TryParseExact($entry.date, 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$date)) { throw 'Update-log dates must use YYYY-MM-DD.' }
    $count = 0
    foreach ($kind in @('new','improved','fixed')) {
        $section = $entry.PSObject.Properties[$kind]
        if ($null -ne $section) {
            if ($section.Value -isnot [array]) { throw 'Update-log sections must be arrays of changes.' }
            foreach ($change in @($section.Value)) {
                if ($change -isnot [string] -or [string]::IsNullOrWhiteSpace($change)) { throw 'Update-log changes must be nonempty text.' }
                $count++
            }
        }
    }
    if ($count -eq 0) { throw 'Each update-log entry needs at least one change.' }
}
if ($OutputFile) {
    $entry = $releases[0]
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add($entry.title); $lines.Add('')
    foreach ($kind in @('new','improved','fixed')) {
        $section = $entry.PSObject.Properties[$kind]
        if ($null -ne $section -and @($section.Value).Count -gt 0) {
            $lines.Add('### ' + $kind.Substring(0,1).ToUpperInvariant() + $kind.Substring(1)); $lines.Add('')
            foreach ($change in @($section.Value)) { $lines.Add('- ' + $change) }
            $lines.Add('')
        }
    }
    $lines.Add('Download the Windows x64 Setup.exe below, or use Updates in the app. Saved menus and preferences are kept. The installer includes the runtime and source; it is unsigned.')
    [IO.File]::WriteAllLines([IO.Path]::GetFullPath($OutputFile), $lines, [Text.UTF8Encoding]::new($false))
}
Write-Output "Validated $($releases.Count) update-log entries; latest is $Version."
