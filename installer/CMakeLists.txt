cmake_minimum_required(VERSION 3.16)
project(Installer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Installer WIN32
    main.cpp
    installer.rc
)

target_link_libraries(Installer PRIVATE
    comctl32      # TaskDialog, InitCommonControls, прогресс-бар
    shell32       # ShellExecuteW, SHGetFolderPathW
    shlwapi       # SHDeleteKeyW
    advapi32      # RegXxx функции
)

# Статически линкуем MinGW runtime — exe будет работать на любом ПК
# без установленного Qt/MinGW (libgcc, libstdc++, libwinpthread)
if(MINGW)
    target_link_options(Installer PRIVATE -static -static-libgcc -static-libstdc++)
endif()
