@echo off
rem Shintsugi (Shatterline mod) - installer. Double-click it: it finds Shatterline in the Steam libraries and copies
rem two files into the game folder, Bin\version.dll and game01\shintsugi.pak. Nothing else changes.
rem The game folder can also be given: drop the Shatterline folder onto this file, or
rem   Install.cmd "X:\...\steamapps\common\Shatterline"
setlocal
title Shintsugi (Shatterline mod) - install
echo.
echo   Shintsugi (Shatterline mod) - install
echo   =====================================
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
if not exist "%GAME%\game01\" goto notfound

tasklist /fi "imagename eq Shatterline.exe" | find /i "Shatterline.exe" >nul && (
  echo Shatterline is running. Close the game, then run Install.cmd again.
  goto end
)
echo Game folder: "%GAME%"
echo.
rem Earlier versions of this mod used other pak names (s9_client_mods.pak, zz_custom.pak). The game loads every
rem game01\*.pak, so an old one next to shintsugi.pak would mix two versions: it is renamed to .old
for %%F in (s9_client_mods.pak zz_custom.pak) do (
  if exist "%GAME%\game01\%%F" (
    move /y "%GAME%\game01\%%F" "%GAME%\game01\%%F.old" >nul || goto copyfail
    echo Old version game01\%%F renamed to %%F.old
  )
)
rem The files were unpacked straight into the game folder: they are in place already
if /i "%~dp0"=="%GAME%\" goto done

rem A version.dll from another mod is kept under another name (only one can be loaded)
if exist "%GAME%\Bin\version.dll" (
  findstr /m /c:"Shatterline Offline Profile" "%GAME%\Bin\version.dll" >nul || (
    move /y "%GAME%\Bin\version.dll" "%GAME%\Bin\version.dll.other" >nul || goto copyfail
    echo Another mod's Bin\version.dll was kept as Bin\version.dll.other
  )
)
copy /y "%~dp0Bin\version.dll" "%GAME%\Bin\version.dll" >nul || goto copyfail
copy /y "%~dp0game01\shintsugi.pak" "%GAME%\game01\shintsugi.pak" >nul || goto copyfail
echo Installed Bin\version.dll
echo Installed game01\shintsugi.pak

:done
echo.
echo Done. Start Shatterline from Steam as usual and open Play to pick a server.
echo No launch options are needed.
goto end

:notfound
echo.
echo That is not the Shatterline folder (it must contain Bin\Shatterline.exe and game01).
echo Install Shatterline from Steam first (it is free), then run Install.cmd again.
goto end

:copyfail
echo.
echo Could not copy the files into "%GAME%".
echo Right-click Install.cmd and choose "Run as administrator", or copy the Bin and game01
echo folders from this archive into the game folder by hand (see README.txt).

:end
echo.
pause
exit /b 0

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
