#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QSet>
#include <QStringList>

class Database;

// Следит за папкой видеозаписей (Xbox Game Bar: Videos/Captures).
// Когда появляется новый видеофайл — добавляет в историю SmartClip.
class VideoWatcher : public QObject
{
    Q_OBJECT

public:
    explicit VideoWatcher(Database *db, QObject *parent = nullptr);

    void start(const QString &folderPath);
    void stop();
    void setFolder(const QString &folderPath); // перезапустить с новой папкой

signals:
    void newVideoAdded(); // новое видео добавлено в историю

private slots:
    void onDirectoryChanged(const QString &path);

private:
    void scanFolder(); // сканирует папку и добавляет новые файлы

    QFileSystemWatcher m_watcher;
    Database          *m_db;
    QString            m_folderPath;
    QSet<QString>      m_knownFiles; // уже известные файлы (не дублируем)

    static const QStringList VIDEO_EXTENSIONS;
};
