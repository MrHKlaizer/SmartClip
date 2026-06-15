@echo off
setlocal

:: ─── Пути ─────────────────────────────────────────────────────────────────────
set QT=C:\Qt\6.11.0\mingw_64
set MINGW=C:\Qt\Tools\mingw1310_64\bin
set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
set BUILD=C:\!_SmartClip\build\Desktop_Qt_6_11_0_MinGW_64_bit-Release
set OUT=C:\!_SmartClip\Release\SmartClip_Setup
set DATA=%OUT%\_data

set PATH=%QT%\bin;%MINGW%;%PATH%

:: ─────────────────────────────────────────────────────────────────────────────

echo.
echo  [1/2] Компилируем Release...
echo.

"%CMAKE%" --build "%BUILD%"
if errorlevel 1 (
    echo.
    echo  ОШИБКА: сборка не удалась.
    pause & exit /b 1
)

echo.
echo  [2/2] Собираем релизную папку...
echo  Из: %BUILD%
echo  В:  %OUT%
echo.

:: Чистим и создаём выходные папки
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
mkdir "%DATA%"

:: Проверяем что сборка существует
if not exist "%BUILD%\SmartClip.exe" (
    echo  ОШИБКА: SmartClip.exe не найден в папке сборки.
    echo  Убедись что проект собран в Qt Creator.
    pause & exit /b 1
)
if not exist "%BUILD%\installer\Installer.exe" (
    echo  ОШИБКА: Installer.exe не найден.
    echo  Убедись что проект собран полностью ^(включая target Installer^).
    pause & exit /b 1
)

:: ── SmartClip.exe → _data\ + windeployqt ────────────────────────────────────
copy /y "%BUILD%\SmartClip.exe" "%DATA%\" > nul
echo  [+] _data\SmartClip.exe

echo  Запускаем windeployqt...
"%QT%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw "%DATA%\SmartClip.exe"
if errorlevel 1 (
    echo  ОШИБКА: windeployqt завершился с ошибкой.
    pause & exit /b 1
)
echo  [+] Qt DLL-ки задеплоены

:: ── MinGW runtime DLL ─────────────────────────────────────────────────────────
for %%f in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if exist "%MINGW%\%%f" (
        copy /y "%MINGW%\%%f" "%DATA%\" > nul
        echo  [+] _data\%%f
    )
)

:: ── Переводы приложения (наши .qm файлы) ─────────────────────────────────────
if exist "%BUILD%\translations\" (
    xcopy /e /i /q /y "%BUILD%\translations" "%DATA%\translations\" > nul
    echo  [+] _data\translations\
)

:: ── Installer.exe — в корень (пользователь видит только его) ─────────────────
copy /y "%BUILD%\installer\Installer.exe" "%OUT%\" > nul
echo  [+] Installer.exe  (корень)

:: ── Итог ─────────────────────────────────────────────────────────────────────
echo.
echo  Готово! Папка для архивации:
echo  %OUT%
echo.
explorer "%OUT%"

endlocal
pause
