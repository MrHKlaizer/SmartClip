#include "Database.h"
#include "AppSettings.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDebug>

bool Database::init()
{
    // Находим папку AppData/Roaming — стандартное место для данных приложений
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    // Создаём папку SmartClip если её нет
    QDir dir;
    if (!dir.exists(dataPath))
        dir.mkpath(dataPath);

    // Создаём папку для картинок
    m_imagesPath = dataPath + "/images";
    if (!dir.exists(m_imagesPath))
        dir.mkpath(m_imagesPath);

    // Полный путь к файлу базы данных
    QString dbPath = dataPath + "/smartclip.db";

    // Создаём соединение с SQLite
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbPath);

    // Открываем файл БД (если не существует — SQLite создаст его)
    if (!m_db.open()) {
        qDebug() << "Ошибка открытия БД:" << m_db.lastError().text();
        return false;
    }

    qDebug() << "БД открыта:" << dbPath;

    // Создаём таблицы если их ещё нет
    if (!createTables()) return false;

    // Добавляем новые колонки если обновляемся со старой версии БД
    migrateIfNeeded();

    // Автоочистка при старте (если настроена)
    int days = AppSettings::get().autocleanDays();
    if (days > 0)
        autocleanByDays(days);

    return true;
}

bool Database::createTables()
{
    QSqlQuery query;

    // Таблица истории — всё что копировал пользователь
    // type: "text" или "image"
    // content: текст (если text) или пустая строка (если image)
    // filepath: путь к файлу картинки (если image) или пустая строка (если text)
    bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "type TEXT NOT NULL,"
        "content TEXT,"
        "filepath TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    if (!ok) {
        qDebug() << "Ошибка создания таблицы history:" << query.lastError().text();
        return false;
    }

    // Таблица закрепов
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS pins ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "folder TEXT NOT NULL,"
        "name TEXT NOT NULL,"
        "type TEXT NOT NULL,"
        "content TEXT,"
        "filepath TEXT"
        ")"
    );
    if (!ok) {
        qDebug() << "Ошибка создания таблицы pins:" << query.lastError().text();
        return false;
    }

    // Таблица профилей горячих клавиш
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS profiles ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "hotkey TEXT NOT NULL,"
        "text TEXT NOT NULL"
        ")"
    );
    if (!ok) {
        qDebug() << "Ошибка создания таблицы profiles:" << query.lastError().text();
        return false;
    }

    // Таблица папок закрепов
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS folders ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL UNIQUE"
        ")"
    );
    if (!ok) {
        qDebug() << "Ошибка создания таблицы folders:" << query.lastError().text();
        return false;
    }

    qDebug() << "Таблицы созданы успешно";
    return true;
}

void Database::migrateIfNeeded()
{
    // Миграция 1: app_name в history
    QSqlQuery check;
    check.exec("PRAGMA table_info(history)");
    bool hasAppName = false;
    while (check.next()) {
        if (check.value("name").toString() == "app_name") {
            hasAppName = true;
            break;
        }
    }
    if (!hasAppName) {
        QSqlQuery alter;
        alter.exec("ALTER TABLE history ADD COLUMN app_name TEXT DEFAULT ''");
        qDebug() << "Миграция: добавлена колонка app_name";
    }

    // Миграция 2: tags в pins
    QSqlQuery checkTags;
    checkTags.exec("PRAGMA table_info(pins)");
    bool hasTags = false;
    while (checkTags.next()) {
        if (checkTags.value("name").toString() == "tags") { hasTags = true; break; }
    }
    if (!hasTags) {
        QSqlQuery alter;
        alter.exec("ALTER TABLE pins ADD COLUMN tags TEXT DEFAULT ''");
        qDebug() << "Миграция: добавлена колонка tags в pins";
    }

    // Миграция 3: pinned в folders
    QSqlQuery check2;
    check2.exec("PRAGMA table_info(folders)");
    bool hasPinned = false;
    while (check2.next()) {
        if (check2.value("name").toString() == "pinned") {
            hasPinned = true;
            break;
        }
    }
    if (!hasPinned) {
        QSqlQuery alter;
        alter.exec("ALTER TABLE folders ADD COLUMN pinned INTEGER DEFAULT 0");
        qDebug() << "Миграция: добавлена колонка pinned в folders";
    }

    // Миграция 4: last_used в pins (для «недавно использованных» во вкладке Все)
    QSqlQuery checkLastUsed;
    checkLastUsed.exec("PRAGMA table_info(pins)");
    bool hasLastUsed = false;
    while (checkLastUsed.next()) {
        if (checkLastUsed.value("name").toString() == "last_used") { hasLastUsed = true; break; }
    }
    if (!hasLastUsed) {
        QSqlQuery alter;
        alter.exec("ALTER TABLE pins ADD COLUMN last_used TEXT DEFAULT NULL");
        qDebug() << "Миграция: добавлена колонка last_used в pins";
    }

    // Миграция 5: sort_order в pins (пользовательский порядок через drag-to-reorder)
    QSqlQuery checkSortOrder;
    checkSortOrder.exec("PRAGMA table_info(pins)");
    bool hasSortOrder = false;
    while (checkSortOrder.next()) {
        if (checkSortOrder.value("name").toString() == "sort_order") { hasSortOrder = true; break; }
    }
    if (!hasSortOrder) {
        QSqlQuery alter;
        alter.exec("ALTER TABLE pins ADD COLUMN sort_order INTEGER DEFAULT 0");
        qDebug() << "Миграция: добавлена колонка sort_order в pins";
    }
}

