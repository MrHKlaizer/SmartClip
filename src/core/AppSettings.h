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

    // Работать с правами администратора (через задачу Планировщика, см. AdminMode.h),
    // чтобы Win+V и вставка работали в окнах, открытых от администратора.
    bool runAsAdmin() const
        { return m_s.value("system/runAsAdmin", false).toBool(); }
    void setRunAsAdmin(bool b)
        { m_s.setValue("system/runAsAdmin", b); m_s.sync(); }

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

    // Лимиты истории — ОТДЕЛЬНО для текстов и для картинок (картинки занимают
    // много места, тексты почти нисколько, общий лимит был неудобен: одни
    // вытесняли другие). При превышении самые старые записи этого вида удаляются.
    // Видео из папки записей экрана считаются вместе с картинками.
    // Начальные значения ставит Database::init() (миграция со старого общего
    // лимита history/maxRecords) — см. migrateHistoryLimits().
    int maxTexts() const
        { return qMax(10, m_s.value("history/maxTexts", 40).toInt()); }
    void setMaxTexts(int n)
        { m_s.setValue("history/maxTexts", n); m_s.sync(); }
    int maxImages() const
        { return qMax(5, m_s.value("history/maxImages", 20).toInt()); }
    void setMaxImages(int n)
        { m_s.setValue("history/maxImages", n); m_s.sync(); }

    // Есть ли ключ в настройках (для миграции)
    bool has(const QString &key) const { return m_s.contains(key); }
    // Старый общий лимит (до 2.0): по умолчанию был 150
    int legacyMaxRecords() const
        { return m_s.value("history/maxRecords", 150).toInt(); }

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

    // ─── Оформление и анимации (Настройки → Оформление) ──────────────────────

    // Настройки ОТДЕЛЬНО для каждого вида анимации (kind — "menu", "dialog",
    // "open", "close"; список видов — в src/ui/Anim.h).
    //
    // Шкала дат сбоку колонки при прокрутке (DateRuler.h):
    // "outer" — у края экрана, "inner" — со стороны центра, "off" — выключена
    QString dateRuler() const
        { return m_s.value("ui/dateRuler", "outer").toString(); }
    void setDateRuler(const QString &s)
        { m_s.setValue("ui/dateRuler", s); m_s.sync(); }

    // Карточка выезжает и увеличивается при наведении мыши (CardArc.h)
    bool cardHover() const
        { return m_s.value("ui/cardHover", true).toBool(); }
    void setCardHover(bool b)
        { m_s.setValue("ui/cardHover", b); m_s.sync(); }

    // Стиль прокрутки колонок:
    // "arc" — дуга-барабан, "smooth" — просто плавная. По умолчанию — дуга.
    QString scrollStyle() const
        { return m_s.value("anim/scroll/style", "arc").toString(); }
    void setScrollStyle(const QString &s)
        { m_s.setValue("anim/scroll/style", s); m_s.sync(); }

    // Скорость этого вида в процентах: 100 = обычная, 200 = в 2 раза быстрее,
    // 50 = в 2 раза медленнее. Допустимо 50–200.
    int animSpeed(const QString &kind) const
        { return qBound(50, m_s.value("anim/" + kind + "/speed",
                                      m_s.value("ui/animSpeed", 100)).toInt(), 200); }
    void setAnimSpeed(const QString &kind, int pct)
        { m_s.setValue("anim/" + kind + "/speed", qBound(50, pct, 200)); m_s.sync(); }

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

    // Включён ли автозахват записей экрана и аудио из папок (VideoWatcher).
    bool videoMonitorEnabled() const
        { return m_s.value("beta/videoMonitor", false).toBool(); }
    void setVideoMonitorEnabled(bool b)
        { m_s.setValue("beta/videoMonitor", b); m_s.sync(); }

    // Папки автозахвата (видео и аудио), которые выбрал пользователь.
    // Ещё не выбирал — VideoWatcher::configuredFolders() возьмёт стандартные.
    bool hasMediaFolders() const { return m_s.contains("beta/mediaFolders"); }
    QStringList mediaFolders() const { return m_s.value("beta/mediaFolders").toStringList(); }
    void setMediaFolders(const QStringList &l)
        { m_s.setValue("beta/mediaFolders", l); m_s.sync(); }
    // Одна папка из версий до 2.0.1 (для перехода на список)
    QString legacyVideoPath() const { return m_s.value("beta/videoPath").toString(); }

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
