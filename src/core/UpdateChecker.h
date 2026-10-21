// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  UpdateChecker.h — Проверка наличия обновлений через GitHub                ║
// ║                                                                              ║
// ║  Этот класс умеет делать одно: отправить запрос на GitHub и узнать,        ║
// ║  есть ли версия новее чем та что установлена у пользователя.               ║
// ║                                                                              ║
// ║  Как это работает:                                                          ║
// ║  1. SmartClip запускается                                                   ║
// ║  2. Через 10 секунд (чтобы не тормозить старт) отправляет запрос на        ║
// ║     GitHub API: «какая последняя версия SmartClip?»                        ║
// ║  3. GitHub отвечает: «1.0.2, вот ссылка на скачивание»                    ║
// ║  4. UpdateChecker сравнивает: если на GitHub версия новее — сообщает об    ║
// ║     этом через сигнал updateAvailable                                       ║
// ║  5. MainWindow получает сигнал и показывает диалог обновления              ║
// ║                                                                              ║
// ║  Вся работа происходит АСИНХРОННО — программа не зависает пока ждёт ответ ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

// QObject — базовый класс всех объектов Qt, нужен для системы сигналов/слотов
#include <QObject>
// QNetworkAccessManager — менеджер сетевых запросов Qt (аналог fetch() в JavaScript)
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QJsonObject>

// ─── Класс UpdateChecker ──────────────────────────────────────────────────────
// Наследуется от QObject — это даёт доступ к системе сигналов Qt.
// Сигналы — это как «события»: когда проверка завершится, UpdateChecker
// «испускает» сигнал, и все кто подписался — получают уведомление.
class UpdateChecker : public QObject
{
    Q_OBJECT  // Обязательный макрос Qt для использования сигналов и слотов

public:
    // Конструктор. parent — владелец объекта (Qt автоматически удалит его из памяти)
    explicit UpdateChecker(QObject *parent = nullptr);

    // Запустить проверку обновлений.
    // silent = true  → не сообщать если обновлений нет (тихий режим при старте)
    // silent = false → сообщать о любом результате (при ручной проверке кнопкой)
    void check(bool silent = true);

    // Большое ли обновление: поменялось первое или второе число версии
    // (2.0 → 2.1, 2.0 → 3.0). О таком сообщаем сразу (уведомление в трее и
    // окно). Мелкие правки (2.0 → 2.0.1) — только плашка в настройках.
    static bool isMajorUpdate(const QString &newVersion);

    // Описание релиза на языке интерфейса. В описании на GitHub языки
    // разделены метками <!-- lang: ru -->, <!-- lang: en -->, <!-- lang: ua -->,
    // <!-- lang: de --> (на странице GitHub метки не видны). Нет нужного
    // языка — английский, потом русский; меток нет — весь текст.
    static QString localizedNotes(const QString &body, const QString &lang);

    // Описание, взятое со страницы релиза (запасной путь), — это HTML, а не
    // Markdown; такое описание начинается с этой метки
    static constexpr const char *HTML_NOTES_PREFIX = "<!--html-->";
    static bool notesAreHtml(const QString &body) { return body.startsWith(QLatin1String(HTML_NOTES_PREFIX)); }

// ─── Сигналы — «оповещения» которые UpdateChecker отправляет подписчикам ─────
// Сигналы не имеют тела — Qt генерирует код для них автоматически.
// Подписаться: connect(checker, &UpdateChecker::updateAvailable, this, &MyClass::onUpdate)
signals:
    // Найдена новая версия!
    // newVersion    — строка типа "1.1.0" — номер новой версии
    // downloadUrl   — ссылка на скачивание установщика
    // releaseNotes  — описание что нового (из GitHub Releases)
    // sha256        — контрольная сумма файла (GitHub считает её сам), может быть пустой
    void updateAvailable(const QString &newVersion,
                         const QString &downloadUrl,
                         const QString &releaseNotes,
                         const QString &sha256);

    // Версия актуальная — обновлений нет (сигналится только если silent=false)
    void upToDate();

    // Произошла ошибка при проверке (нет интернета, GitHub недоступен и т.д.)
    void checkFailed(const QString &error);

private:
    // Менеджер сетевых запросов — через него отправляем HTTP-запрос на GitHub API.
    // Создаётся один раз в конструкторе и живёт всё время работы UpdateChecker.
    QNetworkAccessManager *m_nam;

    // Флаг тихого режима: сохраняем при вызове check() и используем в коллбэке
    bool m_silent = true;
    bool m_retried = false;  // Уже повторяли запрос после сбоя сети

    QNetworkRequest makeRequest(const QUrl &url) const;  // User-Agent + таймаут
    void checkApi();                                     // GitHub API (основной путь)
    void checkWeb();                                     // Страница релизов (запасной путь)
    void handleRelease(const QJsonObject &obj);          // Разобрать релиз и сравнить версии
    void fail(const QString &message);                   // Сообщить об ошибке (если не тихий режим)
    static QString releaseBodyHtml(const QString &page);  // Описание со страницы релиза

    // Вспомогательный метод: конвертирует строку версии в целое число для сравнения.
    // Пример: "1.2.3" → 1*1000000 + 2*1000 + 3 = 1002003
    // Так легко сравнивать: если remote > local — есть обновление.
    static int versionToInt(const QString &ver);
};
