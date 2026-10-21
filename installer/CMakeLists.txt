cmake_minimum_required(VERSION 3.16)
project(Installer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Установщик ───────────────────────────────────────────────────────────────
# Сам по себе Installer.exe — это «пустой» установщик. make_release.bat
# дописывает в его конец сжатые файлы программы (через SmartClipPacker ниже)
# и получается один файл SmartClip_Installer_vX.Y.Z.exe.
add_executable(Installer WIN32
    main.cpp
    Payload.h
    installer.rc
)
# Иконка вшита через .rc — пересобирать установщик, когда меняется сама иконка
set_source_files_properties(installer.rc PROPERTIES
    OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/smartclip.ico)

target_link_libraries(Installer PRIVATE
    comctl32      # TaskDialog, InitCommonControls, прогресс-бар
    shell32       # ShellExecuteW, SHGetFolderPathW
    shlwapi       # SHDeleteKeyW
    advapi32      # RegXxx функции
    cabinet       # Распаковка встроенных файлов (Windows Compression API, LZMS)
)

# ── Упаковщик (запускается только при сборке релиза) ────────────────────────
add_executable(SmartClipPacker packer.cpp Payload.h)
target_link_libraries(SmartClipPacker PRIVATE cabinet)

# Статически линкуем MinGW runtime — exe будет работать на любом ПК
# без установленного Qt/MinGW (libgcc, libstdc++, libwinpthread)
if(MINGW)
    target_link_options(Installer PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(SmartClipPacker PRIVATE -static -municode)
endif()
