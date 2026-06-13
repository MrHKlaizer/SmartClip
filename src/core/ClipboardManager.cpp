#include "ClipboardManager.h"
#include "Database.h"
#include "AppSettings.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QImage>
#include <QDateTime>
#include <QFileInfo>
#include <QSqlQuery>

ClipboardManager::ClipboardManager(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_hwnd(nullptr)
{
}

ClipboardManager::~ClipboardManager()
{
    stop();
}

bool ClipboardManager::start()
{
    // Описываем класс окна для Windows
    WNDCLASS wc = {};
    wc.lpfnWndProc   = WndProc;                      // функция обработки сообщений
    wc.hInstance     = GetModuleHandle(nullptr);      // текущий процесс
    wc.lpszClassName = L"SmartClipMessageWindow";     // имя класса

    // Регистрируем класс окна (если уже зарегистрирован — не страшно)
    RegisterClass(&wc);

    // Создаём скрытое окно — HWND_MESSAGE означает "только для сообщений, не отображать"
    m_hwnd = CreateWindowEx(
        0,
        L"SmartClipMessageWindow",
        L"SmartClip",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,   // ← ключевой момент: окно невидимо и не в таскбаре
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (!m_hwnd) {
        qDebug() << "Ошибка создания скрытого окна, код:" << GetLastError();
        return false;
    }

    // Сохраняем указатель на this внутри оСкна — нужно чтобы WndProc мог
    // обратиться к нашему объекту (WndProc статическая, не знает про this)
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Подписываемся на уведомления об изменении буфера обмена
    if (!AddClipboardFormatListener(m_hwnd)) {
        qDebug() << "Ошибка подписки на буфер, код:" << GetLastError();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    qDebug() << "ClipboardManager запущен";
    return true;
}

void ClipboardManager::stop()
{
    if (m_hwnd) {
        RemoveClipboardFormatListener(m_hwnd);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        qDebug() << "ClipboardManager остановлен";
    }
}

// Эта функция вызывается Windows каждый раз когда приходит сообщение
LRESULT CALLBACK ClipboardManager::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Достаём указатель на наш объект из окна
    ClipboardManager *self = reinterpret_cast<ClipboardManager*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA)
    );

    if (msg == WM_CLIPBOARDUPDATE && self) {
        // Если MainWindow сам положил данные — пропускаем одно событие
        if (self->m_suppressNext) {
            self->m_suppressNext = false;
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        // Определяем из какого приложения скопировали
        QString appName;
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd) {
            DWORD pid = 0;
            GetWindowThreadProcessId(fgWnd, &pid);
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProc) {
                wchar_t exePath[MAX_PATH] = {};
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(hProc, 0, exePath, &size))
                    appName = QFileInfo(QString::fromWCharArray(exePath)).baseName();
                CloseHandle(hProc);
            }
        }

        // Пробуем открыть буфер — браузеры/Notepad могут держать его дольше
        bool opened = false;
        for (int i = 0; i < 5; ++i) {
            if (OpenClipboard(hwnd)) { opened = true; break; }
            Sleep(10);
        }
        if (!opened)
            return DefWindowProc(hwnd, msg, wParam, lParam);

        auto &cfg = AppSettings::get();

        // Исключения приложений
        if (!appName.isEmpty()) {
            const QStringList excluded = cfg.excludedApps();
            for (const QString &ex : excluded) {
                if (appName.compare(ex, Qt::CaseInsensitive) == 0) {
                    CloseClipboard();
                    return DefWindowProc(hwnd, msg, wParam, lParam);
                }
            }
        }

        // ── Текст ────────────────────────────────────────────────────────────
        if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            // Дебаунс 100мс для текста
            if (self->m_lastTextTimer.isValid() && self->m_lastTextTimer.elapsed() < 100) {
                CloseClipboard();
                return DefWindowProc(hwnd, msg, wParam, lParam);
            }

            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                wchar_t *raw = static_cast<wchar_t*>(GlobalLock(hData));
                if (raw) {
                    QString text = QString::fromWCharArray(raw);
                    GlobalUnlock(hData);
                    CloseClipboard();

                    if (text.trimmed().isEmpty())
                        return DefWindowProc(hwnd, msg, wParam, lParam);
                    if (text.length() < cfg.minTextLength())
                        return DefWindowProc(hwnd, msg, wParam, lParam);

                    if (cfg.deduplication()) {
                        QSqlQuery q;
                        q.prepare("SELECT COUNT(*) FROM history WHERE type='text' AND content=:c");
                        q.bindValue(":c", text);
                        q.exec();
                        if (q.next() && q.value(0).toInt() > 0)
                            return DefWindowProc(hwnd, msg, wParam, lParam);
                    }

                    self->m_lastTextTimer.restart();
                    qDebug() << "Скопирован текст длиной:" << text.length() << "из" << appName;
                    self->m_db->addHistory("text", text, "", appName);
                    emit self->historyChanged();
                    return DefWindowProc(hwnd, msg, wParam, lParam);
                }
                GlobalUnlock(hData);
            }
            CloseClipboard();
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        // ── Изображение ───────────────────────────────────────────────────────
        // CF_BITMAP  — скриншоты, Paint и т.д.
        // CF_DIB     — Telegram, Discord, браузеры
        // CF_DIBV5   — расширенный DIB (браузеры, фоторедакторы)
        bool hasImage = IsClipboardFormatAvailable(CF_BITMAP) ||
                        IsClipboardFormatAvailable(CF_DIB)    ||
                        IsClipboardFormatAvailable(CF_DIBV5);

        if (hasImage) {
            if (!cfg.saveImages()) {
                CloseClipboard();
                return DefWindowProc(hwnd, msg, wParam, lParam);
            }

            // Дебаунс 1500мс для картинок:
            // Win+Shift+S (Snipping Tool) стреляет WM_CLIPBOARDUPDATE дважды
            // с произвольным интервалом — 100мс недостаточно
            if (self->m_lastImageTimer.isValid() && self->m_lastImageTimer.elapsed() < 1500) {
                CloseClipboard();
                return DefWindowProc(hwnd, msg, wParam, lParam);
            }

            // Закрываем буфер — Qt сам его откроет и разберётся с форматом
            CloseClipboard();

            // QClipboard умеет читать CF_BITMAP, CF_DIB, CF_DIBV5 одним вызовом
            QImage image = QApplication::clipboard()->image();
            if (!image.isNull()) {
                self->m_lastImageTimer.restart();

                QString fmt     = cfg.imageFormat();
                int     quality = (fmt == "JPEG") ? cfg.imageQuality() : -1;
                QString ext     = (fmt == "JPEG") ? "jpg" : "png";

                QString filename = QString("img_%1.%2")
                    .arg(QDateTime::currentMSecsSinceEpoch()).arg(ext);
                QString filepath = self->m_db->imagesPath() + "/" + filename;

                if (image.save(filepath, fmt.toUtf8().constData(), quality)) {
                    qDebug() << "Скопирована картинка:" << filename << "из" << appName;
                    self->m_db->addHistory("image", "", filepath, appName);
                    emit self->historyChanged();
                }
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        CloseClipboard();
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
