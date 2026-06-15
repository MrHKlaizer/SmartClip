#include "BugReporter.h"
#include "Secrets.h"
#include "Version.h"
#include "AppSettings.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QDateTime>
#include <QSysInfo>
#include <QFile>
#include <QFileInfo>

BugReporter::BugReporter(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{}

BugReporter::FileKind BugReporter::kindOfFile(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QStringList audioExts{"mp3","ogg","wav","m4a","flac","aac"};
    static const QStringList videoExts{"mp4","mov","avi","mkv","webm","m4v"};
    if (audioExts.contains(ext)) return FileKind::Audio;
    if (videoExts.contains(ext)) return FileKind::Video;
    return FileKind::Photo;
}

int BugReporter::hoursSinceLast()
{
    const QString last = AppSettings::get().lastReportTime();
    if (last.isEmpty()) return -1;
    return static_cast<int>(
        QDateTime::fromString(last, Qt::ISODate)
            .secsTo(QDateTime::currentDateTime()) / 3600);
}

bool BugReporter::canSend()
{
    const int h = hoursSinceLast();
    return h < 0 || h >= COOLDOWN_HOURS;
}

void BugReporter::send(Type type, const QString &description,
                       const QList<QString> &filePaths)
{
    m_pendingPhotoVideo.clear();
    m_pendingAudio.clear();
    for (const QString &p : filePaths) {
        if (kindOfFile(p) == FileKind::Audio)
            m_pendingAudio.append(p);
        else
            m_pendingPhotoVideo.append(p);
    }

    const QString header = (type == Type::Bug)
        ? QString::fromUtf8("🔴 ЖАЛОБА")
        : QString::fromUtf8("💡 ПОЖЕЛАНИЕ");

    const QString text = QString("%1\n\n%2\n\n\342\200\224\nSmartClip v%3\n%4")
        .arg(header, description.trimmed(),
             QLatin1String(APP_VERSION), QSysInfo::prettyProductName());

    QJsonObject body;
    body["chat_id"] = QLatin1String(TELEGRAM_CHAT_ID);
    body["text"]    = text;

    const QUrl url(QString("https://api.telegram.org/bot%1/sendMessage")
                       .arg(QLatin1String(TELEGRAM_BOT_TOKEN)));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_nam->post(
        req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        AppSettings::get().setLastReportTime(
            QDateTime::currentDateTime().toString(Qt::ISODate));

        if (!m_pendingPhotoVideo.isEmpty())
            sendMediaGroup();
        else if (!m_pendingAudio.isEmpty())
            sendNextAudio();
        else
            emit sent();
    });
}

void BugReporter::sendMediaGroup()
{
    auto *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart chatPart;
    chatPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"chat_id\""));
    chatPart.setBody(QByteArray(TELEGRAM_CHAT_ID));
    mp->append(chatPart);

    // media JSON: [{"type":"photo","media":"attach://file_0"}, ...]
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

    // Бинарные данные каждого файла
    for (int i = 0; i < m_pendingPhotoVideo.size(); ++i) {
        QFile *f = new QFile(m_pendingPhotoVideo[i], mp);
        if (!f->open(QIODevice::ReadOnly)) continue;
        QHttpPart fp;
        fp.setHeader(QNetworkRequest::ContentDispositionHeader,
                     QVariant(QString("form-data; name=\"file_%1\"; filename=\"%2\"")
                         .arg(i).arg(QFileInfo(m_pendingPhotoVideo[i]).fileName())));
        fp.setBodyDevice(f);
        mp->append(fp);
    }

    const QUrl url(QString("https://api.telegram.org/bot%1/sendMediaGroup")
                       .arg(QLatin1String(TELEGRAM_BOT_TOKEN)));
    QNetworkReply *reply = m_nam->post(QNetworkRequest(url), mp);
    mp->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        if (!m_pendingAudio.isEmpty()) sendNextAudio();
        else                           emit sent();
    });
}

void BugReporter::sendNextAudio()
{
    if (m_pendingAudio.isEmpty()) { emit sent(); return; }

    const QString path = m_pendingAudio.takeFirst();
    auto *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart chatPart;
    chatPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"chat_id\""));
    chatPart.setBody(QByteArray(TELEGRAM_CHAT_ID));
    mp->append(chatPart);

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
        sendNextAudio(); // следующий файл
    });
}
