@echo off
rem Shintsugi skin pack - installer for players. Double-click it: it finds Shatterline in the Steam libraries and
rem copies the skin pack of this folder (game01\shintsugi_skins_<pack>.pak) into the game's game01 folder.
rem Needs the full Shintsugi mod in that game folder (its Bin\version.dll loads the packs; Lite has none).
rem The game folder can also be given: drop the Shatterline folder onto this file, or
rem   Install.cmd "X:\...\steamapps\common\Shatterline"
setlocal
title Shintsugi skin pack - install
echo.
echo   Shintsugi skin pack - install
echo   =============================
echo.
if not exist "%~dp0game01\shintsugi_skins_*.pak" (
  echo No skin pack next to this file: game01\shintsugi_skins_*.pak is missing.
  echo Unpack the whole archive first, then run Install.cmd from the unpacked folder.
  goto end
)
set "GAME=%~1"
if not defined GAME call :find
if not defined GAME (
  echo Shatterline was not found in your Steam libraries.
  echo In Steam: right-click Shatterline, Manage, Browse local files.
  echo Drag that folder into this window and press Enter.
  echo.
  set /p "GAME=Shatterline folder: "
)
if defined GAME set "GAME=%GAME:"=%"
if not defined GAME goto notfound
if "%GAME:~-1%"=="\" set "GAME=%GAME:~0,-1%"
if not exist "%GAME%\Bin\Shatterline.exe" goto notfound
if not exist "%GAME%\game01\" goto notfound

tasklist /fi "imagename eq Shatterline.exe" | find /i "Shatterline.exe" >nul && (
  echo Shatterline is running. Close the game, then run Install.cmd again.
  goto end
)
echo Game folder: "%GAME%"
echo.
rem The pack's skins are made yours by the mod's version.dll: without the full mod a pack does nothing
set "MOD="
if exist "%GAME%\Bin\version.dll" if exist "%GAME%\game01\shintsugi.pak" (
  findstr /m /c:"Shatterline Offline Profile" "%GAME%\Bin\version.dll" >nul && set "MOD=1"
)
if not defined MOD (
  echo The Shintsugi mod is not installed in this game folder.
  echo Skin packs need the full Shintsugi mod: download it from the same page, run its Install.cmd,
  echo then run this Install.cmd again. Shintsugi Lite can't use skin packs.
  goto end
)
for %%P in ("%~dp0game01\shintsugi_skins_*.pak") do (
  copy /y "%%~fP" "%GAME%\game01\%%~nxP" >nul || goto copyfail
  echo Installed skin pack game01\%%~nxP
)
call :catalog || goto catalogfail
echo.
echo Done. Start Shatterline from Steam as usual: the pack's skins are in the customization screen.
echo Other players see them on you when they have this pack too (see README.txt).
goto end

:notfound
echo.
echo That is not the Shatterline folder (it must contain Bin\Shatterline.exe and game01).
goto end

:copyfail
echo.
echo Could not copy the skin pack into "%GAME%\game01".
echo Right-click Install.cmd and choose "Run as administrator", or copy the file from this folder's game01
echo into the game's game01 folder by hand (see README.txt).
goto end

:catalogfail
echo.
echo The pack is installed, but the game's asset catalog could not be updated (the message above says why).
echo The mod still finds the pack's models by itself, so the skins should show. Please report the message
echo on the mod page.

:end
echo.
pause
exit /b 0

rem Lists the models of the installed Shintsugi paks in the game's asset catalog (game01\assetcatalog.pak; the
rem game's own one is kept as assetcatalog.pak.orig), see catalog\UpdateCatalog.ps1
:catalog
echo.
echo Updating the game's asset catalog (the index the game finds models in), this can take a minute...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0catalog\UpdateCatalog.ps1" -Game01 "%GAME%\game01"
exit /b %errorlevel%

rem Looks for "<library>\steamapps\common\Shatterline" in the Steam folder and in every library of its
rem libraryfolders.vdf (paths there have doubled backslashes)
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
if defined GAME exit /b 0
set "LIB=%~1"
set "LIB=%LIB:\\=\%"
if exist "%LIB%\steamapps\common\Shatterline\Bin\Shatterline.exe" set "GAME=%LIB%\steamapps\common\Shatterline"
exit /b 0
