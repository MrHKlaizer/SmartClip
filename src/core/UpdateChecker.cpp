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
#include <QTimer>               // Повтор запроса, если сеть моргнула
#include <QMap>

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
// Сначала спрашиваем GitHub API (там есть описание релиза и контрольная сумма).
// Если API не отвечает или отказал по лимиту — идём запасным путём через
// обычную страницу релизов (см. checkWeb).
void UpdateChecker::check(bool silent)
{
    m_silent  = silent;  // Запоминаем режим — нужен будет когда придёт ответ
    m_retried = false;
    checkApi();
}

// Общие заголовки запроса. Таймаут — чтобы на плохом мобильном интернете
// запрос не висел бесконечно, а честно сообщал об ошибке.
QNetworkRequest UpdateChecker::makeRequest(const QUrl &url) const
{
    QNetworkRequest req(url);
    // GitHub требует User-Agent, иначе отвечает 403 Forbidden.
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SmartClip/") + APP_VERSION);
    req.setTransferTimeout(15000);  // 15 секунд
    return req;
}

void UpdateChecker::checkApi()
{
    QNetworkRequest req = makeRequest(QUrl(GITHUB_API_URL));
    // Accept — просим ответ в формате GitHub JSON API
    req.setRawHeader("Accept", "application/vnd.github+json");

    // m_nam->get(req) — асинхронно отправляет GET-запрос: программа НЕ зависает,
    // а когда сервер ответит — Qt вызовет лямбду (через сигнал finished).
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() == QNetworkReply::NoError) {
            handleRelease(QJsonDocument::fromJson(reply->readAll()).object());
            return;
        }
        // 404 — на GitHub нет ни одного опубликованного релиза (черновики и
        // pre-release в «последний релиз» не считаются). Запасной путь тут не поможет.
        if (status == 404) {
            fail(tr("На GitHub пока нет опубликованной версии SmartClip."));
            return;
        }
        // 403/429 — лимит запросов к API (60 в час с одного IP). На мобильном
        // интернете один IP делят тысячи людей, и лимит часто уже исчерпан.
        // Страница релизов на github.com такого лимита не имеет.
        // 5xx — сбой самого API; сайт в это время обычно работает.
        if (status == 403 || status == 429 || status >= 500) {
            checkWeb();
            return;
        }
        // Сеть моргнула (часто на мобильном интернете) — повторяем один раз,
        // потом пробуем запасной путь
        if (status == 0 && !m_retried) {
            m_retried = true;
            QTimer::singleShot(3000, this, [this]() { checkApi(); });
            return;
        }
        if (status == 0) {
            checkWeb();
            return;
        }
        fail(tr("GitHub ответил ошибкой %1. Попробуйте позже.").arg(status));
    });
}

// ─── Запасной путь: страница релизов github.com ───────────────────────────────
// 1. https://github.com/<репо>/releases/latest перенаправляет на
//    .../releases/tag/<тег> — так узнаём тег последней версии.
// 2. .../releases/expanded_assets/<тег> — HTML со списком файлов релиза,
//    из него берём ссылку на установщик.
void UpdateChecker::checkWeb()
{
    const QString base = QStringLiteral("https://github.com/" GITHUB_REPO "/releases");
    QNetworkReply *reply = m_nam->get(makeRequest(QUrl(base + "/latest")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, base]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("Нет связи с GitHub. Проверьте интернет и попробуйте ещё раз."));
            return;
        }
        // Куда перенаправили: .../releases/tag/v2.0.0
        static const QRegularExpression tagRe(QStringLiteral("/releases/tag/([^/?#]+)"));
        const auto m = tagRe.match(reply->url().toString());
        if (!m.hasMatch()) {
            // Перенаправили на общую страницу релизов — релизов нет
            fail(tr("На GitHub пока нет опубликованной версии SmartClip."));
            return;
        }
        const QString tag = m.captured(1);

        QNetworkReply *assets = m_nam->get(makeRequest(QUrl(base + "/expanded_assets/" + tag)));
        connect(assets, &QNetworkReply::finished, this, [this, assets, tag]() {
            assets->deleteLater();
            if (assets->error() != QNetworkReply::NoError) {
                fail(tr("Нет связи с GitHub. Проверьте интернет и попробуйте ещё раз."));
                return;
            }
            // Ссылки на файлы релиза: href="/<репо>/releases/download/<тег>/<файл>"
            const QString html = QString::fromUtf8(assets->readAll());
            static const QRegularExpression hrefRe(
                QStringLiteral("href=\"(/[^\"]+/releases/download/[^\"]+\\.(?:exe|zip))\""),
                QRegularExpression::CaseInsensitiveOption);
            QJsonArray list;
            for (auto it = hrefRe.globalMatch(html); it.hasNext(); ) {
                const QString path = it.next().captured(1);
                QJsonObject a;
                a[QStringLiteral("name")] = path.section('/', -1);
                a[QStringLiteral("browser_download_url")] = QStringLiteral("https://github.com") + path;
                list.append(a);
            }
            // Описание — со страницы релиза (там оно уже в HTML)
            QNetworkReply *page = m_nam->get(makeRequest(
                QUrl(QStringLiteral("https://github.com/" GITHUB_REPO "/releases/tag/") + tag)));
            connect(page, &QNetworkReply::finished, this, [this, page, tag, list]() {
                page->deleteLater();
                QJsonObject obj;
                obj[QStringLiteral("tag_name")] = tag;
                obj[QStringLiteral("assets")] = list;
                if (page->error() == QNetworkReply::NoError) {
                    const QString html = releaseBodyHtml(QString::fromUtf8(page->readAll()));
                    if (!html.isEmpty()) obj[QStringLiteral("body")] = QString::fromLatin1(HTML_NOTES_PREFIX) + html;
                }
                handleRelease(obj);  // Без описания — тоже не страшно: в окне будет ссылка
            });
        });
    });
}

