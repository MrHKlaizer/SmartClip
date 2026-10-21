// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  VideoWatcher.cpp — Автозахват записей экрана и аудио                      ║
// ║                                                                              ║
// ║  Следит за выбранными папками: новые видео (Xbox Game Bar, «Ножницы») и    ║
// ║  аудио (Звукозапись и т.п.) появляются в истории сами. Видео — в колонке   ║
// ║  картинок, аудио — в колонке текстов. Клик по карточке — вставить файл.     ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "VideoWatcher.h"  // Описание класса
#include "Database.h"      // addHistory() — для добавления записей в историю
#include "AppSettings.h"   // Выбранные папки

#include <QDir>            // Работа с папками: проверка существования, список файлов
#include <QFileInfo>       // Информация о файле: имя, расширение, размер, путь
#include <QTimer>          // Однократный таймер (задержка 2с перед сканированием)
#include <QStandardPaths>  // Папки «Видео» и «Документы» (в том числе перенесённые в OneDrive)
#include <QDebug>          // qDebug() для отладочного вывода в консоль разработчика
#include <windows.h>

// SHGetKnownFolderPath (shell32) — папка «Клипы» Xbox Game Bar. Объявлена здесь
// сама: <shlobj.h> объявляет свой FolderItem, и он конфликтует с нашим (Database.h).
extern "C" __declspec(dllimport) HRESULT __stdcall
SHGetKnownFolderPath(const GUID &rfid, DWORD dwFlags, HANDLE hToken, PWSTR *ppszPath);

const QStringList VideoWatcher::VIDEO_EXTENSIONS = {
    ".mp4", ".mkv", ".mov", ".avi", ".wmv", ".webm", ".m4v"
};
const QStringList VideoWatcher::AUDIO_EXTENSIONS = {
    ".mp3", ".wav", ".m4a", ".aac", ".ogg", ".flac", ".wma", ".opus"
};

VideoWatcher::VideoWatcher(Database *db, QObject *parent)
    : QObject(parent)  // Передаём родителя в базовый класс Qt
    , m_db(db)         // Запоминаем базу данных
{
    // Когда содержимое наблюдаемой папки изменится —
    // m_watcher испустит сигнал directoryChanged, мы его поймаем.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &VideoWatcher::onDirectoryChanged);
}

// ─── Стандартные папки ────────────────────────────────────────────────────────
// Записи Xbox Game Bar («Клипы» — Windows знает, куда пользователь её перенёс),
// «Записи экрана» от «Ножниц» и «Звукозаписи» от приложения «Звукозапись».
QStringList VideoWatcher::defaultFolders()
{
    QStringList result;
    // FOLDERID_Captures — папка клипов Xbox Game Bar
    static const GUID kCaptures = { 0xEDC0FE71, 0x98D8, 0x4F4A, { 0xB9, 0x20, 0xC8, 0xDC, 0x13, 0x3C, 0xB1, 0x65 } };
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(kCaptures, 0, nullptr, &path)) && path)
        result << QDir::fromNativeSeparators(QString::fromWCharArray(path));
    if (path) CoTaskMemFree(path);

    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString docs   = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (result.isEmpty()) result << videos + "/Captures";
    for (const QString &p : { videos + "/Screen Recordings", docs + "/Sound Recordings" })
        if (QDir(p).exists()) result << p;
    result.removeDuplicates();
    return result;
}

// Папки, которые выбрал пользователь. Ещё не выбирал — стандартные
// (а если в старой версии он указывал свою папку — она).
QStringList VideoWatcher::configuredFolders()
{
    auto &s = AppSettings::get();
    if (s.hasMediaFolders()) return s.mediaFolders();
    const QString old = s.legacyVideoPath();
    const QString oldDefault = QDir::homePath() + "/Videos/Captures";
    if (!old.isEmpty() && QDir::cleanPath(old) != QDir::cleanPath(oldDefault))
        return { old };
    return defaultFolders();
}

bool VideoWatcher::isMediaFile(const QString &path, bool *isAudio)
{
    const QString ext = "." + QFileInfo(path).suffix().toLower();
    const bool audio = AUDIO_EXTENSIONS.contains(ext);
    if (isAudio) *isAudio = audio;
    return audio || VIDEO_EXTENSIONS.contains(ext);
}

void VideoWatcher::start(const QStringList &folders)
{
    for (const QString &folder : folders) {
        QDir dir(folder);
        if (!dir.exists()) {
            // Папки ещё нет — например, Xbox Game Bar ещё ни разу не записывал
            qDebug() << "VideoWatcher: папка не существует:" << folder;
            continue;
        }
        // Уже лежащие файлы запоминаем — в историю их НЕ добавляем
        for (const QFileInfo &fi : dir.entryInfoList(QDir::Files))
            if (isMediaFile(fi.fileName()))
                m_knownFiles.insert(fi.absoluteFilePath());
        m_watcher.addPath(folder);
        m_folders << folder;
    }
    qDebug() << "VideoWatcher запущен:" << m_folders << "| уже известно файлов:" << m_knownFiles.size();
}

void VideoWatcher::stop()
{
    if (!m_folders.isEmpty()) m_watcher.removePaths(m_folders);  // Отписываемся от уведомлений
    m_folders.clear();
    m_knownFiles.clear();
}

void VideoWatcher::setFolders(const QStringList &folders)
{
    stop();
    start(folders);
}

void VideoWatcher::onDirectoryChanged(const QString &path)
{
    // Задержка 2 секунды перед сканированием: Xbox Game Bar сначала создаёт
    // файл размером ~0 байт, потом дописывает его — без задержки поймали бы
    // незаконченную запись.
    QTimer::singleShot(2000, this, [this, path]() { scanFolder(path); });
}

void VideoWatcher::scanFolder(const QString &folder)
{
    QDir dir(folder);
    if (!dir.exists()) return;

    bool added = false;  // Было ли добавлено хоть что-то новое
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files, QDir::Time)) {
        const QString filepath = fi.absoluteFilePath();
        bool audio = false;
        if (!isMediaFile(filepath, &audio))  continue; // Не видео и не аудио
        if (m_knownFiles.contains(filepath)) continue; // Уже знаем
        if (fi.size() < 10000)               continue; // < 10 КБ — файл ещё пишется

        m_knownFiles.insert(filepath);
        // Видео — в колонку картинок, аудио — в колонку текстов
        m_db->addHistory(audio ? "audio" : "video", fi.fileName(), filepath,
                         audio ? "AudioCapture" : "ScreenCapture");
        qDebug() << "VideoWatcher: новый файл добавлен:" << fi.fileName();
        added = true;
    }
    if (added)
        emit newVideoAdded();
}
