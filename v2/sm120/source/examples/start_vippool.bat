@echo off
setlocal DisableDelayedExpansion

set "POOL_URL=stratum+tcp://stratum1.vippool.net:8888"
set "POOL_USER=YOUR_WORKER"
set "POOL_PASS=YOUR_PASSWORD"

"%~dp0mona-miner.exe" -a lyra2v2 -o "%POOL_URL%" -u "%POOL_USER%" -p "%POOL_PASS%"
pause
