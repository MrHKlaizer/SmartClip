// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  ClipboardManager.cpp — Реализация перехвата буфера обмена                 ║
// ║                                                                              ║
// ║  Этот файл содержит всю логику «подслушивания» буфера обмена Windows.     ║
// ║  Главная часть — статическая функция WndProc, которую Windows вызывает     ║
// ║  каждый раз при изменении буфера.                                           ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "ClipboardManager.h"
#include "Database.h"      // addHistory() — сохранение в историю
#include "AppSettings.h"   // Настройки (исключения, форматы, дедупликация)

// Qt-модули:
#include <QApplication>    // QApplication::clipboard() — чтение картинки из буфера
#include <QClipboard>      // Qt-обёртка над системным буфером обмена
#include <QDebug>          // qDebug() — вывод в консоль разработчика
#include <QImage>          // Хранение изображения в памяти (RGB/ARGB пиксели)
#include <QDateTime>       // Для генерации уникального имени файла картинки
#include <QFileInfo>       // Для получения имени exe из полного пути
#include <QSqlQuery>       // Прямые SQL-запросы для проверки дедупликации

// ─── Конструктор ─────────────────────────────────────────────────────────────
ClipboardManager::ClipboardManager(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_hwnd(nullptr)  // Окно ещё не создано — инициализируем нулём
{
}

// ─── Деструктор ───────────────────────────────────────────────────────────────
// Вызывается при удалении объекта. Гарантирует освобождение ресурсов Windows.
ClipboardManager::~ClipboardManager()
{
    stop();  // Отписываемся и уничтожаем скрытое окно
}

// ─── Запуск мониторинга ───────────────────────────────────────────────────────
// Создаёт скрытое Win32-окно и подписывает его на уведомления буфера обмена.
bool ClipboardManager::start()
{
    // ── Регистрируем класс окна ───────────────────────────────────────────────
    // WNDCLASS — описание «шаблона» для окон этого класса.
    // Нам нужно только задать имя и функцию обработки сообщений.
    WNDCLASS wc = {};
    wc.lpfnWndProc   = WndProc;                   // Наш обработчик сообщений
    wc.hInstance     = GetModuleHandle(nullptr);   // Дескриптор текущего .exe
    wc.lpszClassName = L"SmartClipMessageWindow";  // Уникальное имя класса

    // Если класс уже зарегистрирован (после перезапуска mониторинга) — не страшно.
    RegisterClass(&wc);

    // ── Создаём скрытое окно-«слушатель» ─────────────────────────────────────
    // HWND_MESSAGE — специальный псевдо-родитель для «message-only» окон.
    // Такое окно: не отображается, не появляется в таскбаре, получает сообщения.
    m_hwnd = CreateWindowEx(
        0,                           // Расширенные стили (нет)
        L"SmartClipMessageWindow",   // Имя класса (зарегистрировали выше)
        L"SmartClip",                // Заголовок (не важен — окно скрыто)
        0,                           // Стили окна (нет)
        0, 0, 0, 0,                  // Позиция и размер (не важны)
        HWND_MESSAGE,                // ← ключевой момент: окно только для сообщений
        nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!m_hwnd) {
        qDebug() << "Ошибка создания скрытого окна, код:" << GetLastError();
        return false;
    }

    // Сохраняем указатель на this внутри оконной структуры Windows.
    // WndProc — статическая, не знает про this. Через GWLP_USERDATA мы
    // можем получить this обратно: reinterpret_cast<ClipboardManager*>(GetWindowLongPtr(...))
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // ── Подписываемся на уведомления буфера ──────────────────────────────────
    // AddClipboardFormatListener — регистрируем наше окно как «слушателя».
    // Теперь при каждом Ctrl+C в любой программе наш WndProc получит WM_CLIPBOARDUPDATE.
    if (!AddClipboardFormatListener(m_hwnd)) {
        qDebug() << "Ошибка подписки на буфер, код:" << GetLastError();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    qDebug() << "ClipboardManager запущен";
    return true;
}

// ─── Остановка мониторинга ────────────────────────────────────────────────────
void ClipboardManager::stop()
{
    if (m_hwnd) {
        RemoveClipboardFormatListener(m_hwnd);  // Отписываемся от буфера
        DestroyWindow(m_hwnd);                  // Уничтожаем скрытое окно
        m_hwnd = nullptr;
        qDebug() << "ClipboardManager остановлен";
    }
}

// ─── Обработчик сообщений Windows (статический) ──────────────────────────────
// Windows вызывает эту функцию при каждом сообщении адресованном нашему окну.
// Самое важное для нас — WM_CLIPBOARDUPDATE (что-то скопировали).
// Функция static — Windows не знает про C++-объекты, только про функции.
LRESULT CALLBACK ClipboardManager::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Восстанавливаем указатель на наш объект из GWLP_USERDATA.
    // Без этого мы не можем обратиться к m_db, m_suppressNext и т.д.
    ClipboardManager *self = reinterpret_cast<ClipboardManager*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA)
    );

    // ── Обработка события «буфер изменился» ───────────────────────────────────
    if (msg == WM_CLIPBOARDUPDATE && self) {

        // Если MainWindow сам положил данные в буфер — пропускаем событие,
        // чтобы не записывать вставку в историю повторно.
        if (self->m_suppressNext) {
            self->m_suppressNext = false;
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        // ── Определяем из какого приложения скопировали ───────────────────────
        // GetForegroundWindow() — активное окно в момент Ctrl+C.
        // Через его PID находим имя .exe процесса.
        QString appName;
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd) {
            DWORD pid = 0;
            GetWindowThreadProcessId(fgWnd, &pid);  // PID процесса активного окна
            // PROCESS_QUERY_LIMITED_INFORMATION — минимальные права (безопаснее чем PROCESS_ALL_ACCESS)
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProc) {
                wchar_t exePath[MAX_PATH] = {};
                DWORD size = MAX_PATH;
                // QueryFullProcessImageNameW → полный путь к .exe: "C:\...\chrome.exe"
                if (QueryFullProcessImageNameW(hProc, 0, exePath, &size))
                    // baseName() отрезает путь и расширение: "chrome"
                    appName = QFileInfo(QString::fromWCharArray(exePath)).baseName();
                CloseHandle(hProc);
            }
        }

        // ── Открываем буфер обмена ────────────────────────────────────────────
        // Некоторые приложения (браузеры, Notepad) держат буфер дольше обычного.
        // Пробуем 5 раз с паузой 10мс — итого до 50мс ожидания.
        bool opened = false;
        for (int i = 0; i < 5; ++i) {
            if (OpenClipboard(hwnd)) { opened = true; break; }
            Sleep(10);  // Ждём 10 миллисекунд и пробуем снова
        }
        if (!opened)
            return DefWindowProc(hwnd, msg, wParam, lParam);

        auto &cfg = AppSettings::get();  // Ссылка на настройки (не копия)

        // ── Проверяем список исключённых приложений ───────────────────────────
        // Пользователь может добавить приложения в чёрный список в настройках.
        // Например, KeePass.exe — чтобы пароли не попадали в историю.
        if (!appName.isEmpty()) {
            const QStringList excluded = cfg.excludedApps();
            for (const QString &ex : excluded) {
                if (appName.compare(ex, Qt::CaseInsensitive) == 0) {
                    CloseClipboard();
                    return DefWindowProc(hwnd, msg, wParam, lParam);
                }
            }
        }

        // ════════════════════════════════════════════════════════════════════════
        //  ТЕКСТ
        // ════════════════════════════════════════════════════════════════════════
        // CF_UNICODETEXT — стандартный формат текста в буфере (UTF-16).
        if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {

            // ── Дебаунс 500мс ─────────────────────────────────────────────────
            // Chrome и Electron-приложения шлют WM_CLIPBOARDUPDATE дважды:
            // сначала с "черновым" содержимым, потом с финальным — с задержкой
            // 100–400мс. 500мс гарантирует что второй вызов будет проигнорирован.
            if (self->m_lastTextTimer.isValid() && self->m_lastTextTimer.elapsed() < 500) {
                CloseClipboard();
                return DefWindowProc(hwnd, msg, wParam, lParam);
            }

            // GetClipboardData(CF_UNICODETEXT) → дескриптор глобальной памяти Windows.
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                // GlobalLock → получаем указатель на данные (блокируем дескриптор).
                wchar_t *raw = static_cast<wchar_t*>(GlobalLock(hData));
                if (raw) {
                    QString text = QString::fromWCharArray(raw);  // wchar_t* → QString
                    GlobalUnlock(hData);  // Разблокируем дескриптор
                    CloseClipboard();

                    // Фильтрация:
                    if (text.trimmed().isEmpty())
                        return DefWindowProc(hwnd, msg, wParam, lParam);

                    if (text.length() < cfg.minTextLength())  // Слишком короткий
                        return DefWindowProc(hwnd, msg, wParam, lParam);

                    // Дедупликация: не сохранять если такой текст уже есть
                    if (cfg.deduplication()) {
                        QSqlQuery q;
                        q.prepare("SELECT COUNT(*) FROM history WHERE type='text' AND content=:c");
                        q.bindValue(":c", text);
                        q.exec();
                        if (q.next() && q.value(0).toInt() > 0)
                            return DefWindowProc(hwnd, msg, wParam, lParam);
                    }

                    // Сохраняем в историю
                    self->m_lastTextTimer.restart();  // Сбрасываем дебаунс-таймер
                    qDebug() << "Скопирован текст длиной:" << text.length() << "из" << appName;
                    self->m_db->addHistory("text", text, "", appName);
                    emit self->historyChanged();  // Сигнал → MainWindow обновит список
                    return DefWindowProc(hwnd, msg, wParam, lParam);
                }
                GlobalUnlock(hData);
            }
            CloseClipboard();
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        // ════════════════════════════════════════════════════════════════════════
        //  ИЗОБРАЖЕНИЕ
        // ════════════════════════════════════════════════════════════════════════
        // Проверяем разные форматы картинок которые могут быть в буфере:
        // CF_BITMAP  — BitBlt/PrintScreen, Paint
        // CF_DIB     — Device Independent Bitmap (Telegram, Discord, браузеры)
        // CF_DIBV5   — расширенный DIB с цветовым профилем (фоторедакторы)
        bool hasImage = IsClipboardFormatAvailable(CF_BITMAP) ||
                        IsClipboardFormatAvailable(CF_DIB)    ||
                        IsClipboardFormatAvailable(CF_DIBV5);

        if (hasImage) {
            // Пользователь выключил сохранение картинок — пропускаем
            if (!cfg.saveImages()) {
                CloseClipboard();
                return DefWindowProc(hwnd, msg, wParam, lParam);
            }

            // ── Дебаунс 1500мс для картинок ───────────────────────────────────
            // Win+Shift+S (Snipping Tool / Ножницы) — отправляет WM_CLIPBOARDUPDATE
            // два раза: сначала «пустой» снимок, затем финальный.
            // Задержка 1500мс гарантирует что мы получим только финальный.
            if (self->m_lastImageTimer.isValid() && self->m_lastImageTimer.elapsed() < 1500) {
                CloseClipboard();
                return DefWindowProc(hwnd, msg, wParam, lParam);
            }

            // Закрываем буфер — Qt сам его откроет через QClipboard::image()
            CloseClipboard();

            // QClipboard::image() умеет читать CF_BITMAP, CF_DIB, CF_DIBV5 автоматически.
            QImage image = QApplication::clipboard()->image();
            if (!image.isNull()) {
                self->m_lastImageTimer.restart();  // Сбрасываем дебаунс-таймер

                QString fmt     = cfg.imageFormat();  // "PNG" или "JPEG"
                // Качество: для JPEG используем настройку, для PNG не нужно (-1 = по умолчанию)
                int     quality = (fmt == "JPEG") ? cfg.imageQuality() : -1;
                QString ext     = (fmt == "JPEG") ? "jpg" : "png";

                // Уникальное имя файла на основе текущего времени в миллисекундах
                QString filename = QString("img_%1.%2")
                    .arg(QDateTime::currentMSecsSinceEpoch()).arg(ext);
                QString filepath = self->m_db->imagesPath() + "/" + filename;

                // Сохраняем картинку на диск
                if (image.save(filepath, fmt.toUtf8().constData(), quality)) {
                    qDebug() << "Скопирована картинка:" << filename << "из" << appName;
                    // Для изображений: content = "" (файл хранится по filepath)
                    self->m_db->addHistory("image", "", filepath, appName);
                    emit self->historyChanged();
                }
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        // Формат не поддерживается (файл, HTML и т.д.) — закрываем буфер
        CloseClipboard();
    }

    // Все остальные сообщения — стандартная обработка Windows
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
