// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  VideoWatcher.cpp — Реализация наблюдателя за папкой видеозаписей          ║
// ║                                                                              ║
// ║  Этот файл реализует логику описанную в VideoWatcher.h:                    ║
// ║  следим за папкой, находим новые видеофайлы, добавляем в базу данных.      ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "VideoWatcher.h"  // Описание класса
#include "Database.h"      // addHistory() — для добавления видео в историю

// Qt-утилиты для работы с файловой системой:
#include <QDir>       // Работа с папками: проверка существования, список файлов
#include <QFileInfo>  // Информация о файле: имя, расширение, размер, путь
#include <QTimer>     // Однократный таймер (задержка 2с перед сканированием)
#include <QDebug>     // qDebug() для отладочного вывода в консоль разработчика

// ─── Список поддерживаемых расширений ────────────────────────────────────────
// static — одна копия для всего класса (не для каждого объекта отдельно).
// Файлы с этими расширениями считаются видео и добавляются в историю.
const QStringList VideoWatcher::VIDEO_EXTENSIONS = {
    ".mp4", ".mkv", ".mov", ".avi", ".wmv", ".webm"
};

// ─── Конструктор ─────────────────────────────────────────────────────────────
// Инициализирует поля и подключает сигнал файловой системы к нашему слоту.
// m_watcher — объект слежения за папкой (создаётся автоматически, не указатель).
VideoWatcher::VideoWatcher(Database *db, QObject *parent)
    : QObject(parent)  // Передаём родителя в базовый класс Qt
    , m_db(db)         // Запоминаем базу данных
{
    // Когда содержимое наблюдаемой папки изменится —
    // m_watcher испустит сигнал directoryChanged, мы его поймаем.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &VideoWatcher::onDirectoryChanged);
}

// ─── Начать слежение ─────────────────────────────────────────────────────────
// Запоминает уже существующие файлы (чтобы не добавлять их как «новые»)
// и начинает следить за папкой через QFileSystemWatcher.
void VideoWatcher::start(const QString &folderPath)
{
    m_folderPath = folderPath;

    QDir dir(folderPath);
    if (!dir.exists()) {
        // Папка не существует — возможно Xbox Game Bar ещё не запускался
        qDebug() << "VideoWatcher: папка не существует:" << folderPath;
        return;
    }

    // Проходим по всем файлам в папке.
    // QDir::Files — только файлы (не папки).
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files)) {
        QString ext = "." + fi.suffix().toLower();  // ".mp4", ".mkv" и т.д.
        if (VIDEO_EXTENSIONS.contains(ext))
            // Добавляем в «известные» — эти файлы мы НЕ будем добавлять в историю
            m_knownFiles.insert(fi.absoluteFilePath());
    }

    // Подписываемся на уведомления от файловой системы — теперь при изменении
    // папки QFileSystemWatcher испустит сигнал directoryChanged.
    m_watcher.addPath(folderPath);
    qDebug() << "VideoWatcher запущен:" << folderPath
             << "| уже известно файлов:" << m_knownFiles.size();
}

// ─── Остановить слежение ─────────────────────────────────────────────────────
void VideoWatcher::stop()
{
    if (!m_folderPath.isEmpty()) {
        m_watcher.removePath(m_folderPath);  // Отписываемся от уведомлений
        m_folderPath.clear();                // Сбрасываем путь
        m_knownFiles.clear();               // Очищаем список известных файлов
        qDebug() << "VideoWatcher остановлен";
    }
}

// ─── Изменить наблюдаемую папку ───────────────────────────────────────────────
// Просто останавливаем и запускаем заново с новым путём.
// Вызывается когда пользователь меняет папку в настройках.
void VideoWatcher::setFolder(const QString &folderPath)
{
    stop();
    start(folderPath);
}

// ─── Слот: папка изменилась ───────────────────────────────────────────────────
// Qt вызывает этот метод когда файловая система сообщает об изменении в папке.
// path — путь к папке (не используем — у нас всегда одна папка m_folderPath).
void VideoWatcher::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path)  // Подавляем предупреждение компилятора о неиспользуемом параметре

    // Задержка 2 секунды перед сканированием.
    // Xbox Game Bar сначала создаёт файл размером ~0 байт, потом дописывает его.
    // Без задержки мы можем поймать незаконченную запись.
    // QTimer::singleShot — запустить один раз через 2000мс, потом автоматически удалить.
    QTimer::singleShot(2000, this, &VideoWatcher::scanFolder);
}

// ─── Сканирование папки ───────────────────────────────────────────────────────
// Проходит по всем файлам в папке и добавляет новые видео в базу данных.
void VideoWatcher::scanFolder()
{
    QDir dir(m_folderPath);
    if (!dir.exists()) return;

    bool added = false;  // Флаг: было ли добавлено хоть одно новое видео

    // QDir::Files     — только файлы
    // QDir::Time      — сортировка от новых к старым (новые первыми)
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files, QDir::Time)) {
        QString ext      = "." + fi.suffix().toLower();
        QString filepath = fi.absoluteFilePath();

        // Фильтрация:
        if (!VIDEO_EXTENSIONS.contains(ext))  continue; // Не видео — пропускаем
        if (m_knownFiles.contains(filepath))  continue; // Уже знаем — пропускаем
        if (fi.size() < 10000)                continue; // < 10 КБ — файл ещё пишется

        // Файл новый и достаточно большой — добавляем в историю SmartClip
        m_knownFiles.insert(filepath);
        // addHistory(тип, название, путь, источник)
        // "ScreenCapture" — категория в базе данных
        m_db->addHistory("video", fi.fileName(), filepath, "ScreenCapture");
        qDebug() << "VideoWatcher: новое видео добавлено:" << fi.fileName();
        added = true;
    }

    // Если хоть одно видео добавлено — сигналим MainWindow обновить список карточек
    if (added)
        emit newVideoAdded();
}
