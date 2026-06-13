#include "VideoWatcher.h"
#include "Database.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>

// Поддерживаемые форматы видео
const QStringList VideoWatcher::VIDEO_EXTENSIONS = {
    ".mp4", ".mkv", ".mov", ".avi", ".wmv", ".webm"
};

VideoWatcher::VideoWatcher(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &VideoWatcher::onDirectoryChanged);
}

void VideoWatcher::start(const QString &folderPath)
{
    m_folderPath = folderPath;

    QDir dir(folderPath);
    if (!dir.exists()) {
        qDebug() << "VideoWatcher: папка не существует:" << folderPath;
        return;
    }

    // Сначала запоминаем все уже существующие файлы — не добавляем их как новые
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files)) {
        QString ext = "." + fi.suffix().toLower();
        if (VIDEO_EXTENSIONS.contains(ext))
            m_knownFiles.insert(fi.absoluteFilePath());
    }

    m_watcher.addPath(folderPath);
    qDebug() << "VideoWatcher запущен:" << folderPath
             << "| уже известно файлов:" << m_knownFiles.size();
}

void VideoWatcher::stop()
{
    if (!m_folderPath.isEmpty()) {
        m_watcher.removePath(m_folderPath);
        m_folderPath.clear();
        m_knownFiles.clear();
        qDebug() << "VideoWatcher остановлен";
    }
}

void VideoWatcher::setFolder(const QString &folderPath)
{
    stop();
    start(folderPath);
}

void VideoWatcher::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path)

    // Небольшая задержка — даём записи дозаписаться перед тем как добавлять в историю.
    // Xbox Game Bar иногда создаёт файл, потом ещё раз меняет его размер.
    QTimer::singleShot(2000, this, &VideoWatcher::scanFolder);
}

void VideoWatcher::scanFolder()
{
    QDir dir(m_folderPath);
    if (!dir.exists()) return;

    bool added = false;

    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files, QDir::Time)) {
        QString ext      = "." + fi.suffix().toLower();
        QString filepath = fi.absoluteFilePath();

        if (!VIDEO_EXTENSIONS.contains(ext))    continue;
        if (m_knownFiles.contains(filepath))     continue;
        if (fi.size() < 10000)                   continue; // < 10 КБ — файл ещё пишется

        m_knownFiles.insert(filepath);
        m_db->addHistory("video", fi.fileName(), filepath, "ScreenCapture");
        qDebug() << "VideoWatcher: новое видео добавлено:" << fi.fileName();
        added = true;
    }

    if (added)
        emit newVideoAdded();
}
