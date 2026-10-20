@echo off
chcp 65001 > nul
setlocal

:: ════════════════════════════════════════════════════════════════════════════
::  Сборка релиза SmartClip — на выходе ОДИН файл:
::      Release\SmartClip_Installer_vX.Y.exe
::  Его и загружаешь в GitHub Release (без zip, без папок).
::
::  Перед релизом поменяй версию в CMakeLists.txt:
::      project(SmartClip VERSION 2.0 ...)   (2.0 — большая версия, 2.0.1 — мелкая правка)
::  Больше версию нигде менять не нужно.
:: ════════════════════════════════════════════════════════════════════════════

:: ─── Пути (поменяй QT, если обновишь Qt) ─────────────────────────────────────
set QT=C:\Qt\6.11.2\mingw_64
set MINGW=C:\Qt\Tools\mingw1310_64\bin
set NINJA=C:\Qt\Tools\Ninja
set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe

set ROOT=%~dp0
set BUILD=%ROOT%build\release
set STAGE=%BUILD%\_stage
set OUT=%ROOT%Release

set PATH=%QT%\bin;%MINGW%;%NINJA%;%PATH%

if not exist "%QT%\bin\windeployqt.exe" (
    echo.
    echo  ОШИБКА: Qt не найден в %QT%
    echo  Открой make_release.bat и поправь строку "set QT=..." под свою версию Qt.
    pause & exit /b 1
)

:: ─── [1/3] Сборка ────────────────────────────────────────────────────────────
echo.
echo  [1/3] Компилируем Release...
echo.

if not exist "%BUILD%\CMakeCache.txt" (
    "%CMAKE%" -S "%ROOT%." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT%"
    if errorlevel 1 goto :fail
)
"%CMAKE%" --build "%BUILD%" --parallel
if errorlevel 1 goto :fail

set /p VER=<"%BUILD%\version.txt"
set /p TAG=<"%BUILD%\tag.txt"
echo.
echo  Версия: %VER%

:: ─── [2/3] Собираем файлы программы ──────────────────────────────────────────
echo.
echo  [2/3] Собираем файлы программы...
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"

copy /y "%BUILD%\SmartClip.exe" "%STAGE%\" > nul || goto :fail

"%QT%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "%STAGE%\SmartClip.exe" > nul
if errorlevel 1 goto :fail
echo  [+] Qt DLL

for %%f in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if exist "%MINGW%\%%f" copy /y "%MINGW%\%%f" "%STAGE%\" > nul
)
echo  [+] MinGW runtime

if exist "%BUILD%\translations\" (
    xcopy /e /i /q /y "%BUILD%\translations" "%STAGE%\translations\" > nul
    echo  [+] Переводы
)

:: ─── [3/3] Упаковываем всё в один exe ────────────────────────────────────────
echo.
echo  [3/3] Упаковываем в один установщик (сжатие займёт ~минуту)...
if not exist "%OUT%" mkdir "%OUT%"
set RESULT=%OUT%\SmartClip_Installer_v%VER%.exe
"%BUILD%\installer\SmartClipPacker.exe" "%BUILD%\installer\Installer.exe" "%STAGE%" "%RESULT%"
if errorlevel 1 goto :fail

echo.
echo  ════════════════════════════════════════════════════════════
echo   Готово!  %RESULT%
echo.
echo   Дальше: GitHub - Releases - Draft a new release
echo     Tag:   %TAG%   - именно из трёх чисел, иначе старые версии не увидят обновление
echo     Название релиза: SmartClip %VER%
echo     Файл:  SmartClip_Installer_v%VER%.exe  (только его, без zip)
echo     НЕ ставь галочку "Set as a pre-release" - иначе друзьям
echo     обновление не придёт.
echo  ════════════════════════════════════════════════════════════
if not defined SMARTCLIP_NO_EXPLORER explorer /select,"%RESULT%"
endlocal
pause
exit /b 0

:fail
echo.
echo  ОШИБКА: сборка релиза не удалась (см. сообщения выше).
endlocal
pause
exit /b 1
