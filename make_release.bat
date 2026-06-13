@echo off
setlocal

:: ─── Путь к папке сборки (поменяй если имя отличается) ───────────────────────
set BUILD=C:\!_SmartClip\build\Desktop_Qt_6_11_0_MinGW_64_bit-Debug

:: ─── Куда складывать ─────────────────────────────────────────────────────────
set OUT=C:\!_SmartClip\Release\SmartClip_Setup
set DATA=%OUT%\_data

:: ─────────────────────────────────────────────────────────────────────────────

echo.
echo  Собираем релизную папку...
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

:: ── Installer.exe — в корень (пользователь видит только его) ─────────────────
copy /y "%BUILD%\installer\Installer.exe" "%OUT%\" > nul
echo  [+] Installer.exe  (корень)

:: ── SmartClip.exe — в _data\ ─────────────────────────────────────────────────
copy /y "%BUILD%\SmartClip.exe" "%DATA%\" > nul
echo  [+] _data\SmartClip.exe

:: ── Все DLL — в _data\ ───────────────────────────────────────────────────────
for %%f in ("%BUILD%\*.dll") do (
    copy /y "%%f" "%DATA%\" > nul
    echo  [+] _data\%%~nxf
)

:: ── Папки плагинов и переводов — в _data\ ────────────────────────────────────
for %%d in (platforms styles sqldrivers imageformats iconengines generic networkinformation tls translations) do (
    if exist "%BUILD%\%%d\" (
        xcopy /e /i /q /y "%BUILD%\%%d" "%DATA%\%%d\" > nul
        echo  [+] _data\%%d\
    )
)

:: ── Итог ─────────────────────────────────────────────────────────────────────
echo.
echo  Готово! Папка для архивации:
echo  %OUT%
echo.
explorer "%OUT%"

endlocal
pause
