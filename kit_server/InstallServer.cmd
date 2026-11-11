@echo off
rem Shintsugi - installer for a Shatterline Dedicated Server. Double-click it: it copies every pak of the game01
rem folder next to it (shintsugi_server.pak of the server part, or shintsugi_skins_<pack>_server.pak of a skin pack)
rem into the server's game01 folder. The same file is in the server part and in every skin pack's server folder.
rem The server folder (the one with Bin.Release.Dedicated and game01) is looked up in the Steam libraries; it can also
rem be given: drop the server folder onto this file, or
rem   InstallServer.cmd "X:\...\Shatterline Dedicated Server"
setlocal
title Shintsugi - server install
echo.
echo   Shintsugi - server install
echo   ==========================
echo.
if not exist "%~dp0game01\*.pak" (
  echo No server pak next to this file: the game01 folder is missing.
  echo Unpack the whole archive first, then run InstallServer.cmd from the unpacked folder.
  goto end
)
set "SRV=%~1"
if not defined SRV call :find
if not defined SRV (
  echo The Shatterline Dedicated Server was not found in your Steam libraries.
  echo Drag the server folder ^(the one with Bin.Release.Dedicated and game01^) into this window and press Enter.
  echo.
  set /p "SRV=Server folder: "
)
if defined SRV set "SRV=%SRV:"=%"
if not defined SRV goto notfound
if "%SRV:~-1%"=="\" set "SRV=%SRV:~0,-1%"
if not exist "%SRV%\Bin.Release.Dedicated\Shatterline_Server.exe" goto notfound
if not exist "%SRV%\game01\" goto notfound

rem The server reads its paks only at start and a pak in use can't be replaced
tasklist /fi "imagename eq Shatterline_Server.exe" | find /i "Shatterline_Server.exe" >nul && (
  echo A Shatterline server is running. Stop every server instance first, and its auto-restart loop if you
  echo use one ^(or it starts the server again^), then run InstallServer.cmd again.
  goto end
)
echo Server folder: "%SRV%"
echo.
rem An earlier version of the server part had another name; the server loads every game01\*.pak, so two versions
rem would mix: the old one is renamed to .old
if exist "%~dp0game01\shintsugi_server.pak" if exist "%SRV%\game01\s9_server_mods.pak" (
  move /y "%SRV%\game01\s9_server_mods.pak" "%SRV%\game01\s9_server_mods.pak.old" >nul || goto copyfail
  echo Old version game01\s9_server_mods.pak renamed to s9_server_mods.pak.old
)
for %%P in ("%~dp0game01\*.pak") do (
  copy /y "%%~fP" "%SRV%\game01\%%~nxP" >nul || goto copyfail
  echo Installed game01\%%~nxP
)
if not exist "%SRV%\game01\shintsugi_server.pak" (
  echo.
  echo Note: the Shintsugi server part ^(game01\shintsugi_server.pak^) is not installed on this server.
  echo The skin pack works without it, but badges and the expedition fixes need it ^(optional file
  echo "Shintsugi Server Mod" on the mod page^).
)
call :catalog || goto catalogfail
echo.
echo Done. Start the servers again. Their log shows one "Opening pak file" line per installed pak.
goto end

:catalogfail
echo.
echo The paks are installed, but the server's asset catalog could not be updated (the message above says why).
echo Without it the server can't load the new outfits' models: players wearing one can't be hit.
echo Please report the message on the mod page.
goto end

:notfound
echo.
echo That is not a Shatterline Dedicated Server folder (it must contain Bin.Release.Dedicated\Shatterline_Server.exe
echo and game01).
goto end

:copyfail
echo.
echo Could not copy the files into "%SRV%\game01".
echo Make sure every server is stopped, or copy the files of this folder's game01 into the server's game01 by hand.

:end
echo.
pause
exit /b 0

rem Lists the models of the installed Shintsugi paks in the server's asset catalog (game01\assetcatalog.pak; the
rem game's own one is kept as assetcatalog.pak.orig), see catalog\UpdateCatalog.ps1
:catalog
echo.
echo Updating the server's asset catalog (the index the server finds models in)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0catalog\UpdateCatalog.ps1" -Game01 "%SRV%\game01"
exit /b %errorlevel%

rem Looks for "<library>\steamapps\common\Shatterline Dedicated Server" in the Steam folder and every library of
rem its libraryfolders.vdf (a server installed with SteamCMD elsewhere is given by hand)
:find
set "STEAM="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find "SteamPath"') do set "STEAM=%%B"
if not defined STEAM exit /b 0
set "STEAM=%STEAM:/=\%"
call :try "%STEAM%"
if not exist "%STEAM%\steamapps\libraryfolders.vdf" exit /b 0
for /f "tokens=1,*" %%A in ('findstr /c:"\"path\"" "%STEAM%\steamapps\libraryfolders.vdf"') do call :try %%B
exit /b 0

:try
if defined SRV exit /b 0
set "LIB=%~1"
set "LIB=%LIB:\\=\%"
if exist "%LIB%\steamapps\common\Shatterline Dedicated Server\Bin.Release.Dedicated\Shatterline_Server.exe" set "SRV=%LIB%\steamapps\common\Shatterline Dedicated Server"
exit /b 0
