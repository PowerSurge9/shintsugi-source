@echo off
rem Shintsugi - uninstaller for a Shatterline Dedicated Server. Double-click it: it removes the paks of the game01
rem folder next to it (shintsugi_server.pak, or a skin pack's shintsugi_skins_<pack>_server.pak) from the server's
rem game01 folder. Nothing else is touched.
rem The server folder can also be given: drop it onto this file.
setlocal
title Shintsugi - server uninstall
echo.
echo   Shintsugi - server uninstall
echo   ============================
echo.
if not exist "%~dp0game01\*.pak" (
  echo No server pak next to this file, so it is unknown what to remove.
  echo Run UninstallServer.cmd from the unpacked folder of the download.
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

tasklist /fi "imagename eq Shatterline_Server.exe" | find /i "Shatterline_Server.exe" >nul && (
  echo A Shatterline server is running. Stop every server instance and its auto-restart loop first,
  echo then run UninstallServer.cmd again.
  goto end
)
echo Server folder: "%SRV%"
echo.
set "FOUND="
for %%P in ("%~dp0game01\*.pak") do (
  if exist "%SRV%\game01\%%~nxP" (
    set "FOUND=1"
    del /f /q "%SRV%\game01\%%~nxP" && echo Removed game01\%%~nxP
  )
)
if not defined FOUND echo Nothing to remove: these files were not installed.
call :catalog || goto catalogfail
echo.
echo Done. Start the servers again.
goto end

:notfound
echo.
echo That is not a Shatterline Dedicated Server folder (it must contain Bin.Release.Dedicated\Shatterline_Server.exe).
goto end

:catalogfail
echo.
echo The paks are removed, but the server's asset catalog could not be updated (the message above says why).
echo The server still works. To get the game's own catalog back: in Steam (or SteamCMD), verify the server files.

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
if defined SRV exit /b 0
set "LIB=%~1"
set "LIB=%LIB:\\=\%"
if exist "%LIB%\steamapps\common\Shatterline Dedicated Server\Bin.Release.Dedicated\Shatterline_Server.exe" set "SRV=%LIB%\steamapps\common\Shatterline Dedicated Server"
exit /b 0

rem Lists the models of the Shintsugi paks still installed in the server's asset catalog; with none left, the
rem game's own catalog is put back (see catalog\UpdateCatalog.ps1)
:catalog
echo.
echo Updating the server's asset catalog (the index the server finds models in)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0catalog\UpdateCatalog.ps1" -Game01 "%SRV%\game01"
exit /b %errorlevel%