// Текст описания со страницы релиза: содержимое блока class="markdown-body"
QString UpdateChecker::releaseBodyHtml(const QString &page)
{
    const int cls = page.indexOf(QStringLiteral("class=\"markdown-body"));
    if (cls < 0) return {};
    const int start = page.indexOf('>', cls);
    if (start < 0) return {};
    // Конец блока — по парным <div ...> / </div>
    int depth = 1, i = start + 1;
    static const QRegularExpression tagRe(QStringLiteral("<(/?)div\\b"), QRegularExpression::CaseInsensitiveOption);
    while (depth > 0) {
        const auto m = tagRe.match(page, i);
        if (!m.hasMatch()) return {};
        depth += m.captured(1).isEmpty() ? 1 : -1;
        i = m.capturedEnd();
        if (depth == 0) return page.mid(start + 1, m.capturedStart() - start - 1).trimmed();
    }
    return {};
}

void UpdateChecker::fail(const QString &message)
{
    if (!m_silent)  // В тихом режиме (проверка при запуске) ошибку не показываем
        emit checkFailed(message);
}

// ─── Разбор релиза (из API или со страницы релизов) ──────────────────────────
void UpdateChecker::handleRelease(const QJsonObject &obj)
{
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
            // (только в ответе API; запасной путь без контрольной суммы)
            const QString digest = asset[QStringLiteral("digest")].toString();
            sha256 = digest.startsWith(QStringLiteral("sha256:")) ? digest.mid(7) : QString();
        }
    }

    // ── Проверяем что данные корректны ────────────────────────────────────
    if (version.isEmpty() || downloadUrl.isEmpty()) {
        fail(tr("В последнем релизе на GitHub нет файла установщика."));
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
}

// ─── Большое или мелкое обновление ───────────────────────────────────────────
bool UpdateChecker::isMajorUpdate(const QString &newVersion)
{
    // versionToInt: 2.0.1 → 2000001; деление на 1000 отбрасывает третье число
    return versionToInt(newVersion) / 1000 > versionToInt(QStringLiteral(APP_VERSION)) / 1000;
}

// ─── Описание на нужном языке ────────────────────────────────────────────────
QString UpdateChecker::localizedNotes(const QString &body, const QString &lang)
{
    // Метки: <!-- lang: ru --> (комментарий) или <a name="lang-ru"></a> (якорь —
    // он, в отличие от комментария, остаётся и на странице релиза, где GitHub
    // превращает его в <a name="user-content-lang-ru">)
    static const QRegularExpression marker(
        QStringLiteral(R"re(<!--\s*lang\s*:\s*([A-Za-z-]+)\s*-->|<a\s+name="(?:user-content-)?lang-([A-Za-z-]+)"[^>]*>\s*</a>)re"),
        QRegularExpression::CaseInsensitiveOption);
    QMap<QString, QString> parts;  // язык → текст
    QString first;
    auto it = marker.globalMatch(body);
    QRegularExpressionMatch prev;
    while (true) {
        const bool more = it.hasNext();
        const QRegularExpressionMatch cur = more ? it.next() : QRegularExpressionMatch();
        if (prev.hasMatch()) {
            const int from = prev.capturedEnd();
            const int to = more ? cur.capturedStart() : body.size();
            QString code = (prev.captured(1).isEmpty() ? prev.captured(2) : prev.captured(1)).toLower();
            if (code == QLatin1String("uk")) code = QStringLiteral("ua");  // ISO-код украинского
            const QString text = body.mid(from, to - from).trimmed();
            parts.insert(code, text);
            if (first.isEmpty()) first = text;
        }
        if (!more) break;
        prev = cur;
    }
    if (parts.isEmpty()) {
        // Меток нет — весь текст, без служебных комментариев
        QString plain = body;
        plain.remove(QRegularExpression(QStringLiteral("<!--.*?-->"),
                                        QRegularExpression::DotMatchesEverythingOption));
        return plain.trimmed();
    }
    for (const QString &code : {lang.toLower(), QStringLiteral("en"), QStringLiteral("ru")})
        if (parts.contains(code)) return parts.value(code);
    return first;
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
