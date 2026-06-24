// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  main.cpp — Точка входа в программу SmartClip                              ║
// ║                                                                              ║
// ║  Здесь всё начинается: функция main() — это первая функция которая         ║
// ║  запускается при старте программы.                                           ║
// ║                                                                              ║
// ║  Что делает main():                                                          ║
// ║  1. Создаёт Qt-приложение и настраивает масштаб/тему/язык                  ║
// ║  2. Инициализирует базу данных, буфер обмена, горячие клавиши              ║
// ║  3. Создаёт главное окно и иконку в трее                                    ║
// ║  4. Связывает все компоненты через сигналы Qt                               ║
// ║  5. Запускает главный цикл событий (app.exec()) — программа «живёт» в нём ║
// ║  6. При выходе — освобождает ресурсы и перезапускает если нужно            ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

// Qt-заголовки:
#include <QApplication>       // Главный класс Qt-приложения с графическим интерфейсом
#include <QTranslator>        // Загрузка файлов перевода (.qm)
#include <QLibraryInfo>       // Пути к стандартным файлам Qt (переводы кнопок ОК/Отмена)
#include <QMap>               // Словарь (ключ → значение)
#include <QDir>               // Работа с папками
#include <QFile>              // Работа с файлами
#include <QProcess>           // Запуск нового процесса (для перезапуска после смены языка)
#include <QJsonDocument>      // Сериализация/десериализация JSON (для бекапа)
#include <QJsonObject>        // JSON-объект {ключ: значение}
#include <QJsonArray>         // JSON-массив [...]
#include <QDateTime>          // Дата и время
#include <QTimer>             // Таймер (для отложенной проверки обновлений)
#include <QRegularExpression> // Регулярные выражения (для масштабирования QSS)
#include <QIcon>              // Иконка приложения
#include <QSystemTrayIcon>    // Иконка в системном трее (область уведомлений)
#include <QMenu>              // Контекстное меню
#include <QAction>            // Пункт меню

// Наши компоненты:
#include "core/Database.h"         // База данных SQLite
#include "core/ClipboardManager.h" // Перехват буфера обмена
#include "core/HotkeyManager.h"    // Глобальные горячие клавиши
#include "core/Watchdog.h"         // «Сторожевой пёс» для ClipboardManager
#include "core/AppSettings.h"      // Настройки (синглтон, INI-файл)
#include "core/VideoWatcher.h"     // Мониторинг папки с видеозаписями
#include "core/UpdateChecker.h"    // Проверка обновлений на GitHub
#include "ui/MainWindow.h"         // Главное окно SmartClip

