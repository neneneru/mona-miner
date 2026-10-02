@echo off
title Mona Miner - SM89
setlocal
cd /d "%~dp0"

rem Mona Miner - VIP Pool launch template
rem Replace WORKER and PASSWORD with your own values.
rem Do not publish or share this BAT after entering real credentials.

set "POOL=stratum+tcp://stratum1.vippool.net:8888"
set "WORKER=YOUR_WEBLOGIN.YOUR_WORKER"
set "PASSWORD=YOUR_PASSWORD"

mona-miner.exe -a lyra2v2 -o "%POOL%" -u "%WORKER%" -p "%PASSWORD%" --device 0

set "EXITCODE=%ERRORLEVEL%"
echo.
echo mona-miner.exe exited with code %EXITCODE%.
pause
exit /b %EXITCODE%
