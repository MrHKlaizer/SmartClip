#include <QApplication>
#include <QTranslator>
#include <QLibraryInfo>
#include <QMap>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QIcon>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include "core/Database.h"
#include "core/ClipboardManager.h"
#include "core/HotkeyManager.h"
#include "core/Watchdog.h"
#include "core/AppSettings.h"
#include "core/VideoWatcher.h"
#include "core/UpdateChecker.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("SmartClip");
    app.setOrganizationName("SmartClipApp");

    // Не выходить при закрытии последнего окна — приложение живёт в трее
    app.setQuitOnLastWindowClosed(false);

    // Иконка приложения — подхватывается всеми диалогами и окном в трее
    app.setWindowIcon(QIcon(":/icons/smartclip.ico"));

    // Загружаем тему оформления из QSS-файла
    QFile qssFile(":/theme.qss");
    if (qssFile.open(QFile::ReadOnly | QFile::Text))
        app.setStyleSheet(QString::fromUtf8(qssFile.readAll()));

    // Загружаем перевод Qt (стандартные кнопки: ОК/Отмена и т.д.)
    QString lang = AppSettings::get().language(); // "ru", "en", "ua", "de"
    QString qtLangCode = (lang == "ua") ? "uk" : lang; // Qt использует "uk" для украинского
    QTranslator qtTranslator;
    if (qtTranslator.load("qt_" + qtLangCode,
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    // Загружаем перевод приложения (не нужен для русского — он исходный язык)
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

    Database db;
    if (!db.init())
        return 1;

    ClipboardManager clipboard(&db);
    if (!clipboard.start())
        return 1;

    HotkeyManager hotkey;
    if (!hotkey.start())
        return 1;

    MainWindow window(&db);

    // ── Иконка в системном трее ───────────────────────────────────────────────
    QSystemTrayIcon tray(QIcon(":/icons/smartclip.ico"));
    tray.setToolTip("SmartClip");

    QMenu trayMenu;

    // Заголовок (не кликабельный)
    QAction *titleAct = trayMenu.addAction("SmartClip");
    titleAct->setEnabled(false);
    QFont titleFont = titleAct->font();
    titleFont.setBold(true);
    titleAct->setFont(titleFont);

    trayMenu.addSeparator();

    QAction *openAct = trayMenu.addAction(QObject::tr("Открыть"));
    QObject::connect(openAct, &QAction::triggered, [&]() { window.showWindow(); });

    trayMenu.addSeparator();

    QAction *quitAct = trayMenu.addAction(QObject::tr("Выход"));
    QObject::connect(quitAct, &QAction::triggered, &app, &QApplication::quit);

    tray.setContextMenu(&trayMenu);

    // Двойной клик по иконке → открыть окно
    QObject::connect(&tray, &QSystemTrayIcon::activated,
                     [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            window.showWindow();
    });

    tray.show();

    // Хоткей → показать окно
    QObject::connect(&hotkey, &HotkeyManager::hotkeyPressed,
                     &window, &MainWindow::showWindow);

    // Перед вставкой MainWindow предупреждает ClipboardManager: не записывай следующее событие
    QObject::connect(&window, &MainWindow::pasteRequested,
                     &clipboard, &ClipboardManager::suppressNext);

    // Когда скопировано что-то новое — помечаем историю как устаревшую
    QObject::connect(&clipboard, &ClipboardManager::historyChanged,
                     &window, &MainWindow::markHistoryDirty);

    // ── Профили ──────────────────────────────────────────────────────────────
    // Вспомогательная функция: собрать карту id→hotkey из БД и передать в HotkeyManager
    auto reloadProfileHotkeys = [&]() {
        QMap<int, QString> map;
        for (const ProfileItem &p : db.getProfiles())
            if (!p.hotkey.trimmed().isEmpty())
                map[p.id] = p.hotkey;
        hotkey.setProfileHotkeys(map);
    };

    // Загружаем хоткеи при старте
    reloadProfileHotkeys();

    // Перерегистрируем когда профили изменились (добавили / изменили / удалили)
    QObject::connect(&window, &MainWindow::profilesChanged, [&]() {
        reloadProfileHotkeys();
    });

    // Смена основной горячей клавиши из настроек → перерегистрация
    QObject::connect(&window, &MainWindow::mainHotkeyChanged,
                     &hotkey, &HotkeyManager::setMainHotkey);

    // Хоткей профиля → вставить текст напрямую, окно не открывается
    QObject::connect(&hotkey, &HotkeyManager::profileHotkeyPressed,
                     [&](int profileId) {
        for (const ProfileItem &p : db.getProfiles()) {
            if (p.id == profileId) {
                window.pasteTextSilent(p.text);
                break;
            }
        }
    });

    // ── Автобекап при старте ─────────────────────────────────────────────────
    auto &cfg = AppSettings::get();
    if (cfg.autoBackup() && !cfg.autoBackupPath().isEmpty()) {
        QString backupDir = cfg.autoBackupPath();
        // Файл с датой последнего бекапа
        QString stampFile = backupDir + "/last_backup.txt";
        bool needBackup = true;
        QFile sf(stampFile);
        if (sf.open(QIODevice::ReadOnly)) {
            QDateTime last = QDateTime::fromString(
                QString(sf.readAll()).trimmed(), Qt::ISODate);
            sf.close();
            if (last.isValid() && last.daysTo(QDateTime::currentDateTime()) < cfg.autoBackupDays())
                needBackup = false;
        }

        if (needBackup) {
            // Формируем бекап
            QJsonObject root;
            root["version"]     = "1.0";
            root["exported_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

            QJsonArray allPins;
            auto addPins = [&](const QString &folder) {
                for (const PinItem &p : db.getPins(folder)) {
                    QJsonObject o;
                    o["folder"]   = p.folder; o["name"] = p.name;
                    o["type"]     = p.type;   o["content"] = p.content;
                    o["filepath"] = p.filepath;
                    allPins.append(o);
                }
            };
            addPins("");
            for (const FolderItem &f : db.getAllFolders()) addPins(f.name);
            root["pins"] = allPins;

            QJsonArray profArr;
            for (const ProfileItem &p : db.getProfiles()) {
                QJsonObject o;
                o["name"] = p.name; o["hotkey"] = p.hotkey; o["text"] = p.text;
                profArr.append(o);
            }
            root["profiles"] = profArr;

            QString fname = backupDir + "/smartclip_backup_"
                + QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".json";
            QFile f(fname);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
                f.close();
                // Обновляем дату бекапа
                if (sf.open(QIODevice::WriteOnly)) {
                    sf.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
                    sf.close();
                }
                qDebug() << "Автобекап сохранён:" << fname;
            }
        }
    }

    // ── Мониторинг папки видеозаписей (Бета) ─────────────────────────────────
    VideoWatcher videoWatcher(&db);
    if (cfg.videoMonitorEnabled() && !cfg.videoMonitorPath().isEmpty()) {
        videoWatcher.start(cfg.videoMonitorPath());
        QObject::connect(&videoWatcher, &VideoWatcher::newVideoAdded,
                         &window, &MainWindow::markHistoryDirty);
    }
    // Обновляем VideoWatcher при смене настроек
    QObject::connect(&window, &MainWindow::settingsChanged, [&]() {
        auto &s = AppSettings::get();
        if (s.videoMonitorEnabled())
            videoWatcher.setFolder(s.videoMonitorPath());
        else
            videoWatcher.stop();
    });

    // ── Проверка обновлений (через 10 сек после старта, чтобы не тормозить запуск) ──
    auto *updater = new UpdateChecker(&app);
    QObject::connect(updater, &UpdateChecker::updateAvailable,
                     &window, &MainWindow::onUpdateAvailable);
    QTimer::singleShot(10000, updater, [updater]() { updater->check(true); });

    // Запускаем watchdog в отдельном потоке
    Watchdog watchdog(&clipboard);
    watchdog.start();

    int result = app.exec();

    // Останавливаем всё при выходе
    watchdog.stop();

    // Если был запрошен перезапуск (смена языка) — запускаем новый процесс
    // К этому моменту всё уже освобождено: хоткеи, БД, окна
    if (AppSettings::get().restartRequested()) {
        AppSettings::get().setRestartRequested(false);
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    }

    return result;
}
