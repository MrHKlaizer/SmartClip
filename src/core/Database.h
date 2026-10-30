// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  Database.h — Работа с базой данных SmartClip                              ║
// ║                                                                              ║
// ║  SmartClip хранит всю историю и закрепы в SQLite — это маленькая          ║
// ║  встраиваемая база данных в виде одного файла на диске.                    ║
// ║                                                                              ║
// ║  Что хранится в базе:                                                        ║
// ║  ┌─────────────────────────────────────────────────────────┐                ║
// ║  │  history  — всё что когда-либо было скопировано        │                ║
// ║  │  pins     — закреплённые элементы (текст, фото, видео) │                ║
// ║  │  folders  — папки для группировки закрепов             │                ║
// ║  │  profiles — профили горячих клавиш                     │                ║
// ║  └─────────────────────────────────────────────────────────┘                ║
// ║                                                                              ║
// ║  Файл базы данных: AppData/Roaming/SmartClipApp/SmartClip.db               ║
// ║                                                                              ║
// ║  Паттерн использования: Database db; db.init(); db.addHistory(...);        ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

// QSqlDatabase — подключение к базе данных (SQLite, PostgreSQL и др.)
#include <QSqlDatabase>
// QString    — строковый тип Qt
// QList      — динамический список
// QStringList — список строк (наследник QList<QString>)
#include <QString>
#include <QList>
#include <QSet>
#include <QPair>
#include <QStringList>

// ─── Структуры данных ─────────────────────────────────────────────────────────
// В C++ «struct» — это простой контейнер полей, аналог объекта/словаря.
// Используем их чтобы передавать группу полей как одно целое.

// Одна запись в истории буфера обмена
struct HistoryItem {
    int     id;         // Уникальный номер записи (первичный ключ в БД)
    QString type;       // Тип содержимого: "text", "image", "file", "video"
    QString content;    // Текст или имя файла (для картинок — имя файла)
    QString filepath;   // Полный путь к файлу (для картинок и видео)
    QString createdAt;  // Дата и время сохранения в формате ISO: "2025-01-15 14:30:00"
    QString appName;    // Имя .exe приложения из которого копировали ("chrome.exe")
};

// Один закреплённый элемент (пин)
struct PinItem {
    int     id;         // Уникальный номер в БД
    QString folder;     // Папка-владелец ("Работа", "Пароли" и т.д.)
    QString name;       // Название закрепа — отображается на карточке
    QString type;       // Тип: "text", "image", "file", "video"
    QString content;    // Текстовое содержимое (для text-пинов)
    QString filepath;   // Путь к файлу (для image/video/file-пинов)
    QString tags;       // Теги через пробел без # — для поиска: "пират море"
    QString lastUsed;   // Дата последнего использования (ISO) или "" если не использовался
};

// Папка для группировки закрепов
struct FolderItem {
    int     id;              // Уникальный номер папки
    QString name;            // Название: "Работа", "Личное" и т.д.
    bool    pinned = false;  // true = папка закреплена (всегда первая в списке)
};

// Профиль горячих клавиш (быстрая вставка текста)
struct ProfileItem {
    int     id;      // Уникальный номер профиля
    QString name;    // Название: "Email-подпись", "Адрес"
    QString hotkey;  // Горячая клавиша: "Ctrl+Shift+1"
    QString text;    // Текст который вставляется при нажатии хоткея
};

// ─── Класс Database ───────────────────────────────────────────────────────────
// Обёртка над SQLite базой данных. Предоставляет простые методы для работы
// с данными — скрывает SQL-запросы от остальной части программы.
class Database
{
public:
    // Открыть (или создать) базу данных, создать таблицы если их нет.
    // Вызывается один раз при старте программы.
    // Возвращает false если база не открылась (нет прав, диск полный и т.д.)
    bool init();

    // Закрыть соединение с базой данных при завершении программы.
    void close();

    // Путь к папке где хранятся сохранённые картинки из буфера.
    QString imagesPath() const { return m_imagesPath; }

    // ─── История (history) ────────────────────────────────────────────────────
    // CRUD = Create, Read, Update, Delete — стандартные операции с данными.

    // Добавить новую запись в историю.
    // type     — "text", "image", "file", "video"
    // content  — текст или имя файла
    // filepath — путь к файлу (для изображений, видео)
    // appName  — имя приложения-источника
    // Возвращает id новой записи или -1 при ошибке.
    int  addHistory(const QString &type, const QString &content,
                    const QString &filepath = "", const QString &appName = "");

    // Удалить из истории все текстовые записи с таким же содержимым.
    // Нужно для дедупликации: повторно скопированный текст «поднимается» наверх.
    void removeTextDuplicates(const QString &text);

    // Удалить записи старше N дней (0 = не удалять).
    // Вызывается при старте программы согласно настройкам.
    void autocleanByDays(int days);

    // Получить список записей истории с пагинацией.
    // limit = сколько записей вернуть, offset = сколько пропустить (для «загрузить ещё»)
    QList<HistoryItem> getHistory(int limit, int offset);

    // Получить историю только определённого типа (например только "image").
    QList<HistoryItem> getHistoryByType(const QString &type, int limit, int offset);

