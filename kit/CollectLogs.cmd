@echo off
setlocal
title Shintsugi (Shatterline mod) - collect logs
rem Collects the logs a bug report needs into shintsugi_logs.zip next to this file (or on the Desktop if this folder
rem can't be written): the mod's offline_profile.log + offline_profile.prev.log and the game's newest Game.log.

set "OUT=%~dp0"
copy /y nul "%OUT%shintsugi_write_test.tmp" >nul 2>&1 || set "OUT=%USERPROFILE%\Desktop\"
del "%OUT%shintsugi_write_test.tmp" >nul 2>&1
set "DIR=%OUT%shintsugi_logs"
set "ZIP=%OUT%shintsugi_logs.zip"
if exist "%DIR%" rd /s /q "%DIR%"
mkdir "%DIR%" || goto fail

echo Collecting the Shintsugi and Shatterline logs...
if exist "%LOCALAPPDATA%\Shatterline\offline_profile.log" (
  copy /y "%LOCALAPPDATA%\Shatterline\offline_profile.log" "%DIR%" >nul
) else (
  echo   offline_profile.log not found: the mod has not run yet, or is not installed.
)
if exist "%LOCALAPPDATA%\Shatterline\offline_profile.prev.log" copy /y "%LOCALAPPDATA%\Shatterline\offline_profile.prev.log" "%DIR%" >nul

rem The game's log folder has a code name (for example PUBE518Q02KF4): the newest Game.log of all of them is taken
powershell -NoProfile -Command "$g = Get-ChildItem \"$env:USERPROFILE\Saved Games\Shatterline\*\user\log\Game.log\" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1; if ($g) { Copy-Item $g.FullName '%DIR%' } else { Write-Host '  Game.log not found.' }"

powershell -NoProfile -Command "Compress-Archive -Path '%DIR%\*' -DestinationPath '%ZIP%' -Force" || goto fail
echo.
echo Done: %ZIP%
echo.
echo The logs contain your Steam account number and the names of Steam friends who play Shatterline.
echo Attach the zip to your bug report (Nexus Mods: the Bugs tab of the Shintsugi page).
echo.
pause
exit /b 0

:fail
echo.
echo Could not collect the logs. Copy them by hand (see README.txt, LOGS).
echo.
pause
exit /b 1
