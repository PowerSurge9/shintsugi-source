# Shintsugi – source code

Source of the compiled code and the scripts in **Shintsugi**, a fan-made mod that restores Shatterline (Steam)
after its online services were shut down: https://www.nexusmods.com/shatterline/mods/2

Shintsugi 新継ぎ = Shatterline + kintsugi: the broken game joined together again.

This repository contains everything in the downloads that is code: the DLL, the `.cmd` scripts and the asset
catalog updater (a PowerShell script with two C# files). The other files are game data in the game's own resource
format, `.pak` (Lumberyard pak, zip-based): UI layouts, Lua scripts, item definitions, skins, textures and texts.
They are not part of this repository.

Current version: **2026.11.11**.

## The downloads and their code

| Download (Nexus file) | Code in it | Data in it |
|---|---|---|
| `Shintsugi_2026.11.11.zip` (main file) | `Bin\version.dll` ([offline_profile](offline_profile)), `Install.cmd`, `Uninstall.cmd`, `CollectLogs.cmd` ([kit](kit)), `catalog\` ([catalog](catalog)) | `game01\shintsugi.pak` |
| `Shintsugi_Server_2026.11.11.zip` (Shintsugi Server Mod) | `InstallServer.cmd`, `UninstallServer.cmd` ([kit_server](kit_server)), `catalog\` ([catalog](catalog)) | `game01\shintsugi_server.pak` |
| `Shintsugi_Skins_<pack>_2026.11.11.zip` (six skin packs) | `Install.cmd`, `Uninstall.cmd` ([kit_skin_pack](kit_skin_pack)), `server\InstallServer.cmd`, `server\UninstallServer.cmd` ([kit_server](kit_server)), `catalog\` and `server\catalog\` ([catalog](catalog)) | `game01\shintsugi_skins_<pack>.pak`, `server\game01\shintsugi_skins_<pack>_server.pak` |
| `Shintsugi_Lite_2026.11.11.zip` (Shintsugi Lite) | none | `game01\shintsugi.pak` |

Every download also has a `README.txt` and a `SHA256SUMS.txt` (the checksums of its files). The skin packs hold no
DLL: they need the main file's `version.dll`.

## What's here

| Path | In the downloads as | What it is |
|---|---|---|
| `offline_profile/offline_profile.cpp` | `Bin\version.dll` | The mod's DLL (see below) |
| `offline_profile/version.def` | | Export list: the 17 functions of Windows' `version.dll`, forwarded to the real one |
| `offline_profile/badges.inc` | | The game's badge ids (icons, backgrounds) the DLL knows |
| `offline_profile/build.ps1` | | Build script |
| `scripts/private_keys.ps1`, `custom/private_items.txt` | | Used by `build.ps1 -Public`: badges the public build shows but doesn't own |
| `kit/Install.cmd`, `kit/Uninstall.cmd` | same names (main file) | Copy / remove `Bin\version.dll` and `game01\shintsugi.pak` in the game folder; Uninstall also removes the skin packs |
| `kit/CollectLogs.cmd` | same name (main file) | Zips the mod's and the game's log files for bug reports |
| `kit/README.txt`, `kit/CHANGELOG.txt` | same names (main file) | The player README and changelog |
| `kit_server/InstallServer.cmd`, `kit_server/UninstallServer.cmd` | same names (Server Mod; `server\` in each skin pack) | Copy / remove the server pak(s) in a Shatterline Dedicated Server's `game01` folder; refuse while a server runs |
| `kit_server/README.txt` | `README.txt` (Server Mod) | The server README |
| `kit_skin_pack/Install.cmd`, `kit_skin_pack/Uninstall.cmd` | same names (skin packs) | Copy / remove one `game01\shintsugi_skins_<pack>.pak`; Install refuses without the main mod |
| `kit_skin_pack/README.txt` | `README.txt` (skin packs) | Template: `{TITLE}`, `{PACK}` and the contents are filled in per pack |
| `catalog/UpdateCatalog.ps1`, `catalog/AssetCatalog.cs`, `catalog/ObjectStream.cs` | `catalog\` (main file, Server Mod, skin packs) | The asset catalog updater (see below) |

## What the DLL does

`version.dll` is a proxy DLL. `Shatterline.exe` loads it from its `Bin` folder instead of the Windows one. Every
`version.dll` export jumps to the same function in `%SystemRoot%\System32\version.dll`. On load (only inside
`Shatterline.exe`, and only for the exact game build it was made for; any other build is left untouched):

- adds the game's own launch options for the classic lobby and server browser (`AddLaunchOptions`)
- hooks game functions in memory to keep progression in `%LOCALAPPDATA%\Shatterline\offline_profile.json`:
  wallet, bought perks, carried expedition gear, chosen badge
- shows the after-match screens the offline game skips
- skin packs: makes the items of the installed `shintsugi_skins_*.pak` files owned, adds their icon lists, and
  for a player without a pack shows the hero's default outfit or part in place of the pack's one
- squads with Steam friends through the Steam API (`steam_api64.dll`) the game already loads

It has no network code of its own. It writes files only in `%LOCALAPPDATA%\Shatterline` (profile, its backup, its
log, and generated copies of two game config files in `generated\`), reads the mod's own paks and the game's files
through the game's file system, and touches nothing outside the game process.

## What the asset catalog updater does

`game01\assetcatalog.pak` is the game's index of its models (path -> asset id). The game finds a model only when it
is listed there, so the installers add the models of the Shintsugi paks to it, the way the game's developers
listed their own. `UpdateCatalog.ps1 -Game01 <game01 folder>`:

1. loads `ObjectStream.cs` and `AssetCatalog.cs` with `Add-Type` (they read and write the catalog's binary format,
   AzFramework AssetRegistry in an ObjectStream);
2. keeps the game's own catalog once as `assetcatalog.pak.orig`;
3. writes a new `assetcatalog.pak`: the game's own entries plus one per `.dynamicslice` / `.cgf` in
   `game01\shintsugi*.pak`, and a marker file `shintsugi_catalog.txt` listing them;
4. with no Shintsugi model left (after uninstalling), puts the game's own catalog back.

It only reads and writes files in the `game01` folder it is given. No network access, no other files.

## Building the DLL

1. Download the llvm-mingw toolchain **llvm-mingw-20261020-ucrt-x86_64** (zip) from
   https://github.com/mstorsjo/llvm-mingw/releases and unpack it into `tools\llvm-mingw\` next to `offline_profile`,
   so that `tools\llvm-mingw\llvm-mingw-20261020-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe` exists.
2. In PowerShell, in this folder:
   ```powershell
   .\offline_profile\build.ps1 -Public
   ```
   This builds `offline_profile\out\public\version.dll`, the file shipped in the download.
   Without `-Public` it builds the author's own variant (it also owns the two badges listed in
   `custom\private_items.txt`).

**Comparing with the shipped DLL:** the rebuild has the same size (483,840 bytes for 2026.11.11) and differs in
exactly 7 bytes, both build times: the PE header's TimeDateStamp (offset 0x80) and the time in the log line
`Shatterline Offline Profile starting (built <date> <time>)` (`__DATE__ __TIME__`). Checked 2026-11-11.
Shipped `Bin\version.dll` of 2026.11.11: SHA-256 `949313f58bb53c3fc235787c6e717c413b833a326b00a29252f6a98a72f31bcf`.

The `.cmd` and `.ps1` scripts and the `.cs` files are plain text and run as they are (the `.cs` files are compiled
by PowerShell's `Add-Type` when the updater runs).

## Not affiliated

A fan-made project, not affiliated with or endorsed by Frag Lab.
