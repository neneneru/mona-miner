@echo off
setlocal DisableDelayedExpansion
title Mona Miner v2 - SM120 - VIP Pool

rem Edit these values once and save this file.
rem Credentials are stored in plaintext in this BAT, like the Public v1 launcher.
set "POOL_URL=stratum+tcp://stratum1.vippool.net:8888"
set "POOL_USER=YOUR_WORKER"
set "POOL_PASS=YOUR_PASSWORD"
set "DEVICE=0"

if "%POOL_USER%"=="YOUR_WORKER" (
    echo ERROR: Edit POOL_USER and POOL_PASS in start_vippool.bat first.
    echo.
    pause
    exit /b 2
)
if "%POOL_PASS%"=="YOUR_PASSWORD" (
    echo ERROR: Edit POOL_USER and POOL_PASS in start_vippool.bat first.
    echo.
    pause
    exit /b 2
)

if not exist "%~dp0mona-miner.exe" (
    echo ERROR: mona-miner.exe was not found.
    echo Microsoft Defender may have quarantined the miner.
    echo.
    pause
    exit /b 2
)

"%~dp0mona-miner.exe" -a lyra2v2 -o "%POOL_URL%" -u "%POOL_USER%" -p "%POOL_PASS%" --device %DEVICE%
set "RC=%ERRORLEVEL%"

echo.
echo Mona Miner exited with code %RC%.
pause
exit /b %RC%
