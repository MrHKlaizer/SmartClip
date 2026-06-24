// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  AppSettings.h — Хранилище всех настроек SmartClip                        ║
// ║                                                                              ║
// ║  AppSettings — это «синглтон»: один объект на всю программу, доступный    ║
// ║  из любого места кода через AppSettings::get().                             ║
// ║                                                                              ║
// ║  Настройки хранятся в INI-файле на диске:                                  ║
// ║    C:\Users\<имя>\AppData\Roaming\SmartClipApp\SmartClip.ini               ║
// ║                                                                              ║
// ║  Каждая настройка — это пара ключ/значение. Пример из файла:               ║
// ║    [system]                                                                  ║
// ║    hotkey=Ctrl+Shift+V                                                       ║
// ║    language=ru                                                               ║
// ║    [history]                                                                 ║
// ║    maxRecords=150                                                            ║
// ║                                                                              ║
// ║  Паттерн использования:                                                      ║
// ║    AppSettings::get().mainHotkey()          // читаем                        ║
// ║    AppSettings::get().setMainHotkey("...");  // пишем                       ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

// QSettings — класс Qt для работы с настройками (INI, реестр Windows и др.)
// Умеет читать/писать ключ-значение и синхронизировать с файлом на диске.
#include <QSettings>
// QStringList — список строк (нужен для исключённых приложений)
#include <QStringList>
// QCoreApplication — нужен для определения пути к AppData (через Qt API)
#include <QCoreApplication>
// QDir — работа с путями и папками (для пути по умолчанию к папке видео)
#include <QDir>

// ─── Класс AppSettings ────────────────────────────────────────────────────────
// Синглтон — паттерн проектирования при котором класс создаётся только один раз.
// Гарантирует что все части программы работают с одними и теми же настройками.
class AppSettings
{
public:
    // Получить единственный экземпляр AppSettings.
    // static — значит принадлежит классу, а не объекту. Можно вызвать без объекта.
    // AppSettings &  — возвращаем ссылку (не копию!) на единственный объект.
    // Слово «static» внутри функции = «создай один раз, живи всегда».
    static AppSettings &get()
    {
        static AppSettings instance;  // Создаётся один раз при первом вызове get()
        return instance;
    }

    // ─── Системные настройки (раздел [system] в INI) ─────────────────────────

    // Горячая клавиша для открытия SmartClip. По умолчанию: "Ctrl+Shift+V".
    // m_s.value(ключ, значение_по_умолчанию) — читает из INI или возвращает дефолт.
    QString mainHotkey() const
        { return m_s.value("system/hotkey", "Ctrl+Shift+V").toString(); }
    void setMainHotkey(const QString &h)
        { m_s.setValue("system/hotkey", h); m_s.sync(); }
    // m_s.sync() — записать на диск немедленно (иначе Qt делает это сам при закрытии)

    // Автозапуск при входе в Windows. По умолчанию: выключен.
    bool autostart() const
        { return m_s.value("system/autostart", false).toBool(); }
    void setAutostart(bool b)
        { m_s.setValue("system/autostart", b); m_s.sync(); }

    // Язык интерфейса: "en", "de", "ua". По умолчанию: "en".
    QString language() const
        { return m_s.value("system/language", "en").toString(); }
    void setLanguage(const QString &l)
        { m_s.setValue("system/language", l); m_s.sync(); }

    // Флаг «нужен перезапуск» — устанавливается при смене языка/масштаба.
    // main.cpp проверяет его после завершения app.exec() и если true — запускает SmartClip заново.
    bool restartRequested() const
        { return m_s.value("system/restartRequested", false).toBool(); }
    void setRestartRequested(bool b)
        { m_s.setValue("system/restartRequested", b); m_s.sync(); }

    // ─── Настройки истории (раздел [history]) ─────────────────────────────────

    // Максимальное количество записей в истории. По умолчанию: 150.
    // При превышении — самые старые записи автоматически удаляются.
    int maxHistoryRecords() const
        { return m_s.value("history/maxRecords", 150).toInt(); }
    void setMaxHistoryRecords(int n)
        { m_s.setValue("history/maxRecords", n); m_s.sync(); }

    // Автоочистка: удалять записи старше N дней. 0 = выключена.
    int autocleanDays() const
        { return m_s.value("history/autocleanDays", 0).toInt(); }
    void setAutocleanDays(int n)
        { m_s.setValue("history/autocleanDays", n); m_s.sync(); }

    // Сохранять ли скопированные картинки в файлы. По умолчанию: да.
    bool saveImages() const
        { return m_s.value("history/saveImages", true).toBool(); }
    void setSaveImages(bool b)
        { m_s.setValue("history/saveImages", b); m_s.sync(); }

    // Дедупликация: не сохранять если такой же текст уже есть в истории.
    bool deduplication() const
        { return m_s.value("history/dedup", false).toBool(); }
    void setDeduplication(bool b)
        { m_s.setValue("history/dedup", b); m_s.sync(); }

    // Минимальная длина текста для сохранения. 1 = сохранять всё.
    int minTextLength() const
        { return m_s.value("history/minLength", 1).toInt(); }
    void setMinTextLength(int n)
        { m_s.setValue("history/minLength", n); m_s.sync(); }

    // Список приложений из которых НЕ нужно сохранять в историю.
    // Хранится как список строк: ["KeePass.exe", "1Password.exe"]
    QStringList excludedApps() const
        { return m_s.value("history/excludedApps").toStringList(); }
    void setExcludedApps(const QStringList &v)
        { m_s.setValue("history/excludedApps", v); m_s.sync(); }

