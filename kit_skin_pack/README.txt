Shintsugi skin pack: {TITLE}
{UNDERLINE}

Version {VERSION}. A set of new skins for Shatterline with the Shintsugi mod:
   {CONTENTS}

A fan-made project, not affiliated with or endorsed by Frag Lab.


WHAT YOU NEED
-------------
- Shatterline from your Steam library, with the full Shintsugi mod installed (the main file on the same page).
  The mod's Bin\version.dll is what makes the pack's skins yours. Install the mod first, then the pack.
- Shintsugi Lite can't use skin packs (it has no DLL). WHO SEES THE SKINS says what Lite players see.


INSTALL (players)
-----------------
1. Unpack this archive anywhere. You get a folder named shintsugi_skins_{PACK}.
2. In that folder, double-click Install.cmd. It finds the game by itself and copies one file:
      game01\shintsugi_skins_{PACK}.pak  ->  <Shatterline>\game01\
   Then it adds the pack's models to the game's asset catalog (see GOOD TO KNOW); that can take a minute.
   If Windows asks "Do you want to run this file?", click Run.
3. Start Shatterline from Steam. The skins are in the customization screen of the agent, already yours.

By hand instead: in Steam right-click Shatterline > Manage > Browse local files, and copy the game01 folder of
this archive into that folder (choose "Replace" if Windows asks). The mod then finds the pack's models by itself.

Uninstall: double-click Uninstall.cmd (or delete game01\shintsugi_skins_{PACK}.pak from the game folder). Only
this pack goes; the mod, other packs and your progress stay.
Update: run the new version's Install.cmd. Updating the Shintsugi mod itself leaves your packs in place; the mod's
Uninstall.cmd removes every skin pack.


WHO SEES THE SKINS
------------------
- You: always, in your lobby and the customization screen, on any server.
- Other players: only when they have this pack installed too. Without it, the mod shows them a default look in
  its place: for a whole outfit, the agent's default outfit; for a hat, mask or hair, the agent's default one.
  Nothing breaks for them, and they can hit you as usual.
- Shintsugi Lite players (no DLL) can't show what they don't have: a whole outfit of a pack makes the wearer's
  body invisible to them, and a missing hat, mask or hair leaves that part empty. Hits still count.
- In matches: the server must have this pack's server part (see below). A server checks every item a player brings
  against the items it knows and drops the unknown ones, so on a server without it everyone sees you in the default
  look.


INSTALL (server hosts)
----------------------
Players don't need this part. For a Shatterline Dedicated Server, the server folder of this archive holds the
pack's server part: server\game01\shintsugi_skins_{PACK}_server.pak (only the item files, no textures).
1. Stop every server instance that runs from the server folder, and its auto-restart loop if you use one. The
   server reads its paks only at start, and a pak in use can't be replaced.
2. Double-click server\InstallServer.cmd. It looks for the server in your Steam libraries; if it isn't there
   (a server installed with SteamCMD), drag the server folder (the one with Bin.Release.Dedicated and game01) into
   its window. It copies server\game01\shintsugi_skins_{PACK}_server.pak into the server's game01 folder and
   adds the pack's models to the server's asset catalog. Use InstallServer.cmd rather than copying the file by
   hand: a server whose catalog doesn't list a pack's whole outfit can't register hits on players wearing it.
3. Start the servers. The log shows "Opening pak file @assets@\shintsugi_skins_{PACK}_server.pak".
Uninstall: stop the servers, then server\UninstallServer.cmd.
After a server update (SteamCMD app_update or validate), run server\InstallServer.cmd again: an update can put
the server's own catalog back.
The pack's server part works on its own, but badges and the expedition fixes need the Shintsugi Server Mod.


WHAT'S INSIDE
-------------
game01\shintsugi_skins_{PACK}.pak                 the skins for players: items, materials, textures, icons, names
server\game01\shintsugi_skins_{PACK}_server.pak   the same items for a dedicated server, without textures
Install.cmd / Uninstall.cmd                       copy / remove the players' file, update the asset catalog
server\InstallServer.cmd / UninstallServer.cmd    copy / remove the server file, update the server's catalog
catalog\, server\catalog\                         the asset catalog updater the installers run
SHA256SUMS.txt                                    checksums of the files, to check your download


GOOD TO KNOW
------------
- A pack only adds its own skins. It changes no stock skin, item or texture of the game.
- The asset catalog (game01\assetcatalog.pak) is the game's index of its models. The installers rebuild it from
  the game's own catalog, kept as game01\assetcatalog.pak.orig, plus the models of the installed Shintsugi paks.
  When no Shintsugi pak with models is left, the game's own catalog is put back.
- After Steam checks the game files or updates the game, run Install.cmd again (the mod's or any pack's: it lists
  every installed pack).
- Delete a pack and its skins leave the customization screen; the rest of your progress stays.
- If something looks wrong, %LOCALAPPDATA%\Shatterline\offline_profile.log lists the skin packs the mod loaded.
