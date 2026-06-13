#pragma once

#include <QObject>
#include <QNetworkAccessManager>

// ─── UpdateChecker ────────────────────────────────────────────────────────────
// Проверяет GitHub Releases API на наличие новой версии.
// Использование:
//   auto *uc = new UpdateChecker(this);
//   connect(uc, &UpdateChecker::updateAvailable, this, &MainWindow::onUpdateAvailable);
//   uc->check(); // silent=true по умолчанию
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    // silent=true → не сигналить если уже актуальная версия
    void check(bool silent = true);

signals:
    void updateAvailable(const QString &newVersion, const QString &downloadUrl);
    void upToDate();
    void checkFailed(const QString &error);

private:
    QNetworkAccessManager *m_nam;
    bool m_silent = true;

    // "1.2.3" → 1002003 (для сравнения)
    static int versionToInt(const QString &ver);
};