// ─── Точка входа ─────────────────────────────────────────────────────────────
// main() — первая функция которую запускает ОС при старте программы.
// argc — количество аргументов командной строки (обычно 1)
// argv — массив строк-аргументов (argv[0] = путь к .exe)
int main(int argc, char *argv[])
{
    // ── Защита от двойного запуска ────────────────────────────────────────────
    // Если SmartClip уже запущен (автозапуск + ручной старт) — второй экземпляр
    // сразу выходит. Без этого оба процесса пишут в одну БД → каждая копия
    // создаёт 2 записи в истории.
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"SmartClipSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0; // Уже запущен — тихо выходим
    }

    // QApplication — «сердце» Qt-приложения. Управляет событиями, шрифтами, темой.
    // Должен быть создан первым, до любых других Qt-объектов.
    QApplication app(argc, argv);
    app.setApplicationName("SmartClip");
    app.setOrganizationName("SmartClipApp");

    // Не выходить при закрытии последнего окна.
    // SmartClip «живёт» в системном трее даже когда окно скрыто.
    app.setQuitOnLastWindowClosed(false);

    // Иконка приложения — отображается в трее и диалогах.
    // ":/icons/smartclip.ico" — путь к ресурсу (qrc файл).
    app.setWindowIcon(QIcon(":/icons/smartclip.ico"));

    // ── Масштаб шрифта ────────────────────────────────────────────────────────
    // Читаем масштаб из настроек (75-125%). Применяем к базовому шрифту системы.
    // Это масштабирует текст во всём приложении пропорционально.
    int uiScale = AppSettings::get().uiScale();
    if (uiScale != 100) {
        QFont f = app.font();
        // setPointSizeF — задаёт размер в пунктах (дробное число для точности)
        f.setPointSizeF(f.pointSizeF() * uiScale / 100.0);
        app.setFont(f);  // Применяем ко всему приложению
    }

    // ── Тема оформления (QSS) ────────────────────────────────────────────────
    // QSS (Qt Style Sheets) — CSS-подобный язык для стилизации виджетов Qt.
    // Загружаем тему из файла ресурсов и применяем ко всему приложению.
    QFile qssFile(":/theme.qss");
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        QString qss = QString::fromUtf8(qssFile.readAll());

        if (uiScale != 100) {
            // ── Масштабирование px-значений в QSS ─────────────────────────────
            // theme.qss содержит размеры типа "padding: 8px", "font-size: 12px".
            // При масштабе ≠ 100% нужно умножить все значения ≥ 4px.
            // 1px/2px/3px — границы и разделители — НЕ масштабируем.
            //
            // QRegularExpression ищет паттерн: число + "px"
            // Пример: "14px" → "17px" при scale=125
            QRegularExpression re("(\\b)(\\d+)(px\\b)");
            QString scaled;
            int last = 0;
            auto it = re.globalMatch(qss);  // Итератор по всем совпадениям в строке
            while (it.hasNext()) {
                auto m = it.next();
                // Добавляем часть до совпадения (без изменений)
                scaled += qss.mid(last, m.capturedStart() - last);
                int v = m.captured(2).toInt();  // Числовое значение px
                if (v >= 4)
                    scaled += QString::number(v * uiScale / 100) + "px";  // Масштабируем
                else
                    scaled += m.captured(0);  // 1px/2px/3px — оставляем как есть
                last = m.capturedEnd();
            }
            scaled += qss.mid(last);  // Добавляем остаток строки после последнего совпадения
            qss = scaled;
        }
        app.setStyleSheet(qss);  // Применяем QSS ко всему приложению
    }

    // ── Загрузка переводов ────────────────────────────────────────────────────
    // Qt использует два уровня переводов:
    // 1. Перевод Qt-фреймворка (стандартные кнопки «ОК», «Отмена», «Применить»)
    // 2. Перевод нашего приложения (все строки tr("...") в нашем коде)

    QString lang = AppSettings::get().language();  // "en", "de", "ua", "ru"
    // Qt использует код "uk" для украинского (ISO 639-1), а мы храним "ua"
    QString qtLangCode = (lang == "ua") ? "uk" : lang;

    // Загружаем стандартный Qt-перевод (кнопки диалогов)
    QTranslator qtTranslator;
    if (qtTranslator.load("qt_" + qtLangCode,
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    // Загружаем перевод нашего приложения (из папки translations/)
    // Русский — исходный язык кода, перевода не нужно.
    QTranslator appTranslator;
    if (lang != "ru") {
        QString langPath = QCoreApplication::applicationDirPath() + "/translations/";
        if (appTranslator.load(langPath + "smartclip_" + lang + ".qm")) {
            app.installTranslator(&appTranslator);
            qDebug() << "Загружен перевод:" << lang;
        } else {
            qDebug() << "Файл перевода не найден:" << langPath + "smartclip_" + lang + ".qm";
        }
    }

    // ── Инициализация компонентов ─────────────────────────────────────────────
    // Порядок важен: Database должна быть готова до ClipboardManager.

    Database db;
    if (!db.init())   // Открываем или создаём SmartClip.db
        return 1;     // 1 = код ошибки (0 = успех)

    ClipboardManager clipboard(&db);
    if (!clipboard.start())  // Создаём скрытое окно-слушатель
        return 1;

    HotkeyManager hotkey;
    if (!hotkey.start())     // Регистрируем глобальные хоткеи
        return 1;

    // Главное окно создаётся но не показывается — будет открыто по хоткею или из трея.
    MainWindow window(&db);

    // ── Иконка в системном трее ───────────────────────────────────────────────
    // Системный трей = область уведомлений рядом с часами в правом нижнем углу экрана.
    QSystemTrayIcon tray(QIcon(":/icons/smartclip.ico"));
    tray.setToolTip("SmartClip");  // Подсказка при наведении на иконку

    // Контекстное меню трея (правый клик по иконке)
    QMenu trayMenu;

    // Заголовок меню — жирный, некликабельный (просто название программы)
    QAction *titleAct = trayMenu.addAction("SmartClip");
    titleAct->setEnabled(false);
    QFont titleFont = titleAct->font();
    titleFont.setBold(true);
    titleAct->setFont(titleFont);

    trayMenu.addSeparator();  // Горизонтальная линия-разделитель

    // «Открыть» → показать главное окно
    QAction *openAct = trayMenu.addAction(QObject::tr("Открыть"));
    QObject::connect(openAct, &QAction::triggered, [&]() { window.showWindow(); });

    trayMenu.addSeparator();

    // «Выход» → завершить программу
    QAction *quitAct = trayMenu.addAction(QObject::tr("Выход"));
    QObject::connect(quitAct, &QAction::triggered, &app, &QApplication::quit);

    tray.setContextMenu(&trayMenu);

    // Двойной клик по иконке трея → показать окно
    QObject::connect(&tray, &QSystemTrayIcon::activated,
                     [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            window.showWindow();
    });

    tray.show();  // Показываем иконку в трее

    // ── Обработка главного хоткея ─────────────────────────────────────────────
    // Нажатие хоткея: если окно открыто — закрыть, если закрыто — открыть.
    QObject::connect(&hotkey, &HotkeyManager::hotkeyPressed, [&]() {
        if (window.isVisible()) {
            // Закрываем модальный диалог если открыт (Настройки, диалог обновления...).
            // activeModalWidget() — возвращает верхнее модальное окно или nullptr.
            if (QWidget *modal = QApplication::activeModalWidget())
                modal->close();
            window.hide();
        } else {
            window.showWindow();
        }
    });

    // ── Связи между компонентами ──────────────────────────────────────────────
    // Перед тем как SmartClip вставляет текст — предупреждаем ClipboardManager
    // «не записывай следующее событие буфера». Иначе вставленное попадёт в историю повторно.
    QObject::connect(&window, &MainWindow::pasteRequested,
                     &clipboard, &ClipboardManager::suppressNext);

    // Когда что-то скопировано → помечаем историю устаревшей (при открытии окна она обновится)
    QObject::connect(&clipboard, &ClipboardManager::historyChanged,
                     &window, &MainWindow::markHistoryDirty);

    // ── Профили быстрой вставки ───────────────────────────────────────────────
    // Лямбда-функция: читает профили из БД и передаёт хоткеи в HotkeyManager.
    // auto = компилятор сам выводит тип (здесь это std::function<void()>).
    auto reloadProfileHotkeys = [&]() {
        QMap<int, QString> map;
        for (const ProfileItem &p : db.getProfiles())
            if (!p.hotkey.trimmed().isEmpty())
                map[p.id] = p.hotkey;
        hotkey.setProfileHotkeys(map);
    };

    reloadProfileHotkeys();  // Загружаем хоткеи при старте

    // Профили изменились (в Settings) → перерегистрировать хоткеи
    QObject::connect(&window, &MainWindow::profilesChanged, [&]() {
        reloadProfileHotkeys();
    });

    // Смена главного хоткея → перерегистрация через HotkeyManager
    QObject::connect(&window, &MainWindow::mainHotkeyChanged,
                     &hotkey, &HotkeyManager::setMainHotkey);

    // Хоткей профиля нажат → найти текст профиля и вставить без открытия окна
    QObject::connect(&hotkey, &HotkeyManager::profileHotkeyPressed,
                     [&](int profileId) {
        for (const ProfileItem &p : db.getProfiles()) {
            if (p.id == profileId) {
                window.pasteTextSilent(p.text);
                break;
            }
        }
    });

    // ── Автоматический бекап ─────────────────────────────────────────────────
    // При каждом запуске проверяем: прошло ли достаточно дней с последнего бекапа.
    // Бекап сохраняется в JSON-файл в указанную папку.
    auto &cfg = AppSettings::get();
    if (cfg.autoBackup() && !cfg.autoBackupPath().isEmpty()) {
        QString backupDir = cfg.autoBackupPath();

        // stampFile — текстовый файл с датой последнего бекапа (ISO-формат)
        QString stampFile = backupDir + "/last_backup.txt";
        bool needBackup = true;

        QFile sf(stampFile);
        if (sf.open(QIODevice::ReadOnly)) {
            // Читаем дату последнего бекапа
            QDateTime last = QDateTime::fromString(
                QString(sf.readAll()).trimmed(), Qt::ISODate);
            sf.close();
            // Если с последнего бекапа прошло меньше autoBackupDays — не делаем
            if (last.isValid() && last.daysTo(QDateTime::currentDateTime()) < cfg.autoBackupDays())
                needBackup = false;
        }

        if (needBackup) {
            // ── Формируем JSON-документ с закрепами и профилями ──────────────
            QJsonObject root;
            root["version"]     = "1.0";
            root["exported_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

            // Собираем все закрепы (из всех папок)
            QJsonArray allPins;
            auto addPins = [&](const QString &folder) {
                for (const PinItem &p : db.getPins(folder)) {
                    QJsonObject o;
                    o["folder"]   = p.folder; o["name"]    = p.name;
                    o["type"]     = p.type;   o["content"] = p.content;
                    o["filepath"] = p.filepath;
                    allPins.append(o);
                }
            };
            addPins("");  // Закрепы без папки
            for (const FolderItem &f : db.getAllFolders())
                addPins(f.name);  // Закрепы из каждой папки
            root["pins"] = allPins;

            // Собираем профили
            QJsonArray profArr;
            for (const ProfileItem &p : db.getProfiles()) {
                QJsonObject o;
                o["name"] = p.name; o["hotkey"] = p.hotkey; o["text"] = p.text;
                profArr.append(o);
            }
            root["profiles"] = profArr;

            // Сохраняем JSON в файл с датой в имени: smartclip_backup_2025-01-15.json
            QString fname = backupDir + "/smartclip_backup_"
                + QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".json";
            QFile f(fname);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
                f.close();
                // Обновляем штамп времени бекапа
                if (sf.open(QIODevice::WriteOnly)) {
                    sf.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
                    sf.close();
                }
                qDebug() << "Автобекап сохранён:" << fname;
            }
        }
    }

    // ── Мониторинг папки видеозаписей (Бета) ─────────────────────────────────
    // Следим за папкой Xbox Game Bar — новые видео автоматически появляются в истории.
    VideoWatcher videoWatcher(&db);
    if (cfg.videoMonitorEnabled() && !cfg.videoMonitorPath().isEmpty()) {
        videoWatcher.start(cfg.videoMonitorPath());
        // Новое видео → обновить историю в главном окне
        QObject::connect(&videoWatcher, &VideoWatcher::newVideoAdded,
                         &window, &MainWindow::markHistoryDirty);
    }
    // При изменении настроек — обновляем VideoWatcher (включить/выключить/сменить папку)
    QObject::connect(&window, &MainWindow::settingsChanged, [&]() {
        auto &s = AppSettings::get();
        if (s.videoMonitorEnabled())
            videoWatcher.setFolder(s.videoMonitorPath());
        else
            videoWatcher.stop();
    });

    // ── Проверка обновлений ───────────────────────────────────────────────────
    // Через 10 секунд после старта (чтобы не тормозить запуск) отправляем запрос на GitHub.
    // new UpdateChecker(&app) — parent=&app, Qt удалит его при завершении программы.
    auto *updater = new UpdateChecker(&app);
    QObject::connect(updater, &UpdateChecker::updateAvailable,
                     &window, &MainWindow::onUpdateAvailable);
    // singleShot — запустить один раз через 10000мс (10 секунд)
    QTimer::singleShot(10000, updater, [updater]() { updater->check(true); });

    // ── Watchdog ──────────────────────────────────────────────────────────────
    // «Сторожевой пёс» запускается в отдельном потоке и следит за ClipboardManager.
    // Если буфер завис — автоматически перезапускает мониторинг.
    Watchdog watchdog(&clipboard);
    watchdog.start();  // Запустить фоновый поток

    // ── Главный цикл событий ──────────────────────────────────────────────────
    // app.exec() — запускает Qt Event Loop: бесконечный цикл обработки событий.
    // Здесь программа «живёт»: обрабатывает клики, хоткеи, таймеры, сетевые ответы.
    // Возвращает управление только когда пользователь нажимает «Выход».
    int result = app.exec();

    // ── Завершение ────────────────────────────────────────────────────────────
    watchdog.stop();  // Останавливаем сторожевого пса (ждём завершения потока)
    // ClipboardManager, HotkeyManager, VideoWatcher — уничтожатся автоматически
    // когда выйдем из main() (локальные переменные удаляются в обратном порядке).

    // ── Перезапуск при смене языка/масштаба ──────────────────────────────────
    // SettingsDialog устанавливает restartRequested = true когда нужен перезапуск.
    // Здесь мы проверяем флаг и запускаем новый процесс SmartClip.
    // Освобождаем мьютекс перед перезапуском — иначе новый экземпляр не запустится
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    if (AppSettings::get().restartRequested()) {
        AppSettings::get().setRestartRequested(false);  // Сбрасываем флаг
        // startDetached — запустить процесс независимо от текущего (не дочерний).
        // applicationFilePath() — полный путь к текущему .exe.
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    }

    return result;  // 0 = нормальное завершение
}
