// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  Backup.cpp — Реализация экспорта/импорта                                  ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "Backup.h"
#include "Database.h"
#include "AppSettings.h"

// Виды анимаций, у которых есть своя скорость (см. Anim::kinds() в ui/Anim.h)
static const char *const kAnimKinds[] = { "menu", "dialog", "open", "close", "scroll", "tabs", "viewer" };
#include "Version.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlDatabase>

namespace {

// Картинки больше этого размера в JSON не встраиваем — бэкап раздуется
constexpr qint64 MAX_EMBED_BYTES = 25 * 1024 * 1024;

// «Отпечаток» закрепа — по нему понимаем, что такой закреп уже есть.
// Для картинок сравниваем содержимое (MD5), а не путь: на другом ПК путь другой.
QString pinKey(const QString &folder, const QString &type, const QString &name,
               const QString &content, const QString &fileKey)
{
    const QChar sep(0x1F);  // Символ-разделитель, которого не бывает в тексте
    return folder + sep + type + sep + name + sep + content + sep + fileKey;
}

QString md5Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

// Ключ файла для существующего закрепа: MD5 картинки (если файл есть) или путь
QString fileKeyFor(const QString &type, const QString &filepath)
{
    if (type == "image") {
        QFile f(filepath);
        if (f.open(QIODevice::ReadOnly))
            return md5Hex(f.readAll());
    }
    return filepath;
}

QString profileKey(const QString &name, const QString &hotkey, const QString &text)
{
    const QChar sep(0x1F);
    return name + sep + hotkey + sep + text;
}

// Создать папку и все её родительские: "Работа/Клиенты/Иван" →
// "Работа", "Работа/Клиенты", "Работа/Клиенты/Иван".
// Без родителей вложенная папка не видна в интерфейсе.
void ensureFolderChain(Database *db, const QString &fullName)
{
    if (fullName.isEmpty()) return;
    const QStringList parts = fullName.split('/', Qt::SkipEmptyParts);
    QString path;
    for (const QString &p : parts) {
        path = path.isEmpty() ? p : path + "/" + p;
        db->addFolder(path);  // INSERT OR IGNORE — повторно не создастся
    }
}

} // namespace

namespace Backup {

QJsonObject exportAll(Database *db)
{
    QJsonObject root;
    root["version"]     = 2;
    root["app_version"] = QStringLiteral(APP_VERSION);
    root["exported_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // ── Папки ────────────────────────────────────────────────────────────────
    QJsonArray folderArr;
    for (const FolderItem &f : db->getAllFolders()) {
        QJsonObject fo;
        fo["name"]   = f.name;
        fo["pinned"] = f.pinned;
        folderArr.append(fo);
    }
    root["folders"] = folderArr;

    // ── Закрепы ──────────────────────────────────────────────────────────────
    // getAllPins() — каждый закреп ровно один раз. (Раньше брали getPins("")
    // + getPins(каждая папка), и «недавно использованные» попадали дважды.)
    QJsonArray pinArr;
    for (const PinItem &p : db->getAllPins()) {
        QJsonObject o;
        o["folder"]   = p.folder;
        o["name"]     = p.name;
        o["type"]     = p.type;
        o["content"]  = p.content;
        o["filepath"] = p.filepath;
        o["tags"]     = p.tags;
        // Картинку кладём прямо в JSON — иначе на другом ПК закреп будет пустым
        if (p.type == "image") {
            QFile f(p.filepath);
            if (f.size() <= MAX_EMBED_BYTES && f.open(QIODevice::ReadOnly))
                o["data"] = QString::fromLatin1(f.readAll().toBase64());
        }
        pinArr.append(o);
    }
    root["pins"] = pinArr;

    // ── Профили ──────────────────────────────────────────────────────────────
    QJsonArray profArr;
    for (const ProfileItem &p : db->getProfiles()) {
        QJsonObject o;
        o["name"]   = p.name;
        o["hotkey"] = p.hotkey;
        o["text"]   = p.text;
        profArr.append(o);
    }
    root["profiles"] = profArr;

    // ── Настройки ────────────────────────────────────────────────────────────
    auto &s = AppSettings::get();
    QJsonObject cfg;
    cfg["mainHotkey"]      = s.mainHotkey();
    cfg["maxTexts"]        = s.maxTexts();
    cfg["maxImages"]       = s.maxImages();
    cfg["autocleanDays"]   = s.autocleanDays();
    cfg["saveImages"]      = s.saveImages();
    cfg["deduplication"]   = s.deduplication();
    cfg["minTextLength"]   = s.minTextLength();
    cfg["excludedApps"]    = QJsonArray::fromStringList(s.excludedApps());
    cfg["imageFormat"]     = s.imageFormat();
    cfg["imageQuality"]    = s.imageQuality();
    cfg["pinsNoName"]      = s.pinsNoName();
    cfg["pinsNoFolder"]    = s.pinsNoFolder();
    cfg["recentPinsLimit"] = s.recentPinsLimit();
    // Оформление (2.0.1): виды и скорости анимаций, карточки, шкала, тёмный фон
    QJsonObject look;
    look["scrollStyle"] = s.scrollStyle();
    look["cardHover"]   = s.cardHover();
    look["dateRuler"]   = s.dateRuler();
    look["solidPanels"] = s.solidPanels();
    QJsonObject speeds;
    for (const char *k : kAnimKinds) speeds[k] = s.animSpeed(k);
    look["animSpeed"] = speeds;
    cfg["appearance"] = look;
    // Полоска файлов (2.2). Папки не переносим — на другом компьютере пути свои
    QJsonObject files;
    files["strip"] = s.filesStrip();
    files["days"]  = s.filesDays();
    files["max"]   = s.filesMax();
    files["view"]  = s.filesView();
    files["types"] = QJsonArray::fromStringList(s.filesTypes());
    cfg["files"] = files;
    root["settings"] = cfg;

    return root;
}

bool exportToFile(Database *db, const QString &path, QString *error)
{
    const QByteArray json = QJsonDocument(exportAll(db)).toJson(QJsonDocument::Indented);

    // Пишем во временный файл и потом переименовываем — если что-то упадёт
    // посередине, старый бэкап не превратится в обрезанный мусор.
    const QString tmp = path + ".tmp";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly) || f.write(json) != json.size()) {
        if (error) *error = f.errorString();
        f.close();
        QFile::remove(tmp);
        return false;
    }
    f.close();
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        if (error) *error = QStringLiteral("rename failed");
        QFile::remove(tmp);
        return false;
    }
    return true;
}

