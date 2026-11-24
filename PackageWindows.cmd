@echo off
setlocal
set "SOKOBAN_UE=%~1"
if not defined SOKOBAN_UE set "SOKOBAN_UE=E:\UE5\UE_5.5"
set "SOKOBAN_PACKAGE_DIR=%~2"
if not defined SOKOBAN_PACKAGE_DIR set "SOKOBAN_PACKAGE_DIR=%~dp0Saved\Packages\Windows"
set "SOKOBAN_UAT=%SOKOBAN_UE%\Engine\Build\BatchFiles\RunUAT.bat"
if not exist "%SOKOBAN_UAT%" (
    echo UE 5.5 packaging tools not found. Pass the engine installation path as the first argument.
    exit /b 1
)
call "%SOKOBAN_UAT%" BuildCookRun -project="%~dp0SokobanRoguelite.uproject" -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="%SOKOBAN_PACKAGE_DIR%" -map=/Game/Maps/GridLaunch -nocompileeditor -unattended -utf8output
exit /b %ERRORLEVEL%
