// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  BugReporter.cpp — Реализация отправки обратной связи в Telegram           ║
// ║                                                                              ║
// ║  Этот файл реализует отправку багрепортов через Telegram Bot API.          ║
// ║  Telegram API работает через обычные HTTPS-запросы (как веб-сайт).         ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "BugReporter.h"   // Описание класса
#include "Secrets.h"       // TELEGRAM_BOT_TOKEN и TELEGRAM_CHAT_ID (файл в .gitignore!)
#include "Version.h"       // APP_VERSION — добавляем в сообщение
#include "AppSettings.h"   // Для lastReportTime (антиспам)

// Qt-модули для сети и данных:
#include <QNetworkRequest>   // Описание HTTP-запроса
#include <QNetworkReply>     // Ответ сервера
#include <QHttpMultiPart>    // Отправка файлов (multipart/form-data — как HTML-форма с файлом)
#include <QJsonObject>       // JSON-объект {ключ: значение}
#include <QJsonArray>        // JSON-массив [...]
#include <QJsonDocument>     // Сериализация JSON → байты
#include <QUrl>              // URL-адрес
#include <QDateTime>         // Дата/время (для антиспама)
#include <QSysInfo>          // Информация об ОС (добавляем в сообщение)
#include <QFile>             // Чтение файлов-вложений
#include <QFileInfo>         // Информация о файле (расширение, имя)

// ─── Конструктор ─────────────────────────────────────────────────────────────
BugReporter::BugReporter(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))  // Создаём HTTP-менеджер (удалится вместе с нами)
{}

// ─── Определение типа файла по расширению ─────────────────────────────────────
// Возвращает FileKind::Audio, Video или Photo в зависимости от расширения.
BugReporter::FileKind BugReporter::kindOfFile(const QString &path)
{
    // suffix() — расширение файла без точки: "mp3", "jpg" и т.д.
    // toLower() — приводим к нижнему регистру для сравнения
    const QString ext = QFileInfo(path).suffix().toLower();

    // static — создаётся один раз для всей программы (экономия памяти)
    static const QStringList audioExts{"mp3","ogg","wav","m4a","flac","aac"};
    static const QStringList videoExts{"mp4","mov","avi","mkv","webm","m4v"};

    if (audioExts.contains(ext)) return FileKind::Audio;
    if (videoExts.contains(ext)) return FileKind::Video;
    return FileKind::Photo;  // Всё остальное считаем фото (jpg, png, bmp и т.д.)
}

// ─── Сколько часов прошло с последней отправки ───────────────────────────────
// Возвращает -1 если отправок ещё не было.
int BugReporter::hoursSinceLast()
{
    const QString last = AppSettings::get().lastReportTime();
    if (last.isEmpty()) return -1;  // Ни разу не отправляли

    // Конвертируем строку ISO (2025-01-15T14:30:00) → QDateTime
    // secsTo() — количество секунд до текущего момента
    // / 3600 — переводим секунды в часы
    return static_cast<int>(
        QDateTime::fromString(last, Qt::ISODate)
            .secsTo(QDateTime::currentDateTime()) / 3600);
}

// ─── Проверка: можно ли отправить прямо сейчас ───────────────────────────────
bool BugReporter::canSend()
{
    const int h = hoursSinceLast();
    // h < 0  = ни разу не отправляли → можно
    // h >= COOLDOWN_HOURS = прошло достаточно времени → можно
    return h < 0 || h >= COOLDOWN_HOURS;
}

// ─── Отправка обращения ───────────────────────────────────────────────────────
// Шаг 1 из 3: отправляем текстовое сообщение через sendMessage.
// После успеха → шаг 2 (медиафайлы) или 3 (аудио).
void BugReporter::send(Type type, const QString &description,
                       const QList<QString> &filePaths)
{
    // Разбираем вложения по типам
    m_pendingPhotoVideo.clear();
    m_pendingAudio.clear();
    for (const QString &p : filePaths) {
        if (kindOfFile(p) == FileKind::Audio)
            m_pendingAudio.append(p);        // Аудио — отдельными запросами
        else
            m_pendingPhotoVideo.append(p);   // Фото/видео — одним альбомом
    }

    // Заголовок сообщения зависит от типа обращения
    const QString header = (type == Type::Bug)
        ? QString::fromUtf8("🔴 ЖАЛОБА")
        : QString::fromUtf8("💡 ПОЖЕЛАНИЕ");

    // Формируем полный текст сообщения:
    // «🔴 ЖАЛОБА\n\n[текст пользователя]\n\n—\nSmartClip v1.0.2\nWindows 11»
    // \342\200\224 — символ «—» (em dash) в UTF-8 октальной записи
    const QString text = QString("%1\n\n%2\n\n\342\200\224\nSmartClip v%3\n%4")
        .arg(header, description.trimmed(),
             QLatin1String(APP_VERSION),
             QSysInfo::prettyProductName());  // "Windows 11 Version 22H2"

    // Формируем JSON-тело для Telegram sendMessage API
    QJsonObject body;
    body["chat_id"] = QLatin1String(TELEGRAM_CHAT_ID);  // Из Secrets.h
    body["text"]    = text;

    // URL Telegram Bot API для отправки сообщения
    const QUrl url(QString("https://api.telegram.org/bot%1/sendMessage")
                       .arg(QLatin1String(TELEGRAM_BOT_TOKEN)));  // Из Secrets.h

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // POST-запрос с JSON-телом.
    // QJsonDocument::Compact — без лишних пробелов (экономия трафика).
    QNetworkReply *reply = m_nam->post(
        req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    // Обработка ответа (асинхронно):
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        // Запоминаем время успешной отправки (для антиспама)
        AppSettings::get().setLastReportTime(
            QDateTime::currentDateTime().toString(Qt::ISODate));

        // Цепочка: после текста → фото/видео → аудио
        if (!m_pendingPhotoVideo.isEmpty())
            sendMediaGroup();           // Шаг 2: отправить альбом
        else if (!m_pendingAudio.isEmpty())
            sendNextAudio();            // Шаг 3: отправить аудио
        else
            emit sent();               // Всё отправлено!
    });
}

