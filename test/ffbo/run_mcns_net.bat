@echo off
REM MaleCNS v1.0 network test launcher (ASCII only - cmd parses .bat as GBK)
REM Usage: run_mcns_net.bat [extra args]
setlocal
set MCNS=D:\WorkSpace\_research\digital-fruit-fly\data\malecns
set PATH=D:\WorkSpace\xhqm_v2\binary;%PATH%
"%~dp0build\mcns_net.exe" "%MCNS%" %*
endlocal
