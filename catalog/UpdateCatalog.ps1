# Shintsugi - asset catalog update, run by the installers (Install.cmd, Uninstall.cmd, InstallServer.cmd,
# UninstallServer.cmd) after they copy or remove paks.
# The game finds a slice or model by looking its path up in game01\assetcatalog.pak, the index the developers' build
# tools wrote for every game update. A file the catalog doesn't list is not found even when a pak holds it. This
# script lists the models and slices of the installed Shintsugi paks (game01\shintsugi*.pak) in the catalog:
# - the first time, the game's own catalog is kept as game01\assetcatalog.pak.orig;
# - every run builds assetcatalog.pak again from that copy plus the paks installed now, so a removed pak's entries
#   go away with it;
# - with no Shintsugi models left, the game's own catalog is put back and the copy removed;
# - a catalog without our marker file (shintsugi_catalog.txt) is the game's own: after a Steam update or a file
#   check replaced ours, it becomes the new copy.
param([Parameter(Mandatory)][string]$Game01)
$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'ObjectStream.cs'), (Join-Path $PSScriptRoot 'AssetCatalog.cs')
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$cat = Join-Path $Game01 'assetcatalog.pak'
$orig = "$cat.orig"
$tmp = "$cat.tmp"
$marker = 'shintsugi_catalog.txt'
if (-not (Test-Path -LiteralPath $cat)) { throw "No asset catalog: $cat" }

function Test-Ours([string]$pak) {
    $z = [IO.Compression.ZipFile]::OpenRead($pak)
    try { $null -ne $z.GetEntry($marker) } finally { $z.Dispose() }
}

# models and slices of the installed Shintsugi paks
$files = New-Object 'System.Collections.Generic.List[CatalogFile]'
foreach ($pak in Get-ChildItem -LiteralPath $Game01 -Filter 'shintsugi*.pak' -File | Sort-Object Name) {
    $z = [IO.Compression.ZipFile]::OpenRead($pak.FullName)
    try {
        foreach ($e in $z.Entries) {
            $type = [AssetCatalog]::TypeOf($e.FullName)
            if ($null -eq $type) { continue }
            $f = New-Object CatalogFile; $f.Path = $e.FullName; $f.Size = $e.Length; $f.Type = $type
            $files.Add($f)
        }
    } finally { $z.Dispose() }
}

$ours = Test-Ours $cat
if ($ours -and -not (Test-Path -LiteralPath $orig)) {
    throw "assetcatalog.pak was changed by Shintsugi but the game's own copy (assetcatalog.pak.orig) is gone. In Steam, verify the integrity of the game files (it restores the catalog), then run this installer again."
}

if (-not $files.Count) {
    if ($ours) {
        Move-Item -LiteralPath $orig -Destination $cat -Force
        'Asset catalog: the game''s own catalog is back (no Shintsugi models installed).'
    } elseif (Test-Path -LiteralPath $orig) {
        Remove-Item -LiteralPath $orig -Force   # a copy of an older stock catalog, no longer needed
    }
    return
}

# the game's own catalog: the kept copy, or the current file when it is not ours (first run, or after a game update)
if (-not $ours) { Copy-Item -LiteralPath $cat -Destination $orig -Force }
$z = [IO.Compression.ZipFile]::OpenRead($orig)
try {
    $entries = @($z.Entries | Where-Object { $_.FullName -notlike '*/' })
    if ($entries.Count -ne 1) { throw "Unexpected asset catalog: $($entries.Count) files in assetcatalog.pak.orig" }
    $name = $entries[0].FullName
    $ms = New-Object IO.MemoryStream ([int]$entries[0].Length)
    $s = $entries[0].Open(); $s.CopyTo($ms); $s.Dispose()
    $xml = $ms.ToArray(); $ms.Dispose()
} finally { $z.Dispose() }

$added = 0
$new = [AssetCatalog]::Add($xml, $files, [ref]$added)
$known = [AssetCatalog]::Paths($new)
$missing = @($files | Where-Object { -not $known.Contains($_.Path.Replace('\', '/').ToLowerInvariant()) })
if ($missing) { throw "Catalog check failed: $($missing.Count) files not listed after the update" }

$fs = [IO.File]::Create($tmp)
try {
    $zip = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
    $e = $zip.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
    $s = $e.Open(); $s.Write($new, 0, $new.Length); $s.Dispose()
    $e = $zip.CreateEntry($marker)
    $w = New-Object IO.StreamWriter($e.Open())
    $w.WriteLine('Asset catalog with the models of the installed Shintsugi paks; the game''s own is assetcatalog.pak.orig.')
    foreach ($f in $files) { $w.WriteLine($f.Path) }
    $w.Dispose()
    $zip.Dispose()
} finally { $fs.Dispose() }
Move-Item -LiteralPath $tmp -Destination $cat -Force
"Asset catalog: $added models and slices of the Shintsugi paks listed (the game's own catalog is kept as assetcatalog.pak.orig)."
