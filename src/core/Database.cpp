// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  Database.cpp — Реализация работы с базой данных SQLite                    ║
// ║                                                                              ║
// ║  SQLite — это встраиваемая база данных в виде одного файла .db.            ║
// ║  Не требует отдельного сервера — всё работает внутри нашей программы.      ║
// ║                                                                              ║
// ║  Основы SQL которые используются в этом файле:                             ║
// ║  • SELECT — прочитать данные                                                ║
// ║  • INSERT — добавить строку                                                  ║
// ║  • UPDATE — изменить строку                                                  ║
// ║  • DELETE — удалить строку                                                   ║
// ║  • WHERE  — условие фильтрации (как «if» для строк)                         ║
// ║  • ORDER BY — сортировка результата                                          ║
// ║  • LIMIT/OFFSET — пагинация (часть результата)                              ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "Database.h"
#include "AppSettings.h"   // maxHistoryRecords(), autocleanDays() и т.д.

// Qt-модули для работы с SQL:
#include <QSqlError>           // Информация об ошибке SQL-запроса
#include <QSqlQuery>           // Объект для выполнения SQL-запросов
#include <QSqlRecord>          // Строка результата SQL-запроса
#include <QStandardPaths>      // Стандартные пути: AppData, Documents и т.д.
#include <QDir>                // Создание и проверка папок
#include <QFile>               // Удаление файлов картинок с диска
#include <QDebug>              // qDebug() — вывод в консоль разработчика

// ─── Инициализация базы данных ────────────────────────────────────────────────
// Открывает (или создаёт) файл БД и создаёт таблицы если их нет.
bool Database::init()
{
    // AppDataLocation → C:\Users\<имя>\AppData\Roaming\SmartClipApp\SmartClip
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    // Создаём папку SmartClip если её нет (mkpath = mkdir -p)
    QDir dir;
    if (!dir.exists(dataPath))
        dir.mkpath(dataPath);

    // Папка для сохранения скопированных картинок
    m_imagesPath = dataPath + "/images";
    if (!dir.exists(m_imagesPath))
        dir.mkpath(m_imagesPath);

    // Полный путь к файлу базы данных
    QString dbPath = dataPath + "/smartclip.db";

    // "QSQLITE" — Qt-драйвер для SQLite (встроен в Qt по умолчанию)
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbPath);

    // Открываем файл. Если файла нет — SQLite создаёт его автоматически.
    if (!m_db.open()) {
        qDebug() << "Ошибка открытия БД:" << m_db.lastError().text();
        return false;
    }
    qDebug() << "БД открыта:" << dbPath;

    // Создаём таблицы если их ещё нет (при первом запуске)
    if (!createTables()) return false;

    // Добавляем новые колонки в существующую БД при обновлении версии SmartClip
    migrateIfNeeded();

    // Автоочистка: удаляем записи старше N дней (если настроена)
    int days = AppSettings::get().autocleanDays();
    if (days > 0)
        autocleanByDays(days);

    return true;
}

