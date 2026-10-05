@echo off
chcp 936 >nul
setlocal
rem 成功后是否停住窗口：0=直接关闭（默认，保持原行为），1=pause 等按键。
rem 双击前可在本文件改默认值；命令行可覆盖：set PAUSE_ON_SUCCESS=1 && build_and_deploy.bat
if not defined PAUSE_ON_SUCCESS set PAUSE_ON_SUCCESS=0

pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if %errorlevel% neq 0 goto error

echo 开始部署...
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1"
if %errorlevel% neq 0 goto error

echo 编译并部署完成！
if "%PAUSE_ON_SUCCESS%"=="1" pause
exit /b 0

:error
echo 编译或部署失败！
pause
exit /b 1