Shintsugi - Shatterline restored
================================

Brings Shatterline back to life on community servers: the classic lobby and server list, progression saved on your
PC (materials, perks, carried gear), the full armory, after-match screens, squads with Steam friends and new skins.
You play with your own free Steam copy of the game.

The name: Shatterline + kintsugi, the Japanese art of mending broken pottery with gold. Shin-tsugi (shin = new,
tsugi = joining): the broken game joined together again, mostly as it was, with a little gold added.

Perks are earned on purpose: agents, outfits and weapons are unlocked, but perk levels are bought with the
materials from successful expeditions, as in the original game. New players get a starter wallet.

The mod works on any server. On servers that also run the Shintsugi server part (all [UA][Surge9] servers do),
the new skins show in matches and your badge is shown to others; elsewhere you appear in the default outfit.

A fan-made project, not affiliated with or endorsed by Frag Lab.


INSTALL (2 minutes)
-------------------
1. Install Shatterline from Steam (free). Start it once, then close it.
2. Unpack this archive anywhere (for example to your Desktop). You get a folder named shintsugi_mod.
3. In that folder, double-click Install.cmd.
   It finds the game by itself and copies two files into the game folder.
   If Windows asks "Do you want to run this file?", click Run.
4. Start Shatterline from Steam as usual. Open Play and pick a server.

That's all. No launch options, nothing else to set.

Install by hand instead (if Install.cmd doesn't work for you):
   In Steam right-click Shatterline > Manage > Browse local files.
   Copy the Bin and game01 folders from the shintsugi_mod folder into that folder.
   Windows asks about files with the same name: choose "Replace".
   If game01 has a pak from an earlier version of this mod (s9_client_mods.pak), delete it.


UNINSTALL
---------
Double-click Uninstall.cmd. The game is back to normal.
Your progress stays in %LOCALAPPDATA%\Shatterline (delete that folder to start over). offline_profile.json.bak
there is your progress from before the last game session: if a session went wrong, close the game and copy it over
offline_profile.json.


WHAT'S INSIDE
-------------
Bin\version.dll              keeps your progress in %LOCALAPPDATA%\Shatterline\offline_profile.json and turns on
                             the mod's lobby, server list and screens. It only works with Shatterline.exe and only
                             touches its own folder. It has no internet code of its own: squads and badges go
                             through Steam's lobby and friends features.
game01\shintsugi.pak         the mod's game data: lobby pages, scripts, skins, texts.
Install.cmd / Uninstall.cmd  copy / remove those two files.
CHANGELOG.txt                what changed in each version, and known issues.
CollectLogs.cmd              puts the logs a bug report needs into shintsugi_logs.zip (see LOGS).
SHA256SUMS.txt               checksums of the files, to check your download.

Some antivirus programs warn about version.dll because it changes how the game runs. That is what every game mod
DLL does. If yours blocks it, allow the file or use the "by hand" install above.


PROBLEMS
--------
- No servers in the list: make sure Install.cmd said "Done" and start the game from Steam. The servers are in
  Europe; ping from other regions is higher.
- All heroes are locked, or the old menu shows up: the files are not in the game folder. Run Install.cmd again.
- Materials show 0 after a game update: open %LOCALAPPDATA%\Shatterline\offline_profile.log.
  "different game build" means the mod has to be updated for the new game version.
- Stuck on the start screen: run Uninstall.cmd to get the normal game back, and report it.

LOGS (errors and info)
   %LOCALAPPDATA%\Shatterline\offline_profile.log
      the mod's own log of the last start: game build check, materials and rewards, squads, badges
   %USERPROFILE%\Saved Games\Shatterline\<folder>\user\log\Game.log
      the game's log of the last start; search for [Error] and [Warning]. The mod's lines start with
      [servers], [squad] or [stub]. <folder> is a code-named folder such as PUBE518Q02KF4; if there are
      several, use the one with the newest Game.log. Earlier starts are in LogBackups next to it.
Paste a path into the Explorer address bar to open it. When you report a problem, close the game and double-click
CollectLogs.cmd: it puts these logs into shintsugi_logs.zip next to it (or on your Desktop). Attach that zip.


Your own launch options still work: an option you set in Steam wins over the mod's one.