// ─── Шаг 2: Отправить альбом фото/видео ─────────────────────────────────────
// Telegram sendMediaGroup принимает до 10 медиафайлов одним запросом.
// Используем multipart/form-data — это формат как HTML-форма с файлами.
void BugReporter::sendMediaGroup()
{
    // QHttpMultiPart — строит multipart/form-data запрос из нескольких частей
    auto *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // ── Часть 1: chat_id ──────────────────────────────────────────────────────
    QHttpPart chatPart;
    chatPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"chat_id\""));
    chatPart.setBody(QByteArray(TELEGRAM_CHAT_ID));
    mp->append(chatPart);

    // ── Часть 2: media (JSON-массив описания файлов) ──────────────────────────
    // Telegram ожидает массив вида: [{"type":"photo","media":"attach://file_0"}, ...]
    // "attach://file_N" — ссылка на другую часть этого же multipart-запроса
    QJsonArray mediaArr;
    for (int i = 0; i < m_pendingPhotoVideo.size(); ++i) {
        QJsonObject item;
        item["type"]  = (kindOfFile(m_pendingPhotoVideo[i]) == FileKind::Video)
                        ? "video" : "photo";
        item["media"] = QString("attach://file_%1").arg(i);
        mediaArr.append(item);
    }
    QHttpPart mediaPart;
    mediaPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant("form-data; name=\"media\""));
    mediaPart.setBody(QJsonDocument(mediaArr).toJson(QJsonDocument::Compact));
    mp->append(mediaPart);

    // ── Части 3+: бинарные данные каждого файла ──────────────────────────────
    for (int i = 0; i < m_pendingPhotoVideo.size(); ++i) {
        // Открываем файл для чтения. QFile(path, mp) — mp станет владельцем файла.
        QFile *f = new QFile(m_pendingPhotoVideo[i], mp);
        if (!f->open(QIODevice::ReadOnly)) continue;  // Файл недоступен — пропускаем
        QHttpPart fp;
        fp.setHeader(QNetworkRequest::ContentDispositionHeader,
                     QVariant(QString("form-data; name=\"file_%1\"; filename=\"%2\"")
                         .arg(i).arg(QFileInfo(m_pendingPhotoVideo[i]).fileName())));
        fp.setBodyDevice(f);  // Читаем содержимое файла потоково (не грузим всё в RAM)
        mp->append(fp);
    }

    const QUrl url(QString("https://api.telegram.org/bot%1/sendMediaGroup")
                       .arg(QLatin1String(TELEGRAM_BOT_TOKEN)));
    QNetworkReply *reply = m_nam->post(QNetworkRequest(url), mp);
    mp->setParent(reply);  // mp удалится вместе с reply (не нужно удалять вручную)

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        // Альбом отправлен → переходим к аудио (если есть)
        if (!m_pendingAudio.isEmpty()) sendNextAudio();
        else                           emit sent();
    });
}

// ─── Шаг 3: Отправить следующий аудиофайл ────────────────────────────────────
// Telegram не поддерживает аудио в sendMediaGroup — каждый файл отправляется отдельно.
// Метод рекурсивно вызывает сам себя пока список m_pendingAudio не опустеет.
void BugReporter::sendNextAudio()
{
    // Список пуст — всё отправлено
    if (m_pendingAudio.isEmpty()) { emit sent(); return; }

    // takeFirst() — берём первый элемент и удаляем его из списка
    const QString path = m_pendingAudio.takeFirst();
    auto *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // Часть 1: chat_id
    QHttpPart chatPart;
    chatPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"chat_id\""));
    chatPart.setBody(QByteArray(TELEGRAM_CHAT_ID));
    mp->append(chatPart);

    // Часть 2: аудиофайл
    QFile *f = new QFile(path, mp);
    if (f->open(QIODevice::ReadOnly)) {
        QHttpPart fp;
        fp.setHeader(QNetworkRequest::ContentDispositionHeader,
                     QVariant(QString("form-data; name=\"audio\"; filename=\"%1\"")
                         .arg(QFileInfo(path).fileName())));
        fp.setBodyDevice(f);
        mp->append(fp);
    }

    const QUrl url(QString("https://api.telegram.org/bot%1/sendAudio")
                       .arg(QLatin1String(TELEGRAM_BOT_TOKEN)));
    QNetworkReply *reply = m_nam->post(QNetworkRequest(url), mp);
    mp->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        // Аудио отправлено → отправляем следующее (рекурсия через сигнал/слот)
        sendNextAudio();
    });
}
