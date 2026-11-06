#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$NotesFile,
    [ValidateSet('Justice','Bassem')][string]$ApprovedBy,
    [string]$ApprovedNotesSha256,
    [string]$Repository = 'justicefrangipane/Tablet-Pro-Radial',
    [string]$Gh = 'gh',
    [switch]$Publish
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function GitHub([string[]]$Arguments) {
    $result = & $Gh @Arguments
    if ($LASTEXITCODE -ne 0) { throw "GitHub command failed ($LASTEXITCODE)." }
    return $result
}
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version = [regex]::Match((Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(TabletProRadial VERSION (\d+\.\d+\.\d+)').Groups[1].Value
$tag = 'v' + $version
& (Join-Path $PSScriptRoot 'update-history.ps1') -Version $version
if (-not $NotesFile) {
    $notesDirectory = Join-Path $root 'build\release-notes'
    New-Item -ItemType Directory -Path $notesDirectory -Force | Out-Null
    $NotesFile = Join-Path $notesDirectory "$version.md"
    & (Join-Path $PSScriptRoot 'update-history.ps1') -Version $version -OutputFile $NotesFile
}
if (-not $ApprovedBy -or $ApprovedNotesSha256 -notmatch '^[a-fA-F0-9]{64}$') {
    throw 'Share these exact notes with Justice and obtain Justice or Bassem approval before any upload. Record ApprovedBy and ApprovedNotesSha256 only after that approval.'
}
if ((Get-FileHash -LiteralPath $NotesFile -Algorithm SHA256).Hash -ine $ApprovedNotesSha256) {
    throw 'Release notes changed after approval. Share the updated notes and obtain approval again.'
}
$setup = Join-Path $root "dist\Tablet-Pro-Radial-$version-Windows-x64-Setup.exe"
$checksums = Join-Path $root "dist\Tablet-Pro-Radial-$version-SHA256SUMS.txt"
$source = Join-Path $root "dist\Tablet-Pro-Radial-$version\Tablet-Pro-Radial-$version-Source.zip"
foreach ($path in @($setup,$checksums,$source,$NotesFile)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing release file: $path" }
}
$expectedHash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
if ((Get-Content -LiteralPath $checksums -Raw).Trim() -ne ($expectedHash + '  ' + [IO.Path]::GetFileName($setup))) { throw 'Installer checksum file is stale.' }
$config = Get-Content -LiteralPath (Join-Path $root "dist\Tablet-Pro-Radial-$version\update-source.json") -Raw | ConvertFrom-Json
if ($config.repository -cne $Repository) { throw 'Packaged repository does not match the publication target.' }
Push-Location $root
try {
    $status = git status --porcelain
    if ($LASTEXITCODE -ne 0 -or $status) { throw 'Commit and review all source changes before publishing.' }
    $head = git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source commit.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($source)
    try {
        $tracked = @(git ls-files)
        if ($LASTEXITCODE -ne 0 -or $tracked.Count -ne $archive.Entries.Count) { throw 'Source archive does not match the reviewed repository.' }
        foreach ($relative in $tracked) {
            $entry = $archive.GetEntry('Tablet-Pro-Radial/' + $relative)
            if (-not $entry) { throw "Source archive is missing $relative." }
            $stream = $entry.Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $embedded = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
            finally { $stream.Dispose(); $sha.Dispose() }
            if ($embedded -ne (Get-FileHash -LiteralPath (Join-Path $root $relative) -Algorithm SHA256).Hash) {
                throw "Source changed after packaging: $relative. Rebuild before publishing."
            }
        }
    } finally { $archive.Dispose() }
    $remote = GitHub @('api',"repos/$Repository/commits/$head",'--jq','.sha')
    if ($remote -ne $head) { throw 'Push the reviewed source commit before publishing.' }
    $null = GitHub @('release','create',$tag,'--repo',$Repository,'--target',$head,'--draft','--title',"Tablet Pro Radial $version",'--notes-file',(Resolve-Path -LiteralPath $NotesFile).ProviderPath,$setup,$source,$checksums)
    # A draft's tag may not exist yet. Resolve its API URL through the CLI,
    # which supports unpublished releases, then inspect the REST asset digests.
    $releaseApi = GitHub @('release','view',$tag,'--repo',$Repository,'--json','apiUrl','--jq','.apiUrl')
    $release = (GitHub @('api',$releaseApi)) | ConvertFrom-Json
    foreach ($path in @($setup,$source,$checksums)) {
        $asset = @($release.assets | Where-Object name -CEQ ([IO.Path]::GetFileName($path)))
        $digest = 'sha256:' + (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($asset.Count -ne 1 -or $asset[0].state -ne 'uploaded' -or $asset[0].size -ne (Get-Item -LiteralPath $path).Length -or $asset[0].digest -cne $digest) {
            throw 'Uploaded asset verification failed. Release remains a draft.'
        }
    }
    if ($Publish) {
        $null = GitHub @('release','edit',$tag,'--repo',$Repository,'--draft=false','--latest')
        $latest = (GitHub @('api',"repos/$Repository/releases/latest")) | ConvertFrom-Json
        if ($latest.tag_name -cne $tag -or $latest.draft -or $latest.prerelease) { throw 'Latest stable release verification failed.' }
        Write-Output $latest.html_url
    } else {
        Write-Output "Uploaded and verified draft $tag. Publish it from GitHub when ready."
    }
} finally { Pop-Location }
