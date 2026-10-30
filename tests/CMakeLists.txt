# ╔══════════════════════════════════════════════════════════════════════════════╗
# ║  Тесты SmartClip — отдельная программа smartclip_tests.exe                   ║
# ║                                                                              ║
# ║  Собирает тот же код, что и SmartClip, но запускает его во временной папке  ║
# ║  (свои настройки и база — настоящие данные не трогаются) и без показа окон  ║
# ║  на экране. Запуск всех тестов — run_tests.bat в корне проекта.             ║
# ║  make_release.bat прогоняет их перед сборкой установщика.                   ║
# ╚══════════════════════════════════════════════════════════════════════════════╝
cmake_minimum_required(VERSION 3.16)
project(smartclip_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network Multimedia)

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(S "${ROOT}/src")
add_executable(smartclip_tests
    main.cpp Check.h TestUtil.h checks.cpp monkey.cpp cards_monkey.cpp scenarios.cpp
    ${ROOT}/assets/resources.qrc
    ${S}/ui/MainWindow.cpp ${S}/ui/MainWindow.h
    ${S}/ui/SettingsDialog.cpp ${S}/ui/SettingsDialog.h
    ${S}/ui/ImageViewer.cpp ${S}/ui/ImageViewer.h
    ${S}/ui/ClickableCard.h ${S}/ui/SmartButton.h ${S}/ui/HotkeyEdit.h ${S}/ui/SmoothScroll.h ${S}/ui/Anim.h
    ${S}/core/Database.cpp ${S}/core/Backup.cpp ${S}/core/AdminMode.cpp ${S}/core/VideoWatcher.cpp ${S}/core/VideoWatcher.h
    ${S}/core/UpdateChecker.cpp ${S}/core/UpdateChecker.h
    ${S}/core/BugReporter.cpp ${S}/core/BugReporter.h)
target_include_directories(smartclip_tests PRIVATE ${S} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(smartclip_tests PRIVATE SMARTCLIP_VERSION="0.0-test")
# Предупреждения компилятора — тоже ловят ошибки (непроинициализированное, лишние сравнения…)
target_compile_options(smartclip_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(smartclip_tests PRIVATE Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Multimedia
    user32 dwmapi advapi32 shell32 ole32 uuid)

# Монитор тестов — значок в трее и окно с ходом прогона (monitor.cpp).
# Отдельная программа: run_tests.bat запускает её сам
add_executable(smartclip_testmon WIN32 monitor.cpp)
target_link_libraries(smartclip_testmon PRIVATE Qt6::Widgets)
