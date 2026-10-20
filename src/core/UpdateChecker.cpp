// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  UpdateChecker.cpp — Реализация проверки обновлений                        ║
// ║                                                                              ║
// ║  Этот файл содержит реальный код: как именно отправить запрос на GitHub,   ║
// ║  разобрать ответ и сравнить версии.                                          ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "UpdateChecker.h"  // Описание класса
#include "Version.h"        // APP_VERSION и GITHUB_API_URL

// Qt-модули для работы с сетью и JSON:
#include <QNetworkRequest>  // Описание HTTP-запроса (URL, заголовки)
#include <QNetworkReply>    // Ответ сервера (статус, тело)
#include <QJsonDocument>        // Парсер JSON-документа
#include <QJsonObject>          // Доступ к полям JSON-объекта (ключ → значение)
#include <QJsonArray>           // Итерация по JSON-массиву (список assets)
#include <QUrl>                 // Класс для работы с URL
#include <QRegularExpression>   // Для извлечения версии из тега любого формата

// ─── Конструктор ─────────────────────────────────────────────────────────────
// Создаём объект. Список инициализации ": QObject(parent)" передаёт родителя
// в базовый класс — Qt автоматически удалит нас из памяти вместе с parent.
// QNetworkAccessManager(this) — менеджер сетевых запросов, привязан к нам (this),
// поэтому удалится вместе с UpdateChecker.
UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{}

// ─── Запуск проверки ──────────────────────────────────────────────────────────
// Отправляет GET-запрос на GitHub API и асинхронно ждёт ответа.
// Вся логика разбора ответа находится в лямбда-функции внутри connect().
void UpdateChecker::check(bool silent)
{
    m_silent = silent;  // Запоминаем режим — нужен будет когда придёт ответ

    // ── Формируем HTTP-запрос ──────────────────────────────────────────────────
    QNetworkRequest req(QUrl(GITHUB_API_URL));
    // User-Agent — представляемся серверу: "SmartClip/1.0.2"
    // GitHub требует User-Agent, иначе отвечает 403 Forbidden.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("SmartClip/") + APP_VERSION);
    // Accept — просим ответ в формате GitHub JSON API v1
    req.setRawHeader("Accept", "application/vnd.github+json");

    // ── Отправляем запрос ──────────────────────────────────────────────────────
    // m_nam->get(req) — асинхронно отправляет GET-запрос.
    // Программа НЕ зависает: запрос уходит в фон, и когда сервер ответит —
    // Qt вызовет нашу лямбда-функцию (через сигнал finished).
    QNetworkReply *reply = m_nam->get(req);

    // ── Обработка ответа (лямбда-функция) ─────────────────────────────────────
    // connect() подписывается на сигнал reply->finished.
    // Когда GitHub ответит — Qt вызовет этот код.
    // [this, reply] — захватываем оба объекта чтобы использовать внутри лямбды.
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // deleteLater() — попросить Qt удалить reply после того как мы выйдем.
        // Нельзя удалять прямо здесь (мы внутри его же сигнала).
        reply->deleteLater();

        // ── Проверяем: была ли сетевая ошибка? ────────────────────────────────
        if (reply->error() != QNetworkReply::NoError) {
            // Нет интернета, GitHub недоступен, таймаут и т.д.
            if (!m_silent)  // В тихом режиме ошибку не показываем
                emit checkFailed(reply->errorString());
            return;  // Дальше разбирать нечего
        }

        // ── Разбираем JSON-ответ ───────────────────────────────────────────────
        // reply->readAll() — читаем тело ответа как массив байт
        // QJsonDocument::fromJson() — парсим в дерево объектов
        // .object() — корневой объект (словарь полей)
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        // ── Читаем номер версии ────────────────────────────────────────────────
        // Тег может быть любого формата: "v1.2.3", "SmartClip_Installer_v1.2.3"
        // Regex вытащит "X.Y.Z" из любого варианта.
        QString tag = obj[QStringLiteral("tag_name")].toString();
        QRegularExpression versionRe(QStringLiteral(R"((\d+(?:\.\d+){1,2}))"));  // "1.2" или "1.2.3"
        auto vMatch = versionRe.match(tag);
        QString version = vMatch.hasMatch() ? vMatch.captured(1) : tag;

        // ── Читаем описание релиза ─────────────────────────────────────────────
        // Поле "body" — текст описания релиза (что нового).
        // .trimmed() убирает лишние пробелы и переносы строк по краям.
        QString releaseNotes = obj[QStringLiteral("body")].toString().trimmed();

        // ── Ищем ссылку на установщик ─────────────────────────────────────────
        // Приоритет: .exe (standalone установщик). Если нет — берём первый .zip
        // (архив со структурой _data\ который распакует MainWindow через PowerShell).
        // Массив assets содержит только вручную загруженные файлы — source code там нет.
        // Начиная с 1.0.5 релиз — один файл SmartClip_Installer_vX.Y.Z.exe.
        // Оценка: 3 = .exe с «Installer»/«Setup» в имени, 2 = любой .exe, 1 = .zip (старый формат)
        QString downloadUrl, sha256;
        int bestScore = 0;
        for (const QJsonValue &val : obj[QStringLiteral("assets")].toArray()) {
            QJsonObject asset = val.toObject();
            QString name = asset[QStringLiteral("name")].toString();
            int score = 0;
            if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
                score = (name.contains(QStringLiteral("Installer"), Qt::CaseInsensitive) ||
                         name.contains(QStringLiteral("Setup"), Qt::CaseInsensitive)) ? 3 : 2;
            else if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
                score = 1;
            if (score > bestScore) {
                bestScore   = score;
                downloadUrl = asset[QStringLiteral("browser_download_url")].toString();
                // GitHub сам считает SHA-256 каждого файла: "sha256:abcdef..."
                const QString digest = asset[QStringLiteral("digest")].toString();
                sha256 = digest.startsWith(QStringLiteral("sha256:")) ? digest.mid(7) : QString();
            }
        }

        // ── Проверяем что данные корректны ────────────────────────────────────
        if (version.isEmpty() || downloadUrl.isEmpty()) {
            if (!m_silent)
                emit checkFailed(QStringLiteral("Не удалось разобрать данные релиза."));
            return;
        }

        // ── Сравниваем версии ─────────────────────────────────────────────────
        // versionToInt("1.2.3") → 1002003
        // Если число на GitHub > нашего → есть обновление.
        if (versionToInt(version) > versionToInt(QStringLiteral(APP_VERSION))) {
            emit updateAvailable(version, downloadUrl, releaseNotes, sha256);
        } else {
            // Версия актуальная. При ручной проверке (silent=false) сообщаем об этом.
            if (!m_silent) emit upToDate();
        }
    });
}

// ─── Конвертация версии в число ───────────────────────────────────────────────
// Превращает строку "1.2.3" в целое число 1002003.
// Так удобно сравнивать версии одним оператором >.
// Алгоритм: берём по одной части через split('.') и накапливаем:
//   "" → 0
//   "1" → 0*1000 + 1 = 1
//   "1.2" → 1*1000 + 2 = 1002
//   "1.2.3" → 1002*1000 + 3 = 1002003
int UpdateChecker::versionToInt(const QString &ver)
{
    const QStringList parts = ver.split('.');  // ["1", "2", "3"]
    int result = 0;
    // Всегда ровно 3 части (MAJOR.MINOR.PATCH): недостающие считаем нулями,
    // иначе "1.1" дало бы 1001 и оказалось бы «меньше» чем "1.0.0" = 1000000.
    for (int i = 0; i < 3; ++i)
        result = result * 1000 + parts.value(i).toInt();
    return result;
}