ImportResult importFromFile(Database *db, const QString &path)
{
    ImportResult r;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        r.error = file.errorString();
        return r;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        r.error = err.errorString();
        return r;
    }
    const QJsonObject root = doc.object();
    r.ok = true;

    // Всё в одной транзакции: быстро, и при сбое ничего не добавится наполовину
    QSqlDatabase sql = QSqlDatabase::database();
    sql.transaction();

    // ── Папки ────────────────────────────────────────────────────────────────
    for (const QJsonValue &v : root["folders"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o["name"].toString();
        if (name.isEmpty()) continue;
        ensureFolderChain(db, name);
        if (o["pinned"].toBool()) db->setFolderPinned(name, true);
    }

    // ── Закрепы ──────────────────────────────────────────────────────────────
    // Отпечатки уже существующих закрепов — чтобы не создавать дубли
    QSet<QString> existing;
    for (const PinItem &p : db->getAllPins())
        existing.insert(pinKey(p.folder, p.type, p.name, p.content,
                               fileKeyFor(p.type, p.filepath)));

    int fileCounter = 0;
    for (const QJsonValue &v : root["pins"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString folder  = o["folder"].toString();
        const QString name    = o["name"].toString();
        const QString type    = o["type"].toString();
        const QString content = o["content"].toString();
        QString filepath      = o["filepath"].toString();
        if (type.isEmpty()) continue;

        QByteArray imageData;
        if (o.contains("data"))
            imageData = QByteArray::fromBase64(o["data"].toString().toLatin1());

        const QString fileKey = !imageData.isEmpty() ? md5Hex(imageData)
                                                     : fileKeyFor(type, filepath);
        const QString key = pinKey(folder, type, name, content, fileKey);
        if (existing.contains(key)) {
            ++r.pinsSkipped;  // Такой закреп уже есть (или он дважды в файле)
            continue;
        }

        // Картинка из бэкапа → сохраняем как новый файл в нашей папке images
        if (!imageData.isEmpty()) {
            QString ext = QFileInfo(filepath).suffix();
            if (ext.isEmpty()) ext = "png";
            const QString newPath = QString("%1/pin_%2_%3.%4")
                .arg(db->imagesPath())
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(++fileCounter)
                .arg(ext);
            QFile out(newPath);
            if (out.open(QIODevice::WriteOnly) && out.write(imageData) == imageData.size())
                filepath = newPath;
        }

        // Старые бэкапы не содержали список папок — создаём папку по закрепу,
        // иначе закреп «потеряется» (его не видно ни в одной вкладке).
        ensureFolderChain(db, folder);

        const int id = db->addPin(folder, name, type, content, filepath);
        if (id < 0) continue;
        const QString tags = o["tags"].toString();
        if (!tags.isEmpty()) db->updatePinTags(id, tags);

        existing.insert(key);
        ++r.pinsAdded;
    }

    // ── Профили ──────────────────────────────────────────────────────────────
    QSet<QString> existingProfiles;
    for (const ProfileItem &p : db->getProfiles())
        existingProfiles.insert(profileKey(p.name, p.hotkey, p.text));

    for (const QJsonValue &v : root["profiles"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString key = profileKey(o["name"].toString(), o["hotkey"].toString(),
                                       o["text"].toString());
        if (existingProfiles.contains(key)) { ++r.profilesSkipped; continue; }
        db->addProfile(o["name"].toString(), o["hotkey"].toString(), o["text"].toString());
        existingProfiles.insert(key);
        ++r.profilesAdded;
    }

    sql.commit();

    // ── Настройки ────────────────────────────────────────────────────────────
    if (root.contains("settings")) {
        const QJsonObject cfg = root["settings"].toObject();
        auto &s = AppSettings::get();
        // Старые бэкапы (до 2.0): общий лимит → оба раздельных
        if (cfg.contains("maxRecords")) {
            s.setMaxTexts(cfg["maxRecords"].toInt());
            s.setMaxImages(cfg["maxRecords"].toInt());
        }
        if (cfg.contains("maxTexts"))        s.setMaxTexts(cfg["maxTexts"].toInt());
        if (cfg.contains("maxImages"))       s.setMaxImages(cfg["maxImages"].toInt());
        if (cfg.contains("autocleanDays"))   s.setAutocleanDays(cfg["autocleanDays"].toInt());
        if (cfg.contains("saveImages"))      s.setSaveImages(cfg["saveImages"].toBool());
        if (cfg.contains("deduplication"))   s.setDeduplication(cfg["deduplication"].toBool());
        if (cfg.contains("minTextLength"))   s.setMinTextLength(cfg["minTextLength"].toInt());
        if (cfg.contains("imageFormat"))     s.setImageFormat(cfg["imageFormat"].toString());
        if (cfg.contains("imageQuality"))    s.setImageQuality(cfg["imageQuality"].toInt());
        if (cfg.contains("pinsNoName"))      s.setPinsNoName(cfg["pinsNoName"].toBool());
        if (cfg.contains("pinsNoFolder"))    s.setPinsNoFolder(cfg["pinsNoFolder"].toBool());
        if (cfg.contains("recentPinsLimit")) s.setRecentPinsLimit(cfg["recentPinsLimit"].toInt());
        if (cfg.contains("appearance")) {
            const QJsonObject look = cfg["appearance"].toObject();
            if (look.contains("scrollStyle")) s.setScrollStyle(look["scrollStyle"].toString());
            if (look.contains("cardHover"))   s.setCardHover(look["cardHover"].toBool());
            if (look.contains("dateRuler"))   s.setDateRuler(look["dateRuler"].toString());
            if (look.contains("solidPanels")) s.setSolidPanels(look["solidPanels"].toBool());
            const QJsonObject speeds = look["animSpeed"].toObject();
            for (const char *k : kAnimKinds)
                if (speeds.contains(k)) s.setAnimSpeed(k, speeds[k].toInt());
        }
        if (cfg.contains("files")) {
            const QJsonObject files = cfg["files"].toObject();
            if (files.contains("strip")) s.setFilesStrip(files["strip"].toBool());
            if (files.contains("days"))  s.setFilesDays(files["days"].toInt());
            if (files.contains("max"))   s.setFilesMax(files["max"].toInt());
            if (files.contains("view"))  s.setFilesView(files["view"].toString());
            if (files.contains("types")) {
                QStringList types;
                for (const QJsonValue &t : files["types"].toArray()) types << t.toString();
                s.setFilesTypes(types);
            }
        }
        if (cfg.contains("excludedApps")) {
            QStringList excl;
            for (const QJsonValue &a : cfg["excludedApps"].toArray())
                excl << a.toString();
            s.setExcludedApps(excl);
        }
        // Главная горячая клавиша раньше экспортировалась, но не импортировалась
        const QString hk = cfg["mainHotkey"].toString().trimmed();
        if (!hk.isEmpty() && hk != s.mainHotkey()) {
            s.setMainHotkey(hk);
            r.mainHotkeyChanged = true;
        }
        r.settingsApplied = true;
    }

    return r;
}

void runAutoBackupIfDue(Database *db)
{
    auto &cfg = AppSettings::get();
    if (!cfg.autoBackup() || cfg.autoBackupPath().isEmpty()) return;

    const QString backupDir = cfg.autoBackupPath();
    QDir().mkpath(backupDir);

    // last_backup.txt — дата последнего бэкапа (ISO-формат)
    const QString stampFile = backupDir + "/last_backup.txt";
    QFile sf(stampFile);
    if (sf.open(QIODevice::ReadOnly)) {
        const QDateTime last = QDateTime::fromString(QString(sf.readAll()).trimmed(), Qt::ISODate);
        sf.close();
        if (last.isValid() && last.daysTo(QDateTime::currentDateTime()) < cfg.autoBackupDays())
            return;  // Ещё рано
    }

    const QString fname = backupDir + "/smartclip_backup_"
        + QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".json";
    QString error;
    if (!exportToFile(db, fname, &error)) {
        qDebug() << "Автобэкап не удался:" << error;
        return;
    }
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        sf.close();
    }
    qDebug() << "Автобэкап сохранён:" << fname;

    // Храним только 10 последних автобэкапов, чтобы папка не росла бесконечно
    QDir dir(backupDir);
    const QStringList old = dir.entryList({"smartclip_backup_*.json"}, QDir::Files, QDir::Name);
    for (int i = 0; i + 10 < old.size(); ++i)
        dir.remove(old[i]);
}

} // namespace Backup
