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
#include <QCryptographicHash> // Хэш картинки — отличить новую картинку от повтора

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
    // Без этого мы не можем обратиться к m_db, m_suppressTimer и т.д.
    ClipboardManager *self = reinterpret_cast<ClipboardManager*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA)
    );

    // ── Обработка события «буфер изменился» ───────────────────────────────────
    if (msg == WM_CLIPBOARDUPDATE && self) {

        // Если буфер записал сам SmartClip (вставка из истории/закрепов) — не
        // сохраняем это повторно. Определяем по ВЛАДЕЛЬЦУ буфера: Qt записывает
        // через скрытое окно нашего же процесса. Так настоящее Ctrl+C сразу
        // после вставки не теряется (раньше игнорировалось всё подряд 800мс).
        {
            DWORD ownerPid = 0;
            if (HWND owner = GetClipboardOwner())
                GetWindowThreadProcessId(owner, &ownerPid);
            if (ownerPid == GetCurrentProcessId())
                return DefWindowProc(hwnd, msg, wParam, lParam);
            // Владельца нет (pasteFile открывает буфер без окна) — тогда
            // подстраховываемся коротким окном после нашей вставки
            if (!ownerPid && self->m_suppressTimer.isValid() &&
                self->m_suppressTimer.elapsed() < 800)
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
            for (QString ex : excluded) {
                ex = ex.trimmed();
                // Пользователь мог ввести «KeePass.exe» — сравниваем без расширения
                if (ex.endsWith(".exe", Qt::CaseInsensitive)) ex.chop(4);
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
            // GetClipboardData(CF_UNICODETEXT) → дескриптор глобальной памяти Windows.
            QString text;
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                // GlobalLock → получаем указатель на данные (блокируем дескриптор).
                if (auto *raw = static_cast<wchar_t*>(GlobalLock(hData))) {
                    text = QString::fromWCharArray(raw);  // wchar_t* → QString
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();  // Буфер держим открытым как можно меньше
            if (!text.isNull())
                self->handleText(text, appName);
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        // ════════════════════════════════════════════════════════════════════════
        //  ИЗОБРАЖЕНИЕ
        // ════════════════════════════════════════════════════════════════════════
        // CF_BITMAP  — BitBlt/PrintScreen, Paint
        // CF_DIB     — Device Independent Bitmap (Telegram, Discord, браузеры)
        // CF_DIBV5   — расширенный DIB с цветовым профилем (фоторедакторы)
        bool hasImage = IsClipboardFormatAvailable(CF_BITMAP) ||
                        IsClipboardFormatAvailable(CF_DIB)    ||
                        IsClipboardFormatAvailable(CF_DIBV5);

        // Закрываем буфер — Qt сам его откроет через QClipboard::image()
        CloseClipboard();

        if (hasImage && cfg.saveImages()) {
            // QClipboard::image() умеет читать CF_BITMAP, CF_DIB, CF_DIBV5 автоматически.
            QImage image = QApplication::clipboard()->image();
            if (!image.isNull())
                self->handleImage(image, appName);
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    // Все остальные сообщения — стандартная обработка Windows
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ─── Обработка скопированного текста ─────────────────────────────────────────
void ClipboardManager::handleText(const QString &text, const QString &appName)
{
    auto &cfg = AppSettings::get();

    // Фильтрация:
    if (text.trimmed().isEmpty()) return;
    if (text.length() < cfg.minTextLength()) return;  // Слишком короткий

    const bool recent = m_lastTextTimer.isValid() && m_lastTextTimer.elapsed() < 2000;

    // Тот же текст пришёл повторно почти сразу — это двойное событие от
    // Chrome/Electron, а не новое копирование. Игнорируем.
    if (recent && text == m_lastText) return;

    // ДРУГОЙ текст пришёл в первые 300мс — приложение сначала положило
    // «черновик», потом финальную версию. Оставляем только финальную.
    if (recent && m_lastTextTimer.elapsed() < 300 && m_lastTextId > 0)
        m_db->deleteHistory(m_lastTextId);

    // Дедупликация: такой текст уже есть в истории → убираем старую копию,
    // чтобы повторно скопированное оказалось наверху (раньше оно просто
    // не сохранялось и «терялось» где-то глубоко в истории).
    if (cfg.deduplication())
        m_db->removeTextDuplicates(text);

    qDebug() << "Скопирован текст длиной:" << text.length() << "из" << appName;
    m_lastTextId = m_db->addHistory("text", text, "", appName);
    m_lastText   = text;
    m_lastTextTimer.restart();
    emit historyChanged();  // Сигнал → MainWindow обновит список
}

// ─── Обработка скопированной картинки ────────────────────────────────────────
void ClipboardManager::handleImage(const QImage &image, const QString &appName)
{
    auto &cfg = AppSettings::get();

    // Хэш пикселей — чтобы понять, та же это картинка или новая
    const QImage norm = image.convertToFormat(QImage::Format_ARGB32);
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(QByteArrayView(reinterpret_cast<const char*>(norm.constBits()),
                             norm.sizeInBytes()));
    const QByteArray hash = h.result();

    const bool recent = m_lastImageTimer.isValid() && m_lastImageTimer.elapsed() < 3000;
    if (recent && hash == m_lastImageHash) return;  // Повторное событие — та же картинка

    // Win+Shift+S шлёт сначала «черновой» снимок, потом финальный.
    // Если новая картинка пришла сразу за предыдущей — заменяем её.
    if (recent && m_lastImageTimer.elapsed() < 1500 && m_lastImageId > 0)
        m_db->deleteHistory(m_lastImageId);  // Удалит и файл, если он не в закрепах

    QString fmt     = cfg.imageFormat();  // "PNG" или "JPEG"
    // Качество: для JPEG используем настройку, для PNG не нужно (-1 = по умолчанию)
    int     quality = (fmt == "JPEG") ? cfg.imageQuality() : -1;
    QString ext     = (fmt == "JPEG") ? "jpg" : "png";

    // Уникальное имя файла на основе текущего времени в миллисекундах
    QString filename = QString("img_%1.%2")
        .arg(QDateTime::currentMSecsSinceEpoch()).arg(ext);
    QString filepath = m_db->imagesPath() + "/" + filename;

    // Сохраняем картинку на диск
    if (image.save(filepath, fmt.toUtf8().constData(), quality)) {
        qDebug() << "Скопирована картинка:" << filename << "из" << appName;
        // Для изображений: content = "" (файл хранится по filepath)
        m_lastImageId   = m_db->addHistory("image", "", filepath, appName);
        m_lastImageHash = hash;
        m_lastImageTimer.restart();
        emit historyChanged();
    }
}
