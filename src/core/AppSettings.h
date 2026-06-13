#pragma once

#include <QSettings>
#include <QStringList>
#include <QCoreApplication>
#include <QDir>

// Синглтон настроек приложения.
// Хранит всё в INI-файле: AppData/Roaming/SmartClipApp/SmartClip.ini
// Использование: AppSettings::get().mainHotkey()
class AppSettings
{
public:
    static AppSettings &get()
    {
        static AppSettings instance;
        return instance;
    }

    // ── Система ───────────────────────────────────────────────────────────────
    QString mainHotkey() const
        { return m_s.value("system/hotkey", "Ctrl+Shift+V").toString(); }
    void setMainHotkey(const QString &h)
        { m_s.setValue("system/hotkey", h); m_s.sync(); }

    bool autostart() const
        { return m_s.value("system/autostart", false).toBool(); }
    void setAutostart(bool b)
        { m_s.setValue("system/autostart", b); m_s.sync(); }

    QString language() const
        { return m_s.value("system/language", "en").toString(); }
    void setLanguage(const QString &l)
        { m_s.setValue("system/language", l); m_s.sync(); }

    // Флаг перезапуска — main.cpp проверяет после выхода из app.exec()
    bool restartRequested() const
        { return m_s.value("system/restartRequested", false).toBool(); }
    void setRestartRequested(bool b)
        { m_s.setValue("system/restartRequested", b); m_s.sync(); }

    // ── История ───────────────────────────────────────────────────────────────
    int maxHistoryRecords() const
        { return m_s.value("history/maxRecords", 150).toInt(); }
    void setMaxHistoryRecords(int n)
        { m_s.setValue("history/maxRecords", n); m_s.sync(); }

    int autocleanDays() const
        { return m_s.value("history/autocleanDays", 0).toInt(); }
    void setAutocleanDays(int n)
        { m_s.setValue("history/autocleanDays", n); m_s.sync(); }

    bool saveImages() const
        { return m_s.value("history/saveImages", true).toBool(); }
    void setSaveImages(bool b)
        { m_s.setValue("history/saveImages", b); m_s.sync(); }

    bool deduplication() const
        { return m_s.value("history/dedup", false).toBool(); }
    void setDeduplication(bool b)
        { m_s.setValue("history/dedup", b); m_s.sync(); }

    int minTextLength() const
        { return m_s.value("history/minLength", 1).toInt(); }
    void setMinTextLength(int n)
        { m_s.setValue("history/minLength", n); m_s.sync(); }

    QStringList excludedApps() const
        { return m_s.value("history/excludedApps").toStringList(); }
    void setExcludedApps(const QStringList &v)
        { m_s.setValue("history/excludedApps", v); m_s.sync(); }

    QString imageFormat() const
        { return m_s.value("history/imageFormat", "PNG").toString(); }
    void setImageFormat(const QString &f)
        { m_s.setValue("history/imageFormat", f); m_s.sync(); }

    int imageQuality() const
        { return m_s.value("history/imageQuality", 90).toInt(); }
    void setImageQuality(int q)
        { m_s.setValue("history/imageQuality", q); m_s.sync(); }

    // ── Закрепы ───────────────────────────────────────────────────────────────
    bool pinsNoName() const
        { return m_s.value("pins/noName", false).toBool(); }
    void setPinsNoName(bool b)
        { m_s.setValue("pins/noName", b); m_s.sync(); }

    bool pinsNoFolder() const
        { return m_s.value("pins/noFolder", false).toBool(); }
    void setPinsNoFolder(bool b)
        { m_s.setValue("pins/noFolder", b); m_s.sync(); }

    // Сколько «недавно использованных» закрепов показывать во вкладке Все
    int recentPinsLimit() const
        { return m_s.value("pins/recentLimit", 50).toInt(); }
    void setRecentPinsLimit(int n)
        { m_s.setValue("pins/recentLimit", n); m_s.sync(); }

    // ── Внешний вид ───────────────────────────────────────────────────────────
    bool solidPanels() const
        { return m_s.value("ui/solidPanels", false).toBool(); }
    void setSolidPanels(bool b)
        { m_s.setValue("ui/solidPanels", b); m_s.sync(); }

    // ── Обратная связь (антиспам) ─────────────────────────────────────────────
    QString lastReportTime() const
        { return m_s.value("report/lastTime", "").toString(); }
    void setLastReportTime(const QString &t)
        { m_s.setValue("report/lastTime", t); m_s.sync(); }

    // ── Бета: мониторинг папки видеозаписей ──────────────────────────────────
    bool videoMonitorEnabled() const
        { return m_s.value("beta/videoMonitor", false).toBool(); }
    void setVideoMonitorEnabled(bool b)
        { m_s.setValue("beta/videoMonitor", b); m_s.sync(); }

    QString videoMonitorPath() const {
        QString def = QDir::homePath() + "/Videos/Captures";
        return m_s.value("beta/videoPath", def).toString();
    }
    void setVideoMonitorPath(const QString &p)
        { m_s.setValue("beta/videoPath", p); m_s.sync(); }

    // ── Экспорт / Импорт ──────────────────────────────────────────────────────
    bool autoBackup() const
        { return m_s.value("backup/auto", false).toBool(); }
    void setAutoBackup(bool b)
        { m_s.setValue("backup/auto", b); m_s.sync(); }

    int autoBackupDays() const
        { return m_s.value("backup/days", 7).toInt(); }
    void setAutoBackupDays(int n)
        { m_s.setValue("backup/days", n); m_s.sync(); }

    QString autoBackupPath() const
        { return m_s.value("backup/path", "").toString(); }
    void setAutoBackupPath(const QString &p)
        { m_s.setValue("backup/path", p); m_s.sync(); }

private:
    AppSettings()
        : m_s(QSettings::IniFormat, QSettings::UserScope,
              "SmartClipApp", "SmartClip")
    {}

    // Запрещаем копирование синглтона
    AppSettings(const AppSettings &) = delete;
    AppSettings &operator=(const AppSettings &) = delete;

    QSettings m_s;
};
