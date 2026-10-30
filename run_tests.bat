@echo off
setlocal

:: ============================================================================
::  SmartClip tests (folder tests\). No windows on screen; real settings,
::  database and folders are not touched - everything runs in a temp folder.
::
::      run_tests.bat quick           - fast, after edits (1-2 min)
::      run_tests.bat                 - everything, before a release (~15 min)
::      run_tests.bat run panel_tree  - one test
::      run_tests.bat monkey 7 500    - "monkey" with seed 7, 500 steps
::      run_tests.bat list            - list all tests
::
::  make_release.bat runs this itself and does not build the installer if
::  anything fails.
::
::  This file is ASCII only: cmd.exe mis-reads .bat files with UTF-8 letters
::  and runs pieces of lines (the test program prints Russian itself).
:: ============================================================================

set QT=C:\Qt\6.11.2\mingw_64
set MINGW=C:\Qt\Tools\mingw1310_64\bin
set NINJA=C:\Qt\Tools\Ninja
set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
set TBUILD=%~dp0tests\build
set PATH=%QT%\bin;%MINGW%;%NINJA%;%PATH%

if not exist "%TBUILD%\CMakeCache.txt" (
    "%CMAKE%" -S "%~dp0tests" -B "%TBUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT%" > nul
    if errorlevel 1 goto :fail
)
echo  Building tests...
"%CMAKE%" --build "%TBUILD%" --parallel > "%TBUILD%\build.log" 2>&1
if errorlevel 1 (
    type "%TBUILD%\build.log"
    goto :fail
)
:: Monitor in the tray (test progress, like Task Manager) - starts once
if exist "%TBUILD%\smartclip_testmon.exe" start "" "%TBUILD%\smartclip_testmon.exe"
"%TBUILD%\smartclip_tests.exe" %*
if errorlevel 1 goto :fail
endlocal
exit /b 0

:fail
endlocal
exit /b 1
