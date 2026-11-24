@echo off
setlocal
set "SOKOBAN_UE=%~1"
if not defined SOKOBAN_UE set "SOKOBAN_UE=E:\UE5\UE_5.5"
set "SOKOBAN_BUILD=%SOKOBAN_UE%\Engine\Build\BatchFiles\Build.bat"
if not exist "%SOKOBAN_BUILD%" (
    echo UE 5.5 build tools not found. Pass the engine installation path as the first argument.
    exit /b 1
)
call "%SOKOBAN_BUILD%" SokobanRogueliteEditor Win64 Development "-Project=%~dp0SokobanRoguelite.uproject" -WaitMutex
exit /b %ERRORLEVEL%
