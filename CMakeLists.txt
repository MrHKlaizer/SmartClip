cmake_minimum_required(VERSION 3.16)
project(SmartClip VERSION 1.0 LANGUAGES CXX)

# Стандарт C++17 — обязательно, используем современные фичи
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 — ищем нужные модули
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Gui Core Network LinguistTools)

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
    src/core/VideoWatcher.h
    src/core/VideoWatcher.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ClickableCard.h
    src/ui/SmartButton.h
    src/ui/HotkeyEdit.h
    src/ui/ImageViewer.h
    src/ui/ImageViewer.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
)

# Windows resource файл — иконка .exe в Проводнике
if(WIN32)
    list(APPEND SOURCES assets/app.rc)
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
    Qt6::Sql
    Qt6::Gui
    Qt6::Core
    Qt6::Network
)

# Подключаем WinAPI библиотеки
target_link_libraries(SmartClip PRIVATE
    user32
    dwmapi
)

# ── Установщик ───────────────────────────────────────────────────────────────
add_subdirectory(installer)

# Копируем .qm файлы рядом с .exe после сборки
add_custom_command(TARGET SmartClip POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:SmartClip>/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${QM_FILES}
        "$<TARGET_FILE_DIR:SmartClip>/translations/"
    COMMENT "Копирование переводов в папку сборки"
)