// ─── Создание таблиц ──────────────────────────────────────────────────────────
// CREATE TABLE IF NOT EXISTS — создать таблицу только если её ещё нет.
// Это безопасно: при повторных запусках программы таблицы не пересоздаются.
bool Database::createTables()
{
    QSqlQuery query;

    // ── Таблица history (история буфера обмена) ────────────────────────────────
    // INTEGER PRIMARY KEY AUTOINCREMENT — id автоматически увеличивается (1, 2, 3...)
    // TEXT NOT NULL — строка, обязательное поле (не может быть пустым)
    // DATETIME DEFAULT CURRENT_TIMESTAMP — дата/время заполняется автоматически
    bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"  // Уникальный номер записи
        "type TEXT NOT NULL,"                     // "text", "image", "file", "video"
        "content TEXT,"                           // Текст (для text) или пусто (для image)
        "filepath TEXT,"                          // Путь к файлу (для image/video)
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"  // Дата/время копирования
        ")"
    );
    if (!ok) {
        qDebug() << "Ошибка создания таблицы history:" << query.lastError().text();
        return false;
    }

    // ── Таблица pins (закреплённые элементы) ──────────────────────────────────
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS pins ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "folder TEXT NOT NULL,"  // Папка-владелец ("Работа", "" = без папки)
        "name TEXT NOT NULL,"    // Название закрепа
        "type TEXT NOT NULL,"    // "text", "image", "file", "video"
        "content TEXT,"          // Текстовое содержимое
        "filepath TEXT"          // Путь к файлу
        ")"
    );
    if (!ok) {
        qDebug() << "Ошибка создания таблицы pins:" << query.lastError().text();
        return false;
    }

    // ── Таблица profiles (профили горячих клавиш) ─────────────────────────────
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS profiles ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"    // Название: "Email-подпись"
        "hotkey TEXT NOT NULL,"  // Комбинация: "Ctrl+Shift+1"
        "text TEXT NOT NULL"     // Текст для вставки
        ")"
    );
    if (!ok) {
        qDebug() << "Ошибка создания таблицы profiles:" << query.lastError().text();
        return false;
    }

    // ── Таблица folders (папки для группировки закрепов) ──────────────────────
    // UNIQUE — нельзя создать две папки с одинаковым именем
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS folders ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL UNIQUE"  // Имя папки, уникальное
        ")"
    );
    if (!ok) {
        qDebug() << "Ошибка создания таблицы folders:" << query.lastError().text();
        return false;
    }

    qDebug() << "Таблицы созданы успешно";
    return true;
}