    // Формат сохранения картинок: "PNG" (без потерь) или "JPG" (с компрессией).
    QString imageFormat() const
        { return m_s.value("history/imageFormat", "PNG").toString(); }
    void setImageFormat(const QString &f)
        { m_s.setValue("history/imageFormat", f); m_s.sync(); }

    // Качество JPG при сохранении картинок: 0-100. 90 = хорошее качество.
    int imageQuality() const
        { return m_s.value("history/imageQuality", 90).toInt(); }
    void setImageQuality(int q)
        { m_s.setValue("history/imageQuality", q); m_s.sync(); }

    // ─── Настройки закрепов (раздел [pins]) ──────────────────────────────────

    // Скрывать имена закрепов — показывать только содержимое.
    bool pinsNoName() const
        { return m_s.value("pins/noName", false).toBool(); }
    void setPinsNoName(bool b)
        { m_s.setValue("pins/noName", b); m_s.sync(); }

    // Скрывать папки — показывать все закрепы плоским списком.
    bool pinsNoFolder() const
        { return m_s.value("pins/noFolder", false).toBool(); }
    void setPinsNoFolder(bool b)
        { m_s.setValue("pins/noFolder", b); m_s.sync(); }

    // Сколько «недавно использованных» закрепов показывать во вкладке Все.
    int recentPinsLimit() const
        { return m_s.value("pins/recentLimit", 50).toInt(); }
    void setRecentPinsLimit(int n)
        { m_s.setValue("pins/recentLimit", n); m_s.sync(); }

    // ─── Внешний вид (раздел [ui]) ────────────────────────────────────────────

    // «Непрозрачные панели» — отключает размытие (для слабых ПК или несовместимых GPU).
    bool solidPanels() const
        { return m_s.value("ui/solidPanels", false).toBool(); }
    void setSolidPanels(bool b)
        { m_s.setValue("ui/solidPanels", b); m_s.sync(); }

    // Масштаб интерфейса в процентах. Допустимые значения: 75, 90, 100, 110, 125.
    // 100 = стандартный размер. Применяется после перезапуска программы.
    int uiScale() const
        { return m_s.value("ui/scale", 100).toInt(); }
    void setUiScale(int pct)
        { m_s.setValue("ui/scale", pct); m_s.sync(); }

    // ─── Обратная связь (раздел [report]) ────────────────────────────────────

    // Время последней отправки багрепорта (ISO-формат).
    // Используется для антиспам-защиты: нельзя отправлять чаще 1 раза в 6 часов.
    QString lastReportTime() const
        { return m_s.value("report/lastTime", "").toString(); }
    void setLastReportTime(const QString &t)
        { m_s.setValue("report/lastTime", t); m_s.sync(); }

    // ─── Бета-функции (раздел [beta]) ─────────────────────────────────────────

    // Включён ли мониторинг папки видеозаписей Xbox Game Bar.
    bool videoMonitorEnabled() const
        { return m_s.value("beta/videoMonitor", false).toBool(); }
    void setVideoMonitorEnabled(bool b)
        { m_s.setValue("beta/videoMonitor", b); m_s.sync(); }

    // Путь к папке с видеозаписями.
    // По умолчанию: %USERPROFILE%/Videos/Captures (стандартный путь Xbox Game Bar).
    QString videoMonitorPath() const {
        QString def = QDir::homePath() + "/Videos/Captures";
        return m_s.value("beta/videoPath", def).toString();
    }
    void setVideoMonitorPath(const QString &p)
        { m_s.setValue("beta/videoPath", p); m_s.sync(); }

    // ─── Резервные копии (раздел [backup]) ───────────────────────────────────

    // Автоматическое создание резервных копий базы данных.
    bool autoBackup() const
        { return m_s.value("backup/auto", false).toBool(); }
    void setAutoBackup(bool b)
        { m_s.setValue("backup/auto", b); m_s.sync(); }

    // Интервал автобэкапа в днях.
    int autoBackupDays() const
        { return m_s.value("backup/days", 7).toInt(); }
    void setAutoBackupDays(int n)
        { m_s.setValue("backup/days", n); m_s.sync(); }

    // Путь к папке где хранить резервные копии.
    QString autoBackupPath() const
        { return m_s.value("backup/path", "").toString(); }
    void setAutoBackupPath(const QString &p)
        { m_s.setValue("backup/path", p); m_s.sync(); }

private:
    // Конструктор приватный — снаружи нельзя создать AppSettings напрямую.
    // Только через AppSettings::get(). Это и есть паттерн «синглтон».
    AppSettings()
        : m_s(QSettings::IniFormat,     // Формат файла: INI (текстовый)
              QSettings::UserScope,      // Область: данные текущего пользователя (AppData/Roaming)
              "SmartClipApp",            // Название компании/приложения (папка)
              "SmartClip")               // Название файла настроек (SmartClip.ini)
    {}

    // Запрещаем копирование синглтона.
    // = delete означает «эта операция запрещена». Если попробовать скопировать —
    // компилятор выдаст ошибку, а не создаст второй экземпляр.
    AppSettings(const AppSettings &) = delete;
    AppSettings &operator=(const AppSettings &) = delete;

    // Объект QSettings — через него происходит чтение/запись INI-файла.
    QSettings m_s;
};
