cmake_minimum_required(VERSION 3.16)
# ════════════════════════════════════════════════════════════════════════════
#  ВЕРСИЯ — меняется ТОЛЬКО здесь, при каждом релизе.
#  Отсюда она автоматически попадает в программу (Version.h), в установщик,
#  в «Программы и компоненты» и в имя файла релиза (make_release.bat).
# ════════════════════════════════════════════════════════════════════════════
#  Формат: «2.0» для больших версий, «2.0.1» для мелких правок.
project(SmartClip VERSION 2.3 LANGUAGES CXX)

add_compile_definitions(SMARTCLIP_VERSION="${PROJECT_VERSION}")
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${PROJECT_VERSION}")

# Тег для GitHub — как номер версии: v2.0, v2.0.1. Начиная с 2.0 SmartClip
# понимает и короткие номера (2.0 = 2.0.0). Версии 1.0.x поддерживать не нужно:
# 2.0 — отправная точка, её ставят заново.
set(SMARTCLIP_TAG "v${PROJECT_VERSION}")
file(WRITE "${CMAKE_BINARY_DIR}/tag.txt" "${SMARTCLIP_TAG}")

# Стандарт C++17 — обязательно, используем современные фичи
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 — ищем нужные модули
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Gui Core Network Multimedia LinguistTools)

# Автоматическая обработка Qt-макросов (сигналы, слоты, ресурсы)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Переводы ──────────────────────────────────────────────────────────────────
# qt6_add_translation компилирует .ts → .qm (без запуска lupdate)
set(TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/smartclip_en.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/smartclip_ua.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/smartclip_de.ts
)
qt6_add_translation(QM_FILES ${TS_FILES})

# Исходники
set(SOURCES
    assets/resources.qrc
    src/main.cpp
    src/core/Database.h
    src/core/Database.cpp
    src/core/ClipboardManager.h
    src/core/ClipboardManager.cpp
    src/core/HotkeyManager.h
    src/core/HotkeyManager.cpp
    src/core/Watchdog.h
    src/core/Watchdog.cpp
    src/core/AppSettings.h
    src/core/Version.h
    src/core/Secrets.h
    src/core/UpdateChecker.h
    src/core/UpdateChecker.cpp
    src/core/BugReporter.h
    src/core/BugReporter.cpp
    src/core/Backup.h
    src/core/Thumbs.h
    src/core/AdminMode.h
    src/core/Backup.cpp
    src/core/AdminMode.cpp
    src/core/VideoWatcher.h
    src/core/VideoWatcher.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ClickableCard.h
    src/ui/SmartButton.h
    src/ui/Anim.h
    src/ui/SmoothScroll.h
    src/ui/CardArc.h
    src/ui/CardShadow.h
    src/ui/ScrollTopButton.h
    src/ui/DateRuler.h
    src/ui/MediaCards.h
    src/ui/VideoViewer.h
    src/ui/FilesStrip.h
    src/ui/FolderShelf.h
    src/ui/FolderPanel.h
    src/ui/ShellMenu.h
    src/ui/DragSqueeze.h
    src/ui/GhostCards.h
    src/ui/FolderSlide.h
    src/ui/DragGhost.h
    src/ui/PhotoWindow.h
    src/ui/VideoWindow.h
    src/ui/Carry.h
    src/ui/NavTabs.h
    src/ui/HotkeyEdit.h
    src/ui/ImageViewer.h
    src/ui/ImageViewer.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
)

# Windows resource файл — иконка .exe в Проводнике
if(WIN32)
    list(APPEND SOURCES assets/app.rc)
    # Иконка вшита через .rc — пересобирать exe, когда меняется сама иконка
    set_source_files_properties(assets/app.rc PROPERTIES
        OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/smartclip.ico)
endif()

# Создаём исполняемый файл
add_executable(SmartClip WIN32 ${SOURCES})

# QM файлы не линкуются — собираем их отдельной целью до основной сборки
add_custom_target(SmartClipTranslations ALL DEPENDS ${QM_FILES})
add_dependencies(SmartClip SmartClipTranslations)

# Добавляем src/ как корень для поиска заголовков
target_include_directories(SmartClip PRIVATE src)

# Подключаем Qt6
target_link_libraries(SmartClip PRIVATE
    Qt6::Widgets
    Qt6::Multimedia   # Воспроизведение аудио и видео в карточках (2.1)
    Qt6::Sql
    Qt6::Gui
    Qt6::Core
    Qt6::Network
)

# Подключаем WinAPI библиотеки
target_link_libraries(SmartClip PRIVATE
    user32
    dwmapi
    advapi32
    shell32
    ole32     # Перенос файлов в программы (Carry.h, DoDragDrop)
    uuid
)

# ── Установщик ───────────────────────────────────────────────────────────────
add_subdirectory(installer)

# Копируем .qm файлы в папку translations рядом с .exe.
# Отдельным правилом на каждый файл, а не шагом «после сборки SmartClip»:
# тот срабатывал, только когда пересобирался сам exe, и если менялись одни
# переводы — в translations (и в установщик) попадали старые .qm.
set(QM_DEPLOYED)
foreach(qm ${QM_FILES})
    get_filename_component(qmName ${qm} NAME)
    set(qmOut "${CMAKE_CURRENT_BINARY_DIR}/translations/${qmName}")
    add_custom_command(OUTPUT ${qmOut}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${qm} ${qmOut}
        DEPENDS ${qm}
        COMMENT "Копирование перевода ${qmName}")
    list(APPEND QM_DEPLOYED ${qmOut})
endforeach()
add_custom_target(SmartClipTranslationsDeploy ALL DEPENDS ${QM_DEPLOYED})
add_dependencies(SmartClip SmartClipTranslationsDeploy)
