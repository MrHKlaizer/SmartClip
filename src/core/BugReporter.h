#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QList>

// Отправляет жалобы и пожелания в Telegram-бот.
// Токен и chat_id берёт из Secrets.h (файл в .gitignore).
// Антиспам: не чаще одного раза в COOLDOWN_DAYS дней.
//
// Последовательность отправки при наличии вложений:
//   1. sendMessage — текст (ЖАЛОБА / ПОЖЕЛАНИЕ + описание)
//   2. sendMediaGroup — все фото + видео (до 10 элементов)
//   3. sendAudio × N — каждый аудиофайл отдельным запросом
class BugReporter : public QObject
{
    Q_OBJECT

public:
    enum class Type { Bug, Feature };

    // Тип вложенного файла — определяется по расширению
    enum class FileKind { Photo, Video, Audio };
    static FileKind kindOfFile(const QString &path);

    explicit BugReporter(QObject *parent = nullptr);

    // filePaths — полные пути к прикреплённым файлам (фото/видео/аудио)
    void send(Type type, const QString &description,
              const QList<QString> &filePaths = {});

    static int  daysSinceLast(); // -1 если ни разу не отправляли
    static bool canSend();

    static const int COOLDOWN_DAYS = 3;
    static const int MAX_PHOTOS    = 10;
    static const int MAX_VIDEOS    = 2;
    static const int MAX_AUDIOS    = 4;

signals:
    void sent();
    void failed(const QString &error);

private:
    QNetworkAccessManager *m_nam;
    QList<QString>         m_pendingPhotoVideo; // фото + видео → sendMediaGroup
    QList<QString>         m_pendingAudio;      // аудио → sendAudio по одному

    void sendMediaGroup();
    void sendNextAudio();
};
