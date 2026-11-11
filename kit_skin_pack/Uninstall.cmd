@echo off
rem Shintsugi skin pack - uninstaller for players. Double-click it: it removes the skin pack of this folder
rem (game01\shintsugi_skins_<pack>.pak) from the game's game01 folder. The mod and other packs stay.
rem The game folder can also be given: drop the Shatterline folder onto this file.
setlocal
title Shintsugi skin pack - uninstall
echo.
echo   Shintsugi skin pack - uninstall
echo   ===============================
echo.
if not exist "%~dp0game01\shintsugi_skins_*.pak" (
  echo No skin pack next to this file: game01\shintsugi_skins_*.pak is missing, so it is unknown which one to remove.
  echo Run Uninstall.cmd from the unpacked folder of the skin pack.
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

tasklist /fi "imagename eq Shatterline.exe" | find /i "Shatterline.exe" >nul && (
  echo Shatterline is running. Close the game, then run Uninstall.cmd again.
  goto end
)
echo Game folder: "%GAME%"
echo.
set "FOUND="
for %%P in ("%~dp0game01\shintsugi_skins_*.pak") do (
  if exist "%GAME%\game01\%%~nxP" (
    set "FOUND=1"
    del /f /q "%GAME%\game01\%%~nxP" && echo Removed skin pack game01\%%~nxP
  )
)
if not defined FOUND echo This skin pack was not installed.
call :catalog || goto catalogfail
echo.
echo Done. Your progress is kept; the pack's skins are gone from the customization screen.
goto end

:notfound
echo.
echo That is not the Shatterline folder (it must contain Bin\Shatterline.exe).
goto end

:catalogfail
echo.
echo The pack is removed, but the game's asset catalog could not be updated (the message above says why).
echo The game still works. To get the game's own catalog back: in Steam, verify the integrity of the game files.

:end
echo.
pause
exit /b 0

rem Lists the models of the Shintsugi paks still installed in the game's asset catalog; with none left, the game's
rem own catalog is put back (see catalog\UpdateCatalog.ps1)
:catalog
echo.
echo Updating the game's asset catalog (the index the game finds models in), this can take a minute...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0catalog\UpdateCatalog.ps1" -Game01 "%GAME%\game01"
exit /b %errorlevel%

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