    // Страница истории нескольких видов сразу (для подгрузки при прокрутке):
    // types — например {"image","video"}, asc — старые сначала.
    // Записи этих видов новее afterId (новые — первыми): быстрое обновление окна
    QList<HistoryItem> getHistoryNewer(const QStringList &types, int afterId);
    // Какие из этих id ещё есть в истории (остальные удалены лимитом/повтором)
    QSet<int> existingHistoryIds(const QList<int> &ids);
    // Для поиска: id и начало текста (до maxChars символов) — без огромных логов целиком
    QList<QPair<int, QString>> getTextHeads(int limit, bool asc, int maxChars);
    // Записи по списку id — в том же порядке
    QList<HistoryItem> getHistoryByIds(const QList<int> &ids);
    QList<HistoryItem> getHistoryPage(const QStringList &types, int limit, int offset, bool asc);

    // Удалить одну запись истории по её id.
    bool deleteHistory(int id);

    // ─── Очистка истории ──────────────────────────────────────────────────────
    // Подсчитать количество записей. type="" = все типы.
    int  countHistory(const QString &type = "");

    // Подсчитать записи скопированные из определённого приложения.
    int  countHistoryByApp(const QString &appName);

    // Удалить все записи (или только определённого типа). Возвращает сколько удалено.
    int  deleteAllHistory(const QString &type = "");

    // Удалить все записи из определённого приложения.
    int  deleteHistoryByApp(const QString &appName);

    // Получить список уникальных имён приложений (для фильтра очистки).
    QStringList getAppNames();

    // ─── Закрепы (pins) ───────────────────────────────────────────────────────
    // Добавить новый закреп. Возвращает id нового закрепа или -1 при ошибке.
    int  addPin(const QString &folder, const QString &name, const QString &type,
                const QString &content = "", const QString &filepath = "");

    // Получить закрепы из одной папки. folder="" = все закрепы.
    QList<PinItem> getPins(const QString &folder = "");

    // Получить все закрепы без фильтра (для поиска по всем папкам).
    QList<PinItem> getAllPins();

    // Удалить закреп по id.
    bool deletePin(int id);

    // Переместить закреп в другую папку.
    bool movePinToFolder(int pinId, const QString &folder);

    // Обновить время последнего использования закрепа (когда пользователь его вставил).
    bool touchPin(int id);

    // Сбросить «последнее использование» у самых старых закрепов если их больше limit.
    void trimRecentPins(int limit);

    // ─── Папки (folders) ──────────────────────────────────────────────────────
    // Добавить новую папку. parentPath — для вложенных папок (обычно пусто).
    bool addFolder(const QString &name, const QString &parentPath = "");

    // Получить папки одного уровня.
    QList<FolderItem> getFolders(const QString &parentPath = "");

    // Получить все папки без фильтра (для диалога выбора папки).
    QList<FolderItem> getAllFolders();

    // Удалить папку по имени.
    bool deleteFolder(const QString &name);

    // Переименовать папку.
    bool renameFolder(const QString &oldName, const QString &newName);

    // Закрепить/открепить папку (закреплённая всегда показывается первой).
    bool setFolderPinned(const QString &name, bool pinned);

    // ─── Редактирование закрепа ───────────────────────────────────────────────
    bool updatePinContent(int id, const QString &content); // Изменить текст
    bool updateHistoryContent(int id, const QString &content); // Изменить текст записи истории
    bool updatePinName(int id, const QString &name);       // Переименовать
    void renameFilePath(const QString &from, const QString &to);  // Файл переименовали на диске
    bool updatePinTags(int id, const QString &tags);       // Изменить теги (через пробел, без #)

    // Обновить порядок закрепов после drag-and-drop.
    // idOrderPairs — список пар (id закрепа, новый порядковый номер).
    bool updatePinsSortOrder(const QList<QPair<int,int>> &idOrderPairs);

    // ─── Профили горячих клавиш (profiles) ───────────────────────────────────
    bool addProfile(const QString &name, const QString &hotkey, const QString &text);
    bool updateProfile(int id, const QString &name, const QString &hotkey, const QString &text);
    QList<ProfileItem> getProfiles();
    bool deleteProfile(int id);

private:
    QSqlDatabase m_db;         // Объект подключения к SQLite-базе данных
    QString      m_imagesPath; // Путь к папке с сохранёнными картинками

    // Удалить файл с диска, но только если он «наш» (лежит в папке images)
    // и на него больше не ссылается ни история, ни закреп.
    // Защищает видео пользователя и картинки в закрепах от случайного удаления.
    void removeFileIfUnused(const QString &path);

    // Экранировать % и _ для LIKE (иначе папка "50%" совпадёт с чем угодно).
    static QString escapeLike(const QString &s);

    // Вспомогательные методы (внутренние):
    bool createTables();     // Создать таблицы при первом запуске
    void trimHistory();      // Обрезать историю до лимитов (тексты и картинки — раздельно)
    void trimType(const QString &where, int limit);  // Обрезать один вид записей
    void migrateHistoryLimits();  // Общий лимит (до 2.0) → раздельные, без потери данных
    void migrateIfNeeded();  // Добавить новые колонки в уже существующую БД (обновление схемы)
};