void Database::close()
{
    m_db.close();
}

bool Database::addHistory(const QString &type, const QString &content,
                          const QString &filepath, const QString &appName)
{
    QSqlQuery query;
    query.prepare("INSERT INTO history (type, content, filepath, app_name) "
                  "VALUES (:type, :content, :filepath, :app_name)");
    query.bindValue(":type",     type);
    query.bindValue(":content",  content);
    query.bindValue(":filepath", filepath);
    query.bindValue(":app_name", appName);

    if (!query.exec()) {
        qDebug() << "Ошибка добавления в history:" << query.lastError().text();
        return false;
    }

    // После каждого добавления проверяем лимит
    trimHistory();
    return true;
}

void Database::autocleanByDays(int days)
{
    if (days <= 0) return;

    // Сначала удаляем файлы картинок которые попадают под очистку
    QSqlQuery fpQuery;
    fpQuery.prepare(
        "SELECT filepath FROM history "
        "WHERE filepath != '' AND created_at < datetime('now', :days)"
    );
    fpQuery.bindValue(":days", QString("-%1 days").arg(days));
    fpQuery.exec();
    while (fpQuery.next()) {
        QString fp = fpQuery.value(0).toString();
        if (!fp.isEmpty()) QFile::remove(fp);
    }

    // Удаляем старые записи
    QSqlQuery q;
    q.prepare("DELETE FROM history WHERE created_at < datetime('now', :days)");
    q.bindValue(":days", QString("-%1 days").arg(days));
    if (q.exec())
        qDebug() << "Автоочистка: удалено записей старше" << days << "дней:" << q.numRowsAffected();
}

void Database::trimHistory()
{
    const int MAX_HISTORY = AppSettings::get().maxHistoryRecords();

    // Считаем сколько записей в таблице
    QSqlQuery countQuery;
    countQuery.exec("SELECT COUNT(*) FROM history");
    countQuery.next();
    int count = countQuery.value(0).toInt();

    // Если превысили лимит — удаляем самые старые
    if (count > MAX_HISTORY) {
        int toDelete = count - MAX_HISTORY;
        QSqlQuery deleteQuery;
        deleteQuery.prepare(
            "DELETE FROM history WHERE id IN "
            "(SELECT id FROM history ORDER BY id ASC LIMIT :n)"
        );
        deleteQuery.bindValue(":n", toDelete);
        deleteQuery.exec();
        qDebug() << "Удалено старых записей:" << toDelete;
    }
}

QList<HistoryItem> Database::getHistory(int limit, int offset)
{
    QList<HistoryItem> items;

    QSqlQuery query;
    query.prepare("SELECT id, type, content, filepath, created_at, app_name FROM history ORDER BY id DESC LIMIT :limit OFFSET :offset");
    query.bindValue(":limit", limit);
    query.bindValue(":offset", offset);

    if (!query.exec()) {
        qDebug() << "Ошибка чтения history:" << query.lastError().text();
        return items;
    }

    // Проходим по всем строкам результата
    while (query.next()) {
        HistoryItem item;
        item.id        = query.value("id").toInt();
        item.type      = query.value("type").toString();
        item.content   = query.value("content").toString();
        item.filepath  = query.value("filepath").toString();
        item.createdAt = query.value("created_at").toString();
        item.appName   = query.value("app_name").toString();
        items.append(item);
    }

    return items;
}

