Shintsugi - Shatterline restored
================================

Brings Shatterline back to life on community servers: the classic lobby and server list, progression saved on your
PC (materials, perks, carried gear), the full armory, after-match screens, squads with Steam friends, and new
skins in optional skin packs. You play with your own Steam copy of the game.

The name: Shatterline + kintsugi, the Japanese art of mending broken pottery with gold. Shin-tsugi (shin = new,
tsugi = joining): the broken game joined together again, mostly as it was, with a little gold added.

Perks are earned on purpose: agents, outfits and weapons are unlocked, but perk levels are bought with the
materials from successful expeditions, as in the original game. New players get a starter wallet.

The mod works on any server. On servers that also run the Shintsugi server part, the new skins show in matches
and your badge is shown to others; elsewhere you appear in the default outfit.

A fan-made project, not affiliated with or endorsed by Frag Lab.


INSTALL (2 minutes)
-------------------
1. Install Shatterline from your Steam library. Start it once, then close it.
2. Unpack this archive anywhere (for example to your Desktop). You get a folder named shintsugi_mod.
3. In that folder, double-click Install.cmd.
   It finds the game by itself and copies the mod into the game folder.
   If Windows asks "Do you want to run this file?", click Run.
4. Start Shatterline from Steam as usual. Open Play and pick a server.
5. Optional: add skin packs for the new skins (see SKIN PACKS).

That's all. No launch options, nothing else to set.

Install by hand instead (if Install.cmd doesn't work for you):
   In Steam right-click Shatterline > Manage > Browse local files.
   Copy the Bin and game01 folders from the shintsugi_mod folder into that folder.
   Windows asks about files with the same name: choose "Replace".
   If game01 has a pak from an earlier version of this mod (s9_client_mods.pak), delete it.


SKIN PACKS (new skins, optional)
--------------------------------
The new skins are not in this archive. They come as skin packs: separate downloads on the same mod page
(optional files "Skin pack - ..."), one per set of skins. Take the ones you like:
1. Install this mod first (above).
2. Download a skin pack and unpack it. You get a folder named shintsugi_skins_<pack>.
3. In that folder, double-click Install.cmd. It copies one file, game01\shintsugi_skins_<pack>.pak, into the game.
4. Start the game: the pack's skins are in the customization screen of the agent, already yours.
To drop a pack, run its Uninstall.cmd (or delete its file from game01). Its README.txt has the details.

The packs when this version came out:
Brisa Camo            Brisa: Winter, Hexagon, Pixel, Jungle, Trench, Forest and Lush Turtleneck + Leggings
Brisa Outfits         Brisa: Star Ray Outfit, Libiac Suit, Libiac Mask, Celestial Empire Turtleneck + Leggings
Malva Gilded          Malva: Gilded Shellguard Jacket I and II, Gilded Overalls, Gilded Polar Hat I and II
Mongoose Bloodstream  Mongoose: Bloodstream Cyber Soldier
Pill Camo             Pill: Winter and Abstract Vest + Pants
Pill Outfits          Pill: Black Ops and Crimson Vest + Pants, Blackgold Vest, Pants and Cap, Bulldog Beanie

What to expect:
- You always see your pack's skins yourself, in your lobby and the customization screen.
- Other players see them on you only when they have the same pack installed. Without it, the mod shows them a
  default look in its place (for a whole outfit the agent's default outfit, for a hat, mask or hair the agent's
  default one); nothing breaks for them and they can hit you as usual. If you want your friends to see your
  skins, tell them which packs you use.
- Shintsugi Lite players can't show what they don't have: a whole outfit of a pack makes your body invisible to
  them, and a missing hat, mask or hair leaves that part empty. Hits still count.
- In matches the server must have the pack's server part too. On other servers, everyone sees you in the
  default look.
- Skin packs need this full mod: they don't work with Shintsugi Lite, nor without the mod's Bin\version.dll.
- Updating the mod keeps your packs. Uninstall.cmd of the mod removes the mod and every skin pack.
- Packs only add their own skins; they change nothing of the game's own skins.
- The installers list the packs' models in the game's asset catalog (game01\assetcatalog.pak, the index the game
  finds its models in). The game's own catalog is kept as assetcatalog.pak.orig and put back when the last pack
  with models is removed.


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
game01\shintsugi.pak         the mod's game data: lobby pages, scripts, texts.
Install.cmd / Uninstall.cmd  copy / remove those files (Uninstall.cmd also removes every skin pack) and update
                             the game's asset catalog for the skin packs (see SKIN PACKS).
catalog\                     the asset catalog updater the installers run.
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
- After Steam checked the game files or updated the game: run Install.cmd again. Steam may put its own asset
  catalog back, and Install.cmd lists the installed skin packs in it again (your packs and progress stay).

LOGS (errors and info)
   %LOCALAPPDATA%\Shatterline\offline_profile.log
      the mod's own log of the last start: game build check, materials and rewards, squads, badges, skin packs
   %USERPROFILE%\Saved Games\Shatterline\<folder>\user\log\Game.log
      the game's log of the last start; search for [Error] and [Warning]. The mod's lines start with
      [servers], [squad] or [stub]. <folder> is a code-named folder such as PUBE518Q02KF4; if there are
      several, use the one with the newest Game.log. Earlier starts are in LogBackups next to it.
Paste a path into the Explorer address bar to open it. When you report a problem, close the game and double-click
CollectLogs.cmd: it puts these logs into shintsugi_logs.zip next to it (or on your Desktop). Attach that zip.


Your own launch options still work: an option you set in Steam wins over the mod's one.
