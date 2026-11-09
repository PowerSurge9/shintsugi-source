# Shintsugi – source code

Source of the compiled code and the scripts in **Shintsugi**, a fan-made mod that restores Shatterline (Steam)
after its online services were shut down: https://www.nexusmods.com/shatterline/mods/2

Shintsugi 新継ぎ = Shatterline + kintsugi: the broken game joined together again.

This repository contains everything in the download that is code: the DLL and the three `.cmd` scripts. The other
file in the download, `game01\shintsugi.pak`, is game data in the game's own resource format (Lumberyard .pak,
zip-based): UI layouts, Lua scripts, skins and texts. It is not part of this repository.

## What's here

| Path | In the download as | What it is |
|---|---|---|
| `offline_profile/offline_profile.cpp` | `Bin\version.dll` | The mod's DLL (see below) |
| `offline_profile/version.def` | | Export list: the 17 functions of Windows' `version.dll`, forwarded to the real one |
| `offline_profile/badges.inc` | | The game's badge ids (icons, backgrounds) the DLL knows |
| `offline_profile/build.ps1` | | Build script |
| `scripts/private_keys.ps1`, `custom/private_items.txt` | | Used by `build.ps1 -Public`: badges the public build shows but doesn't own |
| `kit/Install.cmd`, `kit/Uninstall.cmd` | same names | Copy / remove `Bin\version.dll` and `game01\shintsugi.pak` in the game folder |
| `kit/CollectLogs.cmd` | same name | Zips the mod's and the game's log files for bug reports |
| `kit/README.txt`, `kit/CHANGELOG.txt` | same names | The player README and changelog |

## What the DLL does

`version.dll` is a proxy DLL. `Shatterline.exe` loads it from its `Bin` folder instead of the Windows one. Every
`version.dll` export jumps to the same function in `%SystemRoot%\System32\version.dll`. On load (only inside
`Shatterline.exe`, and only for the exact game build it was made for; any other build is left untouched):

- adds the game's own launch options for the classic lobby and server browser (`AddLaunchOptions`)
- hooks game functions in memory to keep progression in `%LOCALAPPDATA%\Shatterline\offline_profile.json`:
  wallet, bought perks, carried expedition gear, chosen badge
- shows the after-match screens the offline game skips, and registers the mod's new models
- squads with Steam friends through the Steam API (`steam_api64.dll`) the game already loads

It has no network code of its own. It writes files only in `%LOCALAPPDATA%\Shatterline` (profile, its backup, its
log), reads the mod's own pak in the game's `game01` folder (to find the new models), and touches nothing outside
the game process.

## Building

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

The build embeds its build time (`__DATE__ __TIME__`, written to the log at start), so a rebuild is not
byte-identical to the shipped DLL. Its size and behaviour are the same.

The `.cmd` scripts are plain text and run as they are.

## Not affiliated

A fan-made project, not affiliated with or endorsed by Frag Lab.