QList<HistoryItem> Database::getHistoryByType(const QString &type, int limit, int offset)
{
    QList<HistoryItem> items;
    QSqlQuery query;
    query.prepare(
        "SELECT id, type, content, filepath, created_at, app_name FROM history "
        "WHERE type = :type ORDER BY id DESC LIMIT :limit OFFSET :offset"
    );
    query.bindValue(":type",   type);
    query.bindValue(":limit",  limit);
    query.bindValue(":offset", offset);

    if (!query.exec()) {
        qDebug() << "Ошибка getHistoryByType:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        HistoryItem item;
        item.id        = query.value("id").toInt();
        item.type      = query.value("type").toString();
        item.content   = query.value("content").toString();
        item.filepath  = query.value("filepath").toString();
        item.createdAt = query.value("created_at").toString();
        item.appName   = query.value("app_name").toString();
        items.append(item);
    }
    return items;
}

// ── Очистка истории ───────────────────────────────────────────────────────────

int Database::countHistory(const QString &type)
{
    QSqlQuery q;
    if (type.isEmpty())
        q.exec("SELECT COUNT(*) FROM history");
    else {
        q.prepare("SELECT COUNT(*) FROM history WHERE type = :type");
        q.bindValue(":type", type);
        q.exec();
    }
    return q.next() ? q.value(0).toInt() : 0;
}

int Database::countHistoryByApp(const QString &appName)
{
    QSqlQuery q;
    q.prepare("SELECT COUNT(*) FROM history WHERE app_name = :app");
    q.bindValue(":app", appName);
    q.exec();
    return q.next() ? q.value(0).toInt() : 0;
}

int Database::deleteAllHistory(const QString &type)
{
    // Сначала удаляем файлы изображений с диска
    QSqlQuery fpQuery;
    if (type.isEmpty() || type == "image") {
        QString sql = "SELECT filepath FROM history WHERE filepath != ''";
        if (type == "image") sql += " AND type = 'image'";
        fpQuery.exec(sql);
        while (fpQuery.next()) {
            QString fp = fpQuery.value(0).toString();
            if (!fp.isEmpty()) QFile::remove(fp);
        }
    }

    QSqlQuery query;
    if (type.isEmpty()) {
        query.exec("DELETE FROM history");
    } else {
        query.prepare("DELETE FROM history WHERE type = :type");
        query.bindValue(":type", type);
        query.exec();
    }
    qDebug() << "Очищено записей истории:" << query.numRowsAffected();
    return query.numRowsAffected();
}

int Database::deleteHistoryByApp(const QString &appName)
{
    // Удаляем файлы изображений
    QSqlQuery fpQuery;
    fpQuery.prepare("SELECT filepath FROM history WHERE app_name = :app AND filepath != ''");
    fpQuery.bindValue(":app", appName);
    fpQuery.exec();
    while (fpQuery.next()) {
        QString fp = fpQuery.value(0).toString();
        if (!fp.isEmpty()) QFile::remove(fp);
    }

    QSqlQuery query;
    query.prepare("DELETE FROM history WHERE app_name = :app");
    query.bindValue(":app", appName);
    query.exec();
    qDebug() << "Удалено из" << appName << ":" << query.numRowsAffected();
    return query.numRowsAffected();
}

QStringList Database::getAppNames()
{
    QStringList names;
    QSqlQuery query;
    query.exec("SELECT DISTINCT app_name FROM history "
               "WHERE app_name != '' ORDER BY app_name ASC");
    while (query.next())
        names << query.value(0).toString();
    return names;
}

// ─────────────────────────────────────────────────────────────────────────────

bool Database::deleteHistory(int id)
{
    QSqlQuery query;
    query.prepare("DELETE FROM history WHERE id = :id");
    query.bindValue(":id", id);

    if (!query.exec()) {
        qDebug() << "Ошибка удаления из history:" << query.lastError().text();
        return false;
    }
    return true;
}

// ── PINS ──────────────────────────────────────────────────────────────────────

bool Database::addPin(const QString &folder, const QString &name, const QString &type,
                      const QString &content, const QString &filepath)
{
    QSqlQuery query;
    query.prepare("INSERT INTO pins (folder, name, type, content, filepath) "
                  "VALUES (:folder, :name, :type, :content, :filepath)");
    query.bindValue(":folder",   folder);
    query.bindValue(":name",     name);
    query.bindValue(":type",     type);
    query.bindValue(":content",  content);
    query.bindValue(":filepath", filepath);

    if (!query.exec()) {
        qDebug() << "Ошибка добавления в pins:" << query.lastError().text();
        return false;
    }
    return true;
}

