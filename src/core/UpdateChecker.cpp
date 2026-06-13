#include "UpdateChecker.h"
#include "Version.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{}

void UpdateChecker::check(bool silent)
{
    m_silent = silent;

    QNetworkRequest req(QUrl(GITHUB_API_URL));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("SmartClip/") + APP_VERSION);
    req.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            if (!m_silent)
                emit checkFailed(reply->errorString());
            return;
        }

        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        // tag_name обычно "v1.2.3" или "1.2.3"
        QString tag     = obj[QStringLiteral("tag_name")].toString();
        QString version = tag.startsWith('v') ? tag.mid(1) : tag;

        // Ищем asset с "Installer" в имени
        QString downloadUrl;
        for (const QJsonValue &val : obj[QStringLiteral("assets")].toArray()) {
            QJsonObject asset = val.toObject();
            QString name = asset[QStringLiteral("name")].toString();
            if (name.contains(QStringLiteral("Installer"), Qt::CaseInsensitive)) {
                downloadUrl = asset[QStringLiteral("browser_download_url")].toString();
                break;
            }
        }

        if (version.isEmpty() || downloadUrl.isEmpty()) {
            if (!m_silent)
                emit checkFailed(QStringLiteral("Не удалось разобрать данные релиза."));
            return;
        }

        if (versionToInt(version) > versionToInt(QStringLiteral(APP_VERSION))) {
            emit updateAvailable(version, downloadUrl);
        } else {
            if (!m_silent) emit upToDate();
        }
    });
}

int UpdateChecker::versionToInt(const QString &ver)
{
    const QStringList parts = ver.split('.');
    int result = 0;
    for (int i = 0; i < 3 && i < parts.size(); ++i)
        result = result * 1000 + parts[i].toInt();
    return result;
}
