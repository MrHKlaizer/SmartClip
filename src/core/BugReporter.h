// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  BugReporter.h — Отправка жалоб и пожеланий через Telegram-бот             ║
// ║                                                                              ║
// ║  Когда пользователь заполняет форму обратной связи в Settings → Бета,      ║
// ║  BugReporter отправляет сообщение (и прикреплённые файлы) в Telegram.      ║
// ║                                                                              ║
// ║  Токен бота и ID чата хранятся в Secrets.h — этот файл в .gitignore        ║
// ║  и НИКОГДА не попадает в публичный репозиторий GitHub.                      ║
// ║                                                                              ║
// ║  Антиспам: нельзя отправлять чаще чем раз в COOLDOWN_HOURS (6 часов).     ║
// ║                                                                              ║
// ║  Порядок отправки при наличии вложений:                                     ║
// ║  1. sendMessage    → текст (тип + описание проблемы)                        ║
// ║  2. sendMediaGroup → все фото и видео одним альбомом (до 10 файлов)        ║
// ║  3. sendAudio × N  → каждый аудиофайл отдельным запросом                  ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

// QObject — основа для сигналов/слотов Qt
#include <QObject>
// QNetworkAccessManager — менеджер HTTP-запросов (аналог axios/fetch).
// Через него отправляем POST-запросы к Telegram Bot API.
#include <QNetworkAccessManager>
// QList — динамический список (как std::vector), хранит пути к вложенным файлам
#include <QList>

// ─── Класс BugReporter ────────────────────────────────────────────────────────
class BugReporter : public QObject
{
    Q_OBJECT

public:
    // Тип обращения — определяет заголовок сообщения в Telegram
    enum class Type {
        Bug,      // Жалоба / баг-репорт — отображается как «🐛 ЖАЛОБА»
        Feature   // Пожелание / запрос функции — «💡 ПОЖЕЛАНИЕ»
    };

    // Тип вложенного файла — определяется по расширению (см. kindOfFile())
    enum class FileKind {
        Photo,  // .jpg, .png, .bmp → отправляется через sendPhoto/sendMediaGroup
        Video,  // .mp4, .avi, .mkv → отправляется через sendVideo/sendMediaGroup
        Audio   // .mp3, .wav, .ogg → отправляется через sendAudio
    };

    // Определить тип файла по его расширению.
    // Используется при формировании списка вложений.
    static FileKind kindOfFile(const QString &path);

    // Конструктор — создаёт QNetworkAccessManager для HTTP-запросов.
    explicit BugReporter(QObject *parent = nullptr);

    // Отправить обращение в Telegram.
    // type        — жалоба или пожелание
    // description — текст от пользователя
    // filePaths   — полные пути к вложениям (фото/видео/аудио). Может быть пустым.
    void send(Type type, const QString &description,
              const QList<QString> &filePaths = {});

    // Сколько часов прошло с последней отправки.
    // Возвращает -1 если отправок ещё не было (ни разу с момента установки).
    static int hoursSinceLast();

    // Можно ли отправить прямо сейчас (прошло > COOLDOWN_HOURS с последней отправки).
    static bool canSend();

    // ─── Лимиты вложений ──────────────────────────────────────────────────────
    static const int COOLDOWN_HOURS = 6;  // Антиспам: минимальный интервал между отправками
    static const int MAX_PHOTOS     = 10; // Максимум фото/видео в одном альбоме (ограничение Telegram)
    static const int MAX_VIDEOS     = 2;  // Максимум видео
    static const int MAX_AUDIOS     = 4;  // Максимум аудиофайлов

// ─── Сигналы ─────────────────────────────────────────────────────────────────
signals:
    void sent();                    // Всё отправлено успешно — UI показывает «Спасибо!»
    void failed(const QString &error); // Ошибка — UI показывает сообщение об ошибке

private:
    QNetworkAccessManager *m_nam;   // Менеджер сетевых запросов (создаётся в конструкторе)

    // Очереди файлов ожидающих отправки.
    // Сначала отправляется весь m_pendingPhotoVideo одним альбомом,
    // потом m_pendingAudio по одному.
    QList<QString> m_pendingPhotoVideo; // Фото + видео → sendMediaGroup
    QList<QString> m_pendingAudio;      // Аудио → sendAudio (несколько запросов)

    // Отправить альбом фото/видео через Telegram sendMediaGroup API.
    void sendMediaGroup();

    // Отправить следующий аудиофайл из m_pendingAudio через sendAudio API.
    // Вызывается цепочкой: когда один файл отправлен — вызывается снова для следующего.
    void sendNextAudio();
};