QList<PinItem> Database::getPins(const QString &folder)
{
    QList<PinItem> items;
    QSqlQuery query;

    if (folder.isEmpty()) {
        // Вкладка «Все»: пины без папки + недавно использованные из любой папки.
        // Сортируем: недавно использованные (last_used IS NOT NULL) первыми по убыванию,
        // потом без папки по id DESC.
        query.prepare(
            "SELECT id, folder, name, type, content, filepath, tags, last_used FROM pins "
            "WHERE folder = '' OR (folder != '' AND last_used IS NOT NULL) "
            "ORDER BY CASE WHEN last_used IS NOT NULL THEN last_used ELSE '' END DESC, id DESC"
        );
    } else {
        // Конкретная папка — фильтруем строго по ней, уважаем пользовательский порядок
        // sort_order=0 (новые) идут первыми по id DESC; явно упорядоченные — по sort_order ASC
        query.prepare(
            "SELECT id, folder, name, type, content, filepath, tags, last_used FROM pins "
            "WHERE folder = :folder "
            "ORDER BY sort_order ASC, id DESC"
        );
        query.bindValue(":folder", folder);
    }

    if (!query.exec()) {
        qDebug() << "Ошибка чтения pins:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        PinItem item;
        item.id       = query.value("id").toInt();
        item.folder   = query.value("folder").toString();
        item.name     = query.value("name").toString();
        item.type     = query.value("type").toString();
        item.content  = query.value("content").toString();
        item.filepath = query.value("filepath").toString();
        item.tags     = query.value("tags").toString();
        item.lastUsed = query.value("last_used").toString();
        items.append(item);
    }
    return items;
}

QList<PinItem> Database::getAllPins()
{
    QList<PinItem> items;
    QSqlQuery query;
    query.prepare("SELECT id, folder, name, type, content, filepath, tags, last_used FROM pins");
    if (!query.exec()) {
        qDebug() << "Ошибка чтения pins (all):" << query.lastError().text();
        return items;
    }
    while (query.next()) {
        PinItem item;
        item.id       = query.value("id").toInt();
        item.folder   = query.value("folder").toString();
        item.name     = query.value("name").toString();
        item.type     = query.value("type").toString();
        item.content  = query.value("content").toString();
        item.filepath = query.value("filepath").toString();
        item.tags     = query.value("tags").toString();
        item.lastUsed = query.value("last_used").toString();
        items.append(item);
    }
    return items;
}

bool Database::touchPin(int id)
{
    QSqlQuery q;
    q.prepare("UPDATE pins SET last_used = datetime('now') WHERE id = :id");
    q.bindValue(":id", id);
    if (!q.exec()) {
        qDebug() << "Ошибка touchPin:" << q.lastError().text();
        return false;
    }
    // Обрезаем до лимита — только пины из папок (без папки всегда видны)
    int limit = AppSettings::get().recentPinsLimit();
    trimRecentPins(limit);
    return true;
}

void Database::trimRecentPins(int limit)
{
    // Сбрасываем last_used у самых старых «недавно использованных» сверх лимита.
    // Пины без папки не трогаем — они всегда в Все.
    QSqlQuery q;
    q.prepare(
        "UPDATE pins SET last_used = NULL "
        "WHERE folder != '' AND last_used IS NOT NULL "
        "AND id NOT IN ("
        "  SELECT id FROM pins WHERE folder != '' AND last_used IS NOT NULL "
        "  ORDER BY last_used DESC LIMIT :lim"
        ")"
    );
    q.bindValue(":lim", limit);
    q.exec();
}

bool Database::deletePin(int id)
{
    QSqlQuery query;
    query.prepare("DELETE FROM pins WHERE id = :id");
    query.bindValue(":id", id);

    if (!query.exec()) {
        qDebug() << "Ошибка удаления из pins:" << query.lastError().text();
        return false;
    }
    return true;
}

// ── PROFILES ──────────────────────────────────────────────────────────────────

bool Database::addProfile(const QString &name, const QString &hotkey, const QString &text)
{
    QSqlQuery query;
    query.prepare("INSERT INTO profiles (name, hotkey, text) VALUES (:name, :hotkey, :text)");
    query.bindValue(":name",   name);
    query.bindValue(":hotkey", hotkey);
    query.bindValue(":text",   text);

    if (!query.exec()) {
        qDebug() << "Ошибка добавления в profiles:" << query.lastError().text();
        return false;
    }
    return true;
}

