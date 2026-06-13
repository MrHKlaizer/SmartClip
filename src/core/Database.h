#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QList>
#include <QStringList>

// Структура одной записи истории
struct HistoryItem {
    int     id;
    QString type;
    QString content;
    QString filepath;
    QString createdAt;
    QString appName;   // имя .exe приложения из которого скопировали
};

// Структура закрепа
struct PinItem {
    int     id;
    QString folder;
    QString name;
    QString type;
    QString content;
    QString filepath;
    QString tags;     // теги через пробел, без # (например: "пират море")
    QString lastUsed; // ISO datetime последнего использования или ""
};

// Структура папки закрепов
struct FolderItem {
    int     id;
    QString name;
    bool    pinned = false;  // закреплена ли папка (всегда первая)
};

// Структура профиля горячих клавиш
struct ProfileItem {
    int     id;
    QString name;
    QString hotkey;
    QString text;
};

class Database
{
public:
    bool init();
    void close();

    QString imagesPath() const { return m_imagesPath; }

    // CRUD для history
    bool addHistory(const QString &type, const QString &content,
                    const QString &filepath = "", const QString &appName = "");
    void autocleanByDays(int days); // удалить записи старше N дней
    QList<HistoryItem> getHistory(int limit, int offset);
    QList<HistoryItem> getHistoryByType(const QString &type, int limit, int offset);
    bool deleteHistory(int id);

    // Очистка истории
    int  countHistory(const QString &type = "");              // количество записей
    int  countHistoryByApp(const QString &appName);
    int  deleteAllHistory(const QString &type = "");          // "" = всё, возвращает кол-во
    int  deleteHistoryByApp(const QString &appName);          // очистить по приложению
    QStringList getAppNames();                                // список уникальных приложений

    // CRUD для pins
    bool addPin(const QString &folder, const QString &name, const QString &type,
                const QString &content = "", const QString &filepath = "");
    QList<PinItem> getPins(const QString &folder = "");
    QList<PinItem> getAllPins();   // без фильтра по папке (для поиска)
    bool deletePin(int id);
    bool movePinToFolder(int pinId, const QString &folder);
    bool touchPin(int id);                  // обновить last_used = now, обрезать по лимиту
    void trimRecentPins(int limit);         // сбросить last_used у самых старых сверх лимита

    // CRUD для folders
    bool              addFolder(const QString &name, const QString &parentPath = "");
    QList<FolderItem> getFolders(const QString &parentPath = ""); // папки на одном уровне
    QList<FolderItem> getAllFolders();                            // все папки (для диалога выбора)
    bool              deleteFolder(const QString &name);
    bool              renameFolder(const QString &oldName, const QString &newName);

    // Порядок папок
    bool setFolderPinned(const QString &name, bool pinned);

    // Редактирование закрепа
    bool updatePinContent(int id, const QString &content);
    bool updatePinName(int id, const QString &name);
    bool updatePinTags(int id, const QString &tags);  // теги через пробел без #

    // Порядок закрепов (drag-to-reorder): id → sort_order (1-based)
    bool updatePinsSortOrder(const QList<QPair<int,int>> &idOrderPairs);

    // CRUD для profiles
    bool addProfile(const QString &name, const QString &hotkey, const QString &text);
    bool updateProfile(int id, const QString &name, const QString &hotkey, const QString &text);
    QList<ProfileItem> getProfiles();
    bool deleteProfile(int id);

private:
    QSqlDatabase m_db;
    QString      m_imagesPath;
    bool createTables();
    void trimHistory();
    void migrateIfNeeded(); // добавляет новые колонки в существующую БД
};
