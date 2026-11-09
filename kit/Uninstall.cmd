@echo off
rem Shintsugi (Shatterline mod) - uninstaller. Double-click it: it removes the mod's two files from the game folder
rem (Bin\version.dll and game01\shintsugi.pak, plus the paks of earlier versions). Your saved progress is kept.
rem The game folder can also be given: drop the Shatterline folder onto this file.
setlocal
title Shintsugi (Shatterline mod) - uninstall
echo.
echo   Shintsugi (Shatterline mod) - uninstall
echo   =======================================
echo.
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
for %%F in (shintsugi.pak s9_client_mods.pak s9_client_mods.pak.bak s9_client_mods.pak.old zz_custom.pak zz_custom.pak.old) do (
  if exist "%GAME%\game01\%%F" del /f /q "%GAME%\game01\%%F" && echo Removed game01\%%F
)
rem Only this mod's version.dll is removed; another mod's one is put back
if exist "%GAME%\Bin\version.dll" (
  findstr /m /c:"Shatterline Offline Profile" "%GAME%\Bin\version.dll" >nul && del /f /q "%GAME%\Bin\version.dll" && echo Removed Bin\version.dll
)
if exist "%GAME%\Bin\version.dll.other" if not exist "%GAME%\Bin\version.dll" (
  move /y "%GAME%\Bin\version.dll.other" "%GAME%\Bin\version.dll" >nul && echo Put back the other mod's Bin\version.dll
)
echo.
echo Done. The game is back to normal.
echo Your progress is kept in "%LOCALAPPDATA%\Shatterline" (delete that folder to start over).
goto end

:notfound
echo.
echo That is not the Shatterline folder (it must contain Bin\Shatterline.exe).

:end
echo.
pause
exit /b 0

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