QList<ProfileItem> Database::getProfiles()
{
    QList<ProfileItem> items;
    QSqlQuery query;

    if (!query.exec("SELECT id, name, hotkey, text FROM profiles")) {
        qDebug() << "Ошибка чтения profiles:" << query.lastError().text();
        return items;
    }

    while (query.next()) {
        ProfileItem item;
        item.id     = query.value("id").toInt();
        item.name   = query.value("name").toString();
        item.hotkey = query.value("hotkey").toString();
        item.text   = query.value("text").toString();
        items.append(item);
    }
    return items;
}

bool Database::updateProfile(int id, const QString &name,
                             const QString &hotkey, const QString &text)
{
    QSqlQuery query;
    query.prepare(
        "UPDATE profiles SET name = :name, hotkey = :hotkey, text = :text WHERE id = :id"
    );
    query.bindValue(":name",   name);
    query.bindValue(":hotkey", hotkey);
    query.bindValue(":text",   text);
    query.bindValue(":id",     id);
    if (!query.exec()) {
        qDebug() << "Ошибка обновления профиля:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Database::deleteProfile(int id)
{
    QSqlQuery query;
    query.prepare("DELETE FROM profiles WHERE id = :id");
    query.bindValue(":id", id);

    if (!query.exec()) {
        qDebug() << "Ошибка удаления из profiles:" << query.lastError().text();
        return false;
    }
    return true;
}

// ── FOLDERS ───────────────────────────────────────────────────────────────────

bool Database::addFolder(const QString &name, const QString &parentPath)
{
    // Полный путь: "Работа" или "Работа/Клиент1"
    QString fullName = parentPath.isEmpty() ? name : parentPath + "/" + name;
    QSqlQuery query;
    query.prepare("INSERT OR IGNORE INTO folders (name) VALUES (:name)");
    query.bindValue(":name", fullName);
    if (!query.exec()) {
        qDebug() << "Ошибка добавления папки:" << query.lastError().text();
        return false;
    }
    return true;
}

QList<FolderItem> Database::getFolders(const QString &parentPath)
{
    // Возвращает папки на одном уровне иерархии
    QList<FolderItem> items;
    QSqlQuery query;

    if (parentPath.isEmpty()) {
        // Корень: только папки без '/' в имени; закреплённые — первыми
        query.exec(
            "SELECT id, name, pinned FROM folders "
            "WHERE name NOT LIKE '%/%' "
            "ORDER BY pinned DESC, name ASC"
        );
    } else {
        // Один уровень вглубь: "Работа/%" но НЕ "Работа/%/%"
        QString prefix  = parentPath + "/%";
        QString exclude = parentPath + "/%/%";
        query.prepare(
            "SELECT id, name, pinned FROM folders "
            "WHERE name LIKE :prefix AND name NOT LIKE :exclude "
            "ORDER BY pinned DESC, name ASC"
        );
        query.bindValue(":prefix",  prefix);
        query.bindValue(":exclude", exclude);
        query.exec();
    }

    while (query.next()) {
        FolderItem item;
        item.id     = query.value("id").toInt();
        item.name   = query.value("name").toString();
        item.pinned = query.value("pinned").toInt() != 0;
        items.append(item);
    }
    return items;
}

QList<FolderItem> Database::getAllFolders()
{
    // Все папки — для диалога выбора при закреплении
    QList<FolderItem> items;
    QSqlQuery query;
    if (!query.exec("SELECT id, name, pinned FROM folders ORDER BY pinned DESC, name ASC")) {
        qDebug() << "Ошибка чтения folders:" << query.lastError().text();
        return items;
    }
    while (query.next()) {
        FolderItem item;
        item.id     = query.value("id").toInt();
        item.name   = query.value("name").toString();
        item.pinned = query.value("pinned").toInt() != 0;
        items.append(item);
    }
    return items;
}

bool Database::deleteFolder(const QString &name)
{
    // Перемещаем закрепы из этой папки И подпапок в "без папки"
    QSqlQuery movePins;
    movePins.prepare(
        "UPDATE pins SET folder = '' "
        "WHERE folder = :name OR folder LIKE :prefix"
    );
    movePins.bindValue(":name",   name);
    movePins.bindValue(":prefix", name + "/%");
    movePins.exec();

    // Удаляем все подпапки
    QSqlQuery deleteSubs;
    deleteSubs.prepare("DELETE FROM folders WHERE name LIKE :prefix");
    deleteSubs.bindValue(":prefix", name + "/%");
    deleteSubs.exec();

    // Удаляем саму папку
    QSqlQuery query;
    query.prepare("DELETE FROM folders WHERE name = :name");
    query.bindValue(":name", name);
    if (!query.exec()) {
        qDebug() << "Ошибка удаления папки:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Database::renameFolder(const QString &oldName, const QString &newName)
{
    // Получаем все подпапки для каскадного переименования
    QSqlQuery getSubs;
    getSubs.prepare("SELECT name FROM folders WHERE name LIKE :prefix");
    getSubs.bindValue(":prefix", oldName + "/%");
    getSubs.exec();

    QStringList subfolders;
    while (getSubs.next())
        subfolders << getSubs.value(0).toString();

    // Переименовываем подпапки (заменяем префикс пути)
    for (const QString &sub : subfolders) {
        QString newSub = newName + sub.mid(oldName.length());

        QSqlQuery updateSub;
        updateSub.prepare("UPDATE folders SET name = :new WHERE name = :old");
        updateSub.bindValue(":new", newSub);
        updateSub.bindValue(":old", sub);
        updateSub.exec();

        QSqlQuery updatePins;
        updatePins.prepare("UPDATE pins SET folder = :new WHERE folder = :old");
        updatePins.bindValue(":new", newSub);
        updatePins.bindValue(":old", sub);
        updatePins.exec();
    }

    // Переименовываем саму папку
    QSqlQuery q1;
    q1.prepare("UPDATE folders SET name = :new WHERE name = :old");
    q1.bindValue(":new", newName);
    q1.bindValue(":old", oldName);
    if (!q1.exec()) {
        qDebug() << "Ошибка переименования папки:" << q1.lastError().text();
        return false;
    }

    // Обновляем закрепы в переименованной папке
    QSqlQuery q2;
    q2.prepare("UPDATE pins SET folder = :new WHERE folder = :old");
    q2.bindValue(":new", newName);
    q2.bindValue(":old", oldName);
    q2.exec();

    return true;
}

bool Database::setFolderPinned(const QString &name, bool pinned)
{
    QSqlQuery query;
    query.prepare("UPDATE folders SET pinned = :pinned WHERE name = :name");
    query.bindValue(":pinned", pinned ? 1 : 0);
    query.bindValue(":name",   name);
    return query.exec();
}

bool Database::updatePinContent(int id, const QString &content)
{
    QSqlQuery query;
    query.prepare("UPDATE pins SET content = :content WHERE id = :id");
    query.bindValue(":content", content);
    query.bindValue(":id",      id);
    if (!query.exec()) {
        qDebug() << "Ошибка обновления текста закрепа:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Database::updatePinName(int id, const QString &name)
{
    QSqlQuery query;
    query.prepare("UPDATE pins SET name = :name WHERE id = :id");
    query.bindValue(":name", name);
    query.bindValue(":id",   id);
    if (!query.exec()) {
        qDebug() << "Ошибка переименования закрепа:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Database::updatePinTags(int id, const QString &tags)
{
    QSqlQuery query;
    query.prepare("UPDATE pins SET tags = :tags WHERE id = :id");
    query.bindValue(":tags", tags);
    query.bindValue(":id",   id);
    if (!query.exec()) {
        qDebug() << "Ошибка обновления тегов закрепа:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Database::updatePinsSortOrder(const QList<QPair<int,int>> &idOrderPairs)
{
    QSqlQuery query;
    for (const auto &p : idOrderPairs) {
        query.prepare("UPDATE pins SET sort_order = :order WHERE id = :id");
        query.bindValue(":order", p.second);
        query.bindValue(":id",    p.first);
        if (!query.exec())
            qDebug() << "Ошибка обновления sort_order:" << query.lastError().text();
    }
    return true;
}

bool Database::movePinToFolder(int pinId, const QString &folder)
{
    QSqlQuery query;
    query.prepare("UPDATE pins SET folder = :folder WHERE id = :id");
    query.bindValue(":folder", folder);
    query.bindValue(":id",     pinId);
    if (!query.exec()) {
        qDebug() << "Ошибка перемещения закрепа:" << query.lastError().text();
        return false;
    }
    return true;
}
