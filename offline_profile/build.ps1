# Builds version.dll (Shatterline Offline Profile) with the portable llvm-mingw toolchain in ..\tools
#   (default)  out\version.dll         for us and close friends; -Install copies it into the game's Bin
#   -Public    out\public\version.dll  for everyone else (public download, friend zip): knows the private badges of
#              custom\private_items.txt (shows them on players who wear them) but doesn't own them
param([switch]$Install, [switch]$Public)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$cxx = Get-ChildItem "$here\..\tools\llvm-mingw" -Recurse -Filter 'x86_64-w64-mingw32-clang++.exe' | Select-Object -First 1
if (-not $cxx) { throw "llvm-mingw not found under tools\llvm-mingw" }
$out = if ($Public) { "$here\out\public" } else { "$here\out" }
New-Item -ItemType Directory -Force $out | Out-Null
$defines = @()
if ($Public) {
    $keys = & "$here\..\scripts\private_keys.ps1"
    $pattern = ($keys | ForEach-Object { [regex]::Escape($_) }) -join '|'
    # badges.inc lines look like { "bcn_cteha", 0 },
    $names = @(foreach ($line in Get-Content "$here\badges.inc") {
        $m = [regex]::Match($line, '\{\s*"([^"]+)"')
        if ($m.Success -and $m.Groups[1].Value -match $pattern) { $m.Groups[1].Value }
    })
    if ($names.Count -eq 0) { throw "No private badges found in badges.inc" }
    [IO.File]::WriteAllLines("$out\private_badges.inc", ($names | ForEach-Object { "`"$_`"," }))
    "Private badges (shown, not owned): $($names -join ', ')"
    $defines = @('-DPUBLIC_RELEASE')
}
# -static: no libc++/winpthread DLLs next to the game; only Windows system DLLs are needed
& $cxx.FullName -std=c++17 -O2 -Wall -shared -static -s @defines -o "$out\version.dll" `
    "$here\offline_profile.cpp" "$here\version.def"
if ($LASTEXITCODE -ne 0) { throw "build failed" }
"Built $out\version.dll ($((Get-Item "$out\version.dll").Length) bytes)"
if ($Install) {
    if ($Public) { throw "-Install takes the full build, not -Public" }
    $bin = 'D:\Program Files (x86)\Steam\steamapps\common\Shatterline\Bin'
    Copy-Item "$here\out\version.dll" "$bin\version.dll" -Force
    "Installed to $bin"
}