// ─── Миграция базы данных ────────────────────────────────────────────────────
// При обновлении SmartClip могут добавляться новые поля в таблицы.
// Существующие БД не пересоздаются — добавляем только новые колонки.
// PRAGMA table_info — SQLite-команда которая возвращает описание структуры таблицы.
void Database::migrateIfNeeded()
{
    // ── Миграция 1: app_name в history ────────────────────────────────────────
    // Добавлено чтобы знать из какого приложения что скопировано.
    QSqlQuery check;
    check.exec("PRAGMA table_info(history)");
    bool hasAppName = false;
    while (check.next()) {
        if (check.value("name").toString() == "app_name") {
            hasAppName = true; break;
        }
    }
    if (!hasAppName) {
        QSqlQuery alter;
        alter.exec("ALTER TABLE history ADD COLUMN app_name TEXT DEFAULT ''");
        qDebug() << "Миграция: добавлена колонка app_name";
    }

    // ── Миграция 2: tags в pins ───────────────────────────────────────────────
    // Теги для поиска закрепов ("пират море работа").
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

    // ── Миграция 3: pinned в folders ─────────────────────────────────────────
    // Закреплённые папки всегда отображаются первыми.
    QSqlQuery check2;
    check2.exec("PRAGMA table_info(folders)");
    bool hasPinned = false;
    while (check2.next()) {
        if (check2.value("name").toString() == "pinned") { hasPinned = true; break; }
    }
    if (!hasPinned) {
        QSqlQuery alter;
        alter.exec("ALTER TABLE folders ADD COLUMN pinned INTEGER DEFAULT 0");
        qDebug() << "Миграция: добавлена колонка pinned в folders";
    }

    // ── Миграция 4: last_used в pins ─────────────────────────────────────────
    // Дата последнего использования закрепа — для вкладки «Все» (недавно использованные).
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

    // ── Миграция 5: sort_order в pins ────────────────────────────────────────
    // Пользовательский порядок карточек после drag-to-reorder.
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

// ─── Закрытие соединения ──────────────────────────────────────────────────────
void Database::close()
{
    m_db.close();
}

// ════════════════════════════════════════════════════════════════════════════════
//  ИСТОРИЯ (history)
// ════════════════════════════════════════════════════════════════════════════════

// ─── Добавить запись в историю ────────────────────────────────────────────────
// :type, :content и т.д. — именованные параметры (защита от SQL-инъекций).
// Всегда используем bindValue а не склейку строк!
bool Database::addHistory(const QString &type, const QString &content,
                          const QString &filepath, const QString &appName)
{
    QSqlQuery query;
    // prepare() — компилирует SQL-запрос (быстрее и безопаснее чем exec() со строкой)
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
    // После добавления проверяем лимит и удаляем старые если нужно
    trimHistory();
    return true;
}

// ─── Автоочистка по дате ─────────────────────────────────────────────────────
// Удаляет записи старше N дней. datetime('now', '-7 days') — SQLite-функция.
void Database::autocleanByDays(int days)
{
    if (days <= 0) return;

    // Сначала удаляем файлы картинок с диска (иначе останутся «мусорные» файлы)
    QSqlQuery fpQuery;
    fpQuery.prepare(
        "SELECT filepath FROM history "
        "WHERE filepath != '' AND created_at < datetime('now', :days)"
    );
    fpQuery.bindValue(":days", QString("-%1 days").arg(days));
    fpQuery.exec();
    while (fpQuery.next()) {
        QString fp = fpQuery.value(0).toString();
        if (!fp.isEmpty()) QFile::remove(fp);  // Удаляем файл с диска
    }

    // Удаляем старые строки из таблицы
    QSqlQuery q;
    q.prepare("DELETE FROM history WHERE created_at < datetime('now', :days)");
    q.bindValue(":days", QString("-%1 days").arg(days));
    if (q.exec())
        qDebug() << "Автоочистка: удалено записей старше" << days << "дней:" << q.numRowsAffected();
}

// ─── Обрезка истории до лимита ────────────────────────────────────────────────
// Вызывается после каждого addHistory. Удаляет самые старые записи если превышен лимит.
void Database::trimHistory()
{
    const int MAX_HISTORY = AppSettings::get().maxHistoryRecords();

    // COUNT(*) — количество строк в таблице
    QSqlQuery countQuery;
    countQuery.exec("SELECT COUNT(*) FROM history");
    countQuery.next();
    int count = countQuery.value(0).toInt();

    if (count > MAX_HISTORY) {
        int toDelete = count - MAX_HISTORY;
        // Удаляем toDelete самых старых записей (с наименьшим id)
        // ORDER BY id ASC — от старых к новым
        // LIMIT :n — берём только toDelete записей
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

// ─── Получить историю (с пагинацией) ─────────────────────────────────────────
// ORDER BY id DESC — новые записи первыми
// LIMIT/OFFSET — для «загрузить ещё» (например: первые 50, следующие 50...)
QList<HistoryItem> Database::getHistory(int limit, int offset)
{
    QList<HistoryItem> items;
    QSqlQuery query;
    query.prepare(
        "SELECT id, type, content, filepath, created_at, app_name "
        "FROM history ORDER BY id DESC LIMIT :limit OFFSET :offset"
    );
    query.bindValue(":limit",  limit);
    query.bindValue(":offset", offset);

    if (!query.exec()) {
        qDebug() << "Ошибка чтения history:" << query.lastError().text();
        return items;
    }

    // query.next() — переходим к следующей строке результата (возвращает false когда строк нет)
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

// ─── Получить историю одного типа ─────────────────────────────────────────────
// Например: только изображения ("image") или только текст ("text").
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

// ── Методы подсчёта и очистки истории ────────────────────────────────────────

// Количество записей. type="" = все типы, "text" = только текст, "image" = только картинки.
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
    return q.next() ? q.value(0).toInt() : 0;  // Тернарный оператор: если next() → вернуть значение, иначе 0
}

// Количество записей из определённого приложения.
int Database::countHistoryByApp(const QString &appName)
{
    QSqlQuery q;
    q.prepare("SELECT COUNT(*) FROM history WHERE app_name = :app");
    q.bindValue(":app", appName);
    q.exec();
    return q.next() ? q.value(0).toInt() : 0;
}

// Удалить всю историю (или только определённого типа).
// Возвращает количество удалённых записей.
int Database::deleteAllHistory(const QString &type)
{
    // Сначала удаляем файлы картинок с диска чтобы не засорять AppData
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

    // Удаляем строки из таблицы
    QSqlQuery query;
    if (type.isEmpty()) {
        query.exec("DELETE FROM history");         // Удалить всё
    } else {
        query.prepare("DELETE FROM history WHERE type = :type");
        query.bindValue(":type", type);
        query.exec();
    }
    qDebug() << "Очищено записей истории:" << query.numRowsAffected();
    return query.numRowsAffected();
}

// Удалить все записи скопированные из определённого приложения.
int Database::deleteHistoryByApp(const QString &appName)
{
    // Удаляем файлы картинок этого приложения
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

// Список уникальных приложений которые записаны в историю (для фильтра очистки).
// DISTINCT — убирает дубликаты: "chrome" появится один раз сколько бы раз ни копировали.
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

// Удалить одну запись истории по id.
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

// ════════════════════════════════════════════════════════════════════════════════
//  ЗАКРЕПЫ (pins)
// ════════════════════════════════════════════════════════════════════════════════

// ─── Добавить закреп ──────────────────────────────────────────────────────────
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

// ─── Получить закрепы (с логикой вкладки «Все») ──────────────────────────────
// folder="" → вкладка «Все»: показываем пины без папки + недавно использованные из папок.
// folder="Работа" → показываем только закрепы из этой папки.
QList<PinItem> Database::getPins(const QString &folder)
{
    QList<PinItem> items;
    QSqlQuery query;

    if (folder.isEmpty()) {
        // Вкладка «Все»:
        // • Пины без папки (folder = '') — всегда показываем
        // • Пины с last_used из любой папки — «недавно использованные»
        // CASE WHEN ... THEN ... ELSE ... END — SQL аналог тернарного оператора
        // Сортировка: недавно использованные вверху по убыванию даты, затем по id DESC
        query.prepare(
            "SELECT id, folder, name, type, content, filepath, tags, last_used FROM pins "
            "WHERE folder = '' OR (folder != '' AND last_used IS NOT NULL) "
            "ORDER BY CASE WHEN last_used IS NOT NULL THEN last_used ELSE '' END DESC, id DESC"
        );
    } else {
        // Конкретная папка:
        // sort_order — пользовательский порядок (drag-to-reorder).
        // sort_order=0 — новые без ручной сортировки → id DESC (новые вверху)
        // sort_order>0 — явно упорядоченные → ASC
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

// ─── Получить все закрепы без фильтра ────────────────────────────────────────
// Используется для поиска по всем папкам сразу.
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

// ─── Обновить «последнее использование» закрепа ──────────────────────────────
// Вызывается при вставке закрепа. datetime('now') = текущая дата/время SQLite.
bool Database::touchPin(int id)
{
    QSqlQuery q;
    q.prepare("UPDATE pins SET last_used = datetime('now') WHERE id = :id");
    q.bindValue(":id", id);
    if (!q.exec()) {
        qDebug() << "Ошибка touchPin:" << q.lastError().text();
        return false;
    }
    // Обрезаем список «недавно использованных» до лимита из настроек
    int limit = AppSettings::get().recentPinsLimit();
    trimRecentPins(limit);
    return true;
}

// ─── Обрезать список «недавно использованных» до лимита ──────────────────────
// Если использованных пинов больше limit — сбрасываем last_used у самых старых.
// Они больше не будут появляться во вкладке «Все».
void Database::trimRecentPins(int limit)
{
    // UPDATE пинов которые НЕ попали в топ-limit по дате последнего использования
    // NOT IN (SELECT ...) — исключаем топ-limit из обновления
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

// ─── Удалить закреп ───────────────────────────────────────────────────────────
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

// ════════════════════════════════════════════════════════════════════════════════
//  ПРОФИЛИ (profiles)
// ════════════════════════════════════════════════════════════════════════════════

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

// Получить все профили.
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

// Обновить профиль (имя, хоткей, текст) по id.
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

// Удалить профиль по id.
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

// ════════════════════════════════════════════════════════════════════════════════
//  ПАПКИ (folders)
// ════════════════════════════════════════════════════════════════════════════════

// ─── Добавить папку ───────────────────────────────────────────────────────────
// parentPath="" → корневая папка ("Работа")
// parentPath="Работа" → вложенная папка ("Работа/Клиент1")
// INSERT OR IGNORE — не ошибаться если папка с таким именем уже есть (UNIQUE)
bool Database::addFolder(const QString &name, const QString &parentPath)
{
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

// ─── Получить папки одного уровня ────────────────────────────────────────────
// parentPath="" → корень: папки без "/" в имени
// parentPath="Работа" → подпапки "Работа/*" но НЕ "Работа/*/*"
// LIKE '%/%' — строки содержащие "/" (путь к подпапке)
// NOT LIKE '%/%/%' — не более одного уровня вложенности
// ORDER BY pinned DESC — закреплённые папки первыми (pinned=1 > pinned=0)
QList<FolderItem> Database::getFolders(const QString &parentPath)
{
    QList<FolderItem> items;
    QSqlQuery query;

    if (parentPath.isEmpty()) {
        // Корневые папки: без "/" в имени
        query.exec(
            "SELECT id, name, pinned FROM folders "
            "WHERE name NOT LIKE '%/%' "
            "ORDER BY pinned DESC, name ASC"
        );
    } else {
        // Подпапки: "Работа/%" но НЕ "Работа/%/%"
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
        item.pinned = query.value("pinned").toInt() != 0;  // 1 → true, 0 → false
        items.append(item);
    }
    return items;
}

// ─── Получить все папки ───────────────────────────────────────────────────────
// Для диалога выбора папки при закреплении элемента.
QList<FolderItem> Database::getAllFolders()
{
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

// ─── Удалить папку (каскадно) ─────────────────────────────────────────────────
// При удалении папки её закрепы перемещаются в «без папки», подпапки тоже удаляются.
bool Database::deleteFolder(const QString &name)
{
    // Перемещаем закрепы: folder = '' (без папки)
    // folder = :name — закрепы из самой папки
    // folder LIKE :prefix — закрепы из подпапок ("Работа/Клиент1")
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

// ─── Переименовать папку (каскадно) ──────────────────────────────────────────
// Переименование обновляет имя папки И все пути подпапок И folder всех закрепов.
bool Database::renameFolder(const QString &oldName, const QString &newName)
{
    // Получаем все подпапки старого имени
    QSqlQuery getSubs;
    getSubs.prepare("SELECT name FROM folders WHERE name LIKE :prefix");
    getSubs.bindValue(":prefix", oldName + "/%");
    getSubs.exec();

    QStringList subfolders;
    while (getSubs.next())
        subfolders << getSubs.value(0).toString();

    // Обновляем каждую подпапку: заменяем старый префикс пути на новый.
    // sub.mid(oldName.length()) — отрезаем старый префикс, оставляем "/ПодПапка"
    for (const QString &sub : subfolders) {
        QString newSub = newName + sub.mid(oldName.length());

        // Переименовываем папку в таблице folders
        QSqlQuery updateSub;
        updateSub.prepare("UPDATE folders SET name = :new WHERE name = :old");
        updateSub.bindValue(":new", newSub);
        updateSub.bindValue(":old", sub);
        updateSub.exec();

        // Обновляем поле folder в таблице pins
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

    // Обновляем закрепы которые были в этой папке
    QSqlQuery q2;
    q2.prepare("UPDATE pins SET folder = :new WHERE folder = :old");
    q2.bindValue(":new", newName);
    q2.bindValue(":old", oldName);
    q2.exec();

    return true;
}

// ─── Закрепить/открепить папку ────────────────────────────────────────────────
// pinned=true → папка всегда первая в списке. pinned ? 1 : 0 — C++ тернарный оператор.
bool Database::setFolderPinned(const QString &name, bool pinned)
{
    QSqlQuery query;
    query.prepare("UPDATE folders SET pinned = :pinned WHERE name = :name");
    query.bindValue(":pinned", pinned ? 1 : 0);
    query.bindValue(":name",   name);
    return query.exec();
}

// ─── Редактирование закрепа ────────────────────────────────────────────────────

// Изменить текстовое содержимое закрепа.
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

// Переименовать закреп.
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

// Обновить теги закрепа (строка через пробел без #: "море работа пароль").
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

// ─── Обновить порядок закрепов после drag-to-reorder ─────────────────────────
// idOrderPairs — список пар (id закрепа, новый порядковый номер 1, 2, 3...).
// Вызывается когда пользователь перетащил карточки в новый порядок.
bool Database::updatePinsSortOrder(const QList<QPair<int,int>> &idOrderPairs)
{
    QSqlQuery query;
    for (const auto &p : idOrderPairs) {
        // p.first = id, p.second = sort_order
        query.prepare("UPDATE pins SET sort_order = :order WHERE id = :id");
        query.bindValue(":order", p.second);
        query.bindValue(":id",    p.first);
        if (!query.exec())
            qDebug() << "Ошибка обновления sort_order:" << query.lastError().text();
    }
    return true;
}

// ─── Переместить закреп в другую папку ───────────────────────────────────────
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
