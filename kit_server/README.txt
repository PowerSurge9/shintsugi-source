Shintsugi - Shatterline restored: SERVER PART
=============================================

For people who run a Shatterline Dedicated Server. Players don't need this file: the player mod is enough to play
on any server.

game01\shintsugi_server.pak adds to the server:
- Player icon and badge of every player with the mod, sent to the others (MVP screen, "killed by" screen).
- Expedition fixes: MULE supply drops and Red Salt keep working in every session of a long-running server, the
  boss-portal chain weapon and wave grenades deal damage again, respawn points stay near the living players, escort
  weak points work.
- The mod's own items that players can wear (the badges), so the server accepts them.

The new skins are NOT in this file: each skin pack is a separate download with its own server part (see SKIN PACKS).


INSTALL
-------
The server reads its paks only when it starts, and a pak in use can't be replaced. So:
1. Stop every server instance that runs from this server folder (close their windows). If you use an
   auto-restart loop, stop the loop too, or it starts the server again before you are done.
2. Double-click InstallServer.cmd. It looks for "Shatterline Dedicated Server" in your Steam libraries; if your
   server is somewhere else (installed with SteamCMD), drag the server folder (the one with Bin.Release.Dedicated
   and game01) into its window. It copies game01\shintsugi_server.pak into the server's game01 folder and renames
   a server pak of an earlier version (s9_server_mods.pak) to .old, since the server loads every game01\*.pak and
   two versions would mix.
   By hand instead: copy game01\shintsugi_server.pak into the server's game01 folder (the one with assets.pak and
   gamedata.pak) and rename or delete game01\s9_server_mods.pak if there is one.
3. Start the servers again. The server log shows "Opening pak file @assets@\shintsugi_server.pak".

Update: the same steps with the new version. Uninstall: stop the servers, double-click UninstallServer.cmd (or
delete game01\shintsugi_server.pak).


SKIN PACKS
----------
Each skin pack (optional files "Skin pack - ..." on the mod page) holds both halves: the players' file and, in
its server folder, the server's file game01\shintsugi_skins_<pack>_server.pak (item files only, no textures, a
few KB). Install it with the pack's server\InstallServer.cmd, the same steps as above.
- InstallServer.cmd also lists the pack's models in the server's asset catalog (game01\assetcatalog.pak, the index
  the server finds models in; its own is kept as assetcatalog.pak.orig and put back when no pack with models is
  left). Use it rather than copying a pack's server file by hand: a server whose catalog doesn't list a pack's
  whole outfit can't register hits on players wearing it.
- After a server update (SteamCMD app_update or validate), stop the servers and run InstallServer.cmd again: an
  update can put the server's own catalog back.
- A server checks every item a player brings against the items it knows and drops the unknown ones. Without a
  pack's server file, players wearing that pack's skins appear in the default look to everyone in the match.
- Install the server part of every pack your players use; it costs nothing to have them all. The packs don't
  depend on each other, and a pack's server file works with or without shintsugi_server.pak.
- Players also need the pack themselves to see its skins on others; that is their side, not the server's.
- Updating shintsugi_server.pak leaves the packs' server files in place, and the other way round.


KILL CAM
--------
Kill cam after PvP matches: add +highlight_enabled 1 to the PvP server's command line, e.g.
   Shatterline_Server.exe +sv_port 30093 --server_config game_server_config_pvp.json --community +highlight_enabled 1


Checksums of the files: SHA256SUMS.txt.
A fan-made project, not affiliated with or endorsed by Frag Lab.
