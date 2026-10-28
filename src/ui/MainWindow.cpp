// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  MainWindow.cpp — Главное окно SmartClip                                   ║
// ║                                                                              ║
// ║  SmartClip — это полноэкранное прозрачное окно которое появляется          ║
// ║  поверх всех окон при нажатии горячей клавиши (Ctrl+Shift+V по умолчанию). ║
// ║                                                                              ║
// ║  Структура макета (ASCII-схема):                                             ║
// ║  ┌──────────────────────────────────────────────────────────────────────┐   ║
// ║  │ TopPanel: [История] [⭐ Закрепы]      [✏] [Профили] [↕] [✕] [⚙️]  │   ║
// ║  ├──────────────────────────────────────────────────────────────────────┤   ║
// ║  │ FoldersBar: [←] [📁Работа] [Клиент1] [🎬 Видео] [🎵 Аудио] [+]    │   ║
// ║  ├─────────────────┬──────────────────────────┬───────────────────────┤   ║
// ║  │ LeftColumn:     │  CenterGap (прозрачный)  │  RightColumn:         │   ║
// ║  │ текст, аудио   │  (клики проваливаются)    │  картинки, видео     │   ║
// ║  ├─────────────────┴──────────────────────────┴───────────────────────┤   ║
// ║  │ BottomPanel: [🔍 Поиск по тексту...]                               │   ║
// ║  └──────────────────────────────────────────────────────────────────────┘   ║
// ║                                                                              ║
// ║  Ключевые механизмы:                                                         ║
// ║  • FramelessWindowHint + WA_TranslucentBackground — прозрачный фон          ║
// ║  • WM_NCHITTEST HTTRANSPARENT — клики на прозрачный центр проваливают       ║
// ║  • WM_ACTIVATE — скрытие окна при потере фокуса (если фокус ушёл в другой  ║
// ║    процесс, не в собственный диалог)                                         ║
// ║  • pasteText/pasteImage/pasteFile — кладут данные в буфер и симулируют     ║
// ║    Ctrl+V через SendInput() с задержкой 150мс                               ║
// ║  • Ленивая загрузка картинок (loadNextImage) — через QTimer::singleShot(0) ║
// ║    чтобы UI не зависал при загрузке многих изображений                     ║
// ║  • Нечёткий поиск (fuzzyMatch) — Левенштейн + subsequence + prefix fuzzy   ║
// ║  • Drag-to-reorder (ClickableCard::dragStarted/Moved/Finished)              ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "MainWindow.h"
#include "ClickableCard.h"      // Карточка истории/закрепов с кликом и перетаскиванием
#include "SmartButton.h"        // Кнопка с QPainterPath (без артефактов)
#include "ImageViewer.h"        // Полноэкранный просмотр картинок
#include "HotkeyEdit.h"         // Поле захвата горячих клавиш
#include "Anim.h"               // Плавные анимации 2.0 (меню, окна)
#include "SmoothScroll.h"       // Плавная прокрутка колонок (этап 3)
#include "CardArc.h"           // Прокрутка «Дуга» (этап 4)
#include "CardShadow.h"        // Быстрая тень карточек
#include "ScrollTopButton.h"   // Кнопка «наверх» в колонках
#include "DateRuler.h"         // Шкала дат при прокрутке
#include "MediaCards.h"        // Аудио и видео прямо в карточках (2.1)
#include "VideoViewer.h"
#include "FilesStrip.h"
#include "DragSqueeze.h"
#include "GhostCards.h"
#include "FolderSlide.h"
#include "PhotoWindow.h"
#include "Carry.h"
#include <QImageReader>       // Большой просмотр видео
#include "SettingsDialog.h"     // Диалог настроек (5 вкладок)
#include "core/Database.h"      // SQLite CRUD
#include "core/AppSettings.h"   // Синглтон настроек
#include "core/Thumbs.h"        // Превью картинок в фоне

#include <QScreen>              // Размер экрана
#include <QKeyEvent>            // Клавиша Escape для скрытия окна
#include <QNetworkAccessManager>// HTTP для скачивания обновления
#include <QNetworkReply>        // Ответ HTTP-запроса
#include <QNetworkRequest>      // Запрос с URL и заголовками
#include <QProgressBar>         // Прогресс скачивания установщика
#include <QStandardPaths>       // TempLocation — путь для временных файлов
#include <QProcess>             // Запуск PowerShell для распаковки ZIP
#include <QDirIterator>         // Рекурсивный поиск Installer.exe в архиве
#include <QCryptographicHash>   // SHA-256 скачанного обновления
#include "core/Version.h"       // APP_VERSION — строка версии
#include "core/UpdateChecker.h" // Проверка обновлений через GitHub API
#include <QStyle>
#include <QApplication>         // clipboard(), primaryScreen()
#include <QClipboard>           // setText/setPixmap
#include <QTimer>               // singleShot — отложенный вызов
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QTextEdit>
#include <QIcon>
#include <QTextBrowser>
#include <QAbstractTextDocumentLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>          // Ползунок скорости анимаций (окно «Оформление»)
#include <QPropertyAnimation> // Переезд «пилюли» вкладок
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QLabel>
#include <QPixmap>
#include <QGraphicsDropShadowEffect>  // Тень для карточек и кнопок
#include <QPainter>
#include <QPaintEvent>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>    // std::sort, std::reverse, std::min, std::max
#include <functional>   // std::function — для рекурсивных лямбд
#include <windows.h>    // Win32 API: HWND, INPUT, SendInput, SetForegroundWindow
#include <atomic>
#include <windowsx.h>
#include <ShObjIdl.h>   // IShellItemImageFactory — миниатюры видео через Shell

// ─── SC_DROPFILES ────────────────────────────────────────────────────────────
// DROPFILES не экспортируется в MinGW в данной конфигурации — определяем вручную.
// Эта структура стабильна с Windows 3.1 и не менялась.
// Используется в pasteFile() чтобы «вставить» путь к файлу через CF_HDROP.
struct SC_DROPFILES {
    DWORD pFiles; // смещение до списка путей (всегда sizeof(SC_DROPFILES))
    POINT pt;     // точка сброса — для буфера обмена не используется
    BOOL  fNC;    // non-client area — не используется
    BOOL  fWide;  // TRUE = пути в UTF-16 (wchar_t); мы всегда передаём TRUE
};

// ─── Превью видео через Windows Shell ───────────────────────────────────────
// Те же миниатюры что Windows Проводник показывает в папке файлов.
// Используем IShellItemImageFactory — COM-интерфейс из ShObjIdl.h.

// Конвертируем HBITMAP в QPixmap через GDI (GetDIBits) — не зависим от Qt WinExtras.
// HBITMAP — это Win32 дескриптор (handle) растрового изображения в памяти.
// QPixmap — это Qt-картинка оптимизированная для рисования на экране.
static QPixmap hbitmapToPixmap(HBITMAP hBmp)
{
    BITMAP bm = {};
    if (!GetObject(hBmp, sizeof(BITMAP), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return {};

    BITMAPINFOHEADER bi = {};
    bi.biSize        = sizeof(BITMAPINFOHEADER);
    bi.biWidth       = bm.bmWidth;
    bi.biHeight      = -bm.bmHeight; // top-down
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return {};

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);
    HBITMAP hOld  = static_cast<HBITMAP>(SelectObject(hdcMem, hBmp));

    GetDIBits(hdcMem, hBmp, 0, bm.bmHeight,
              img.bits(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    // Shell возвращает BGRA — Qt Format_ARGB32_Premultiplied тоже BGRA на Windows, всё ок.
    return QPixmap::fromImage(img);
}

// Получаем миниатюру видео/файла через Windows Shell COM API.
// Параметры: filepath — полный путь к файлу, w/h — желаемый размер миниатюры.
static QPixmap shellThumbnail(const QString &filepath, int w, int h)
{
    // Используем нативные разделители — Shell API требует обратные слеши.
    // QDir::toNativeSeparators("C:/Video/clip.mp4") → "C:\Video\clip.mp4"
    QString native = QDir::toNativeSeparators(filepath);

    IShellItemImageFactory *pFactory = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(
        native.toStdWString().c_str(), nullptr,
        IID_PPV_ARGS(&pFactory));
    if (FAILED(hr) || !pFactory) return {};

    HBITMAP hBmp = nullptr;
    SIZE sz = { w, h };
    // SIIGBF_BIGGERSIZEOK — вернуть что есть, даже больше запрошенного.
    // Qt потом сам масштабирует до нужного размера через scaled().
    hr = pFactory->GetImage(sz, SIIGBF_BIGGERSIZEOK, &hBmp);
    pFactory->Release();
    if (FAILED(hr) || !hBmp) return {};

    QPixmap result = hbitmapToPixmap(hBmp);
    DeleteObject(hBmp);
    return result;
}

// Кадр-превью и длительность видео — в фоновом потоке (Shell, как в Проводнике).
// Возвращает [QImage, длительность в мс]. В фоне нельзя QPixmap — только QImage.
static QVariant videoPosterWork(const QString &filepath, QSize px)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    QImage img;
    qint64 durMs = 0;
    IShellItem2 *item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(QDir::toNativeSeparators(filepath).toStdWString().c_str(),
                                              nullptr, IID_PPV_ARGS(&item))) && item) {
        IShellItemImageFactory *factory = nullptr;
        if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) && factory) {
            HBITMAP hBmp = nullptr;
            const SIZE sz = { px.width(), px.height() };
            if (SUCCEEDED(factory->GetImage(sz, SIIGBF_BIGGERSIZEOK, &hBmp)) && hBmp) {
                BITMAP bm = {};
                if (GetObject(hBmp, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
                    BITMAPINFOHEADER bi = {};
                    bi.biSize = sizeof(bi);
                    bi.biWidth = bm.bmWidth;
                    bi.biHeight = -bm.bmHeight;  // Сверху вниз
                    bi.biPlanes = 1;
                    bi.biBitCount = 32;
                    bi.biCompression = BI_RGB;
                    img = QImage(bm.bmWidth, bm.bmHeight, QImage::Format_RGB32);
                    HDC dc = GetDC(nullptr);
                    GetDIBits(dc, hBmp, 0, bm.bmHeight, img.bits(), reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS);
                    ReleaseDC(nullptr, dc);
                }
                DeleteObject(hBmp);
            }
            factory->Release();
        }
        // System.Media.Duration — в 100-наносекундных единицах
        static const PROPERTYKEY kDuration = { { 0x64440490, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } }, 3 };
        ULONGLONG d = 0;
        if (SUCCEEDED(item->GetUInt64(kDuration, &d))) durMs = qint64(d / 10000);
        item->Release();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return QVariantList{ img, durMs };
}

// Превью любого файла, как в Проводнике (картинка, кадр видео, значок программы).
// В фоновом потоке. Значки приходят с прозрачностью, миниатюры фото — без неё.
static QImage fileThumbWork(const QString &filepath, QSize px)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    QImage img;
    IShellItemImageFactory *factory = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(QDir::toNativeSeparators(filepath).toStdWString().c_str(),
                                              nullptr, IID_PPV_ARGS(&factory))) && factory) {
        HBITMAP hBmp = nullptr;
        const SIZE sz = { px.width(), px.height() };
        // Картинкам и видео — превью, остальному — ЗНАЧОК программы: «миниатюра»
        // документа Word у Windows — пустой белый лист, а значок — с буквой W
        const QString cat = Files::category(filepath);
        const SIIGBF flags = (cat == QLatin1String("image") || cat == QLatin1String("video"))
                                 ? SIIGBF_BIGGERSIZEOK : (SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK);
        if (SUCCEEDED(factory->GetImage(sz, flags, &hBmp)) && hBmp) {
            BITMAP bm = {};
            if (GetObject(hBmp, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
                BITMAPINFOHEADER bi = {};
                bi.biSize = sizeof(bi);
                bi.biWidth = bm.bmWidth;
                bi.biHeight = -bm.bmHeight;  // Сверху вниз
                bi.biPlanes = 1;
                bi.biBitCount = 32;
                bi.biCompression = BI_RGB;
                img = QImage(bm.bmWidth, bm.bmHeight, QImage::Format_RGB32);
                HDC dc = GetDC(nullptr);
                GetDIBits(dc, hBmp, 0, bm.bmHeight, img.bits(), reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS);
                ReleaseDC(nullptr, dc);
                // Прозрачность: значки приходят с ней, миниатюры фото — без неё
                // (альфа вся 0). И бывает два вида прозрачности: «умноженная» и
                // обычная — если цвет ярче своей прозрачности, это обычная.
                // Раньше всё читалось как «умноженная», и края значков светились
                // (зелёная кайма у ZIP, красная у PDF).
                bool alpha = false, straight = false;
                for (int y = 0; y < img.height(); ++y) {
                    const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                    for (int x = 0; x < img.width(); ++x) {
                        const int a = qAlpha(line[x]);
                        if (a != 0) alpha = true;
                        if (qRed(line[x]) > a || qGreen(line[x]) > a || qBlue(line[x]) > a) straight = true;
                    }
                }
                if (alpha)
                    img.reinterpretAsFormat(straight ? QImage::Format_ARGB32 : QImage::Format_ARGB32_Premultiplied);
            }
            DeleteObject(hBmp);
        }
        factory->Release();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return img;
}

// Карточка видео: строка с названием сверху (nameH = 0 — без названия),
// ниже — кадр (играет прямо тут)
static Media::VideoSurface *setupVideoCard(QLabel *card, const QString &name, const QString &filepath,
                                           const QString &nameCss, int nameH,
                                           std::function<void(Media::VideoSurface *)> onExpand)
{
    if (nameH > 0) {
        QLabel *nameLabel = new QLabel(QString("🎬  %1").arg(name), card);
        nameLabel->setObjectName("videoName");
        nameLabel->setStyleSheet(nameCss);
        nameLabel->setFixedWidth(card->width());
        nameLabel->setFixedHeight(nameH);
        nameLabel->move(0, 0);
        nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    const QRect area(2, nameH + 2, card->width() - 4, card->height() - nameH - 4);
    Media::VideoSurface *surface = Media::attachVideo(card, filepath, area, onExpand);
    const qreal dpr = card->devicePixelRatioF();
    const QSize px = (QSizeF(area.size()) * dpr).toSize();
    QPointer<QLabel> c = card;
    Thumbs::runAsync(card, [filepath, px]() { return videoPosterWork(filepath, px); },
                     [c, surface, dpr](const QVariant &v) {
        if (!c) return;
        const QVariantList r = v.toList();
        QPixmap pm = QPixmap::fromImage(r.value(0).value<QImage>());
        pm.setDevicePixelRatio(dpr);
        surface->setPoster(pm);
        Media::setVideoDuration(c, r.value(1).toLongLong());
    });
    return surface;
}

// ─── Вспомогательные UI-компоненты ──────────────────────────────────────────

// Колонка с прокруткой, которой можно задать отступ сверху (под полосу папок
// в Закрепах): карточки при прокрутке уходят под отступ, а не под кнопки папок
class ColumnScroll : public QScrollArea
{
public:
    using QScrollArea::QScrollArea;
    using QAbstractScrollArea::setViewportMargins;
};

// applyCardShadow — тень под карточками-плитками (QGraphicsDropShadowEffect).
// blurRadius=40 даёт мягкую размытую тень, offset(0,8) смещает вниз.
static void applyCardShadow(QWidget *w)
{
    // Своя тень (см. CardShadow.h): размытие считается один раз, а не каждый
    // кадр — плавная прокрутка и «Дуга» не тормозят
    w->setGraphicsEffect(new CardShadowEffect(w));
}

// Кнопка-крестик: рисует ✕ через QPainter, прижатый к правому краю
class CloseBtn : public QAbstractButton {
public:
    explicit CloseBtn(QWidget *p) : QAbstractButton(p) {
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        bool h = underMouse();
        p.setPen(h ? QColor("#ff6666") : QColor(255, 255, 255, 150));
        QFont f = font(); f.setPixelSize(15); p.setFont(f);
        // ✕ прижат к правому краю: отступ 4px справа, вся высота
        p.drawText(rect().adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter, "✕");
    }
    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QAbstractButton::leaveEvent(e); }
};

// Диалог без OS-рамки: скруглённые углы через QPainter, своя шапка с drag + закрытие
class AppDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AppDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);

        // Шапка растянута на всю ширину (без отступов master-лейаута)
        // чтобы крестик был у самого правого края диалога
        auto *master = new QVBoxLayout(this);
        master->setContentsMargins(0, 0, 0, 0);
        master->setSpacing(0);

        m_titleBar = new QWidget(this);
        m_titleBar->setFixedHeight(36);
        m_titleBar->setStyleSheet("background: transparent;");
        auto *tl = new QHBoxLayout(m_titleBar);
        tl->setContentsMargins(14, 0, 2, 0);   // слева текст, справа минимум
        tl->setSpacing(0);
        m_titleLabel = new QLabel(m_titleBar);
        m_titleLabel->setStyleSheet("color: #e0e0e0; font-size: 13px; font-weight: bold; background: transparent;");
        tl->addWidget(m_titleLabel, 1);

        m_closeBtn = new CloseBtn(m_titleBar);
        connect(m_closeBtn, &QAbstractButton::clicked, this, &QDialog::reject);
        tl->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

        master->addWidget(m_titleBar);

        // Тело с внутренними отступами
        auto *bodyWrap = new QWidget(this);
        bodyWrap->setStyleSheet("background: transparent;");
        auto *bwl = new QVBoxLayout(bodyWrap);
        bwl->setContentsMargins(12, 2, 12, 12);
        bwl->setSpacing(0);
        m_body = new QWidget(bodyWrap);
        m_body->setStyleSheet("background: transparent;");
        bwl->addWidget(m_body, 1);
        master->addWidget(bodyWrap, 1);
    }

    void setWindowTitle(const QString &t) { QDialog::setWindowTitle(t); if (m_titleLabel) m_titleLabel->setText(t); }
    QWidget *body() { return m_body; }

protected:
    // Диалог «выпрыгивает» из центра и уходит обратно (см. Anim.h)
    void showEvent(QShowEvent *e) override
    {
        QDialog::showEvent(e);
        m_anim->onShow();
    }
    void done(int r) override
    {
        if (!m_anim->handleDone([self = QPointer<AppDialog>(this), r]() {
                if (self) self->QDialog::done(r);
            }))
            QDialog::done(r);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1), 12, 12);
        p.fillPath(path, QColor(18, 18, 30, 252));
        p.setPen(QPen(QColor(255,255,255,55), 2));
        p.drawPath(path);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (m_titleBar->geometry().contains(e->pos()) && e->button() == Qt::LeftButton)
            m_drag = e->globalPosition().toPoint() - frameGeometry().topLeft();
        else m_drag = {};
        QDialog::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!m_drag.isNull() && (e->buttons() & Qt::LeftButton))
            move(e->globalPosition().toPoint() - m_drag);
    }
    void mouseReleaseEvent(QMouseEvent *e) override { m_drag = {}; QDialog::mouseReleaseEvent(e); }

private:
    QWidget         *m_titleBar   = nullptr;
    QWidget         *m_body       = nullptr;
    QLabel          *m_titleLabel = nullptr;
    QAbstractButton *m_closeBtn   = nullptr;
    QPoint           m_drag;
    Anim::DialogAnimator *m_anim = new Anim::DialogAnimator(this);
};

// Ввод строки в нашем стиле — замена QInputDialog::getText
static QString askText(QWidget *parent,
                       const QString &title,
                       const QString &label,
                       const QString &initial,
                       bool *ok)
{
    AppDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(340);

    QVBoxLayout *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    QLabel *lbl = new QLabel(label, dlg.body());
    lbl->setStyleSheet("color: #c0c0d0; font-size: 13px; background: transparent;");

    QLineEdit *edit = new QLineEdit(initial, dlg.body());
    edit->setStyleSheet(
        "background: rgba(28,28,44,245); color: #f0f0f0;"
        "border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
        "padding: 5px 10px; font-size: 13px;"
        "selection-background-color: rgba(100,150,255,150);");
    edit->selectAll();

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    QPushButton *cancelBtn = new QPushButton(QObject::tr("Отмена"), dlg.body());
    QPushButton *okBtn     = new QPushButton(QObject::tr("ОК"),     dlg.body());
    okBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
        "border:1px solid rgba(120,160,255,160);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:70px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    cancelBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(255,255,255,200);"
        "border:1px solid rgba(255,255,255,55);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:70px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    okBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);

    vl->addWidget(lbl);
    vl->addWidget(edit);
    vl->addStretch();
    vl->addLayout(btnRow);

    QObject::connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(edit, &QLineEdit::returnPressed,  &dlg, &QDialog::accept);

    bool accepted = (dlg.exec() == QDialog::Accepted);
    if (ok) *ok = accepted;
    return accepted ? edit->text() : QString{};
}

// Диалог подтверждения в нашем стиле — замена QMessageBox::warning/critical
// danger=false → обычный; danger=true → красная иконка ⚠
static bool confirmDialog(QWidget *parent,
                          const QString &title,
                          const QString &message,
                          bool danger = false)
{
    AppDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(340);

    QVBoxLayout *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(14);

    // Строка с иконкой и текстом
    QHBoxLayout *msgRow = new QHBoxLayout();
    msgRow->setSpacing(12);
    if (danger) {
        QLabel *icon = new QLabel("⚠", dlg.body());
        icon->setStyleSheet("color: #ff6644; font-size: 24px; background: transparent;");
        icon->setFixedWidth(28);
        icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        msgRow->addWidget(icon);
    }
    QLabel *msgLbl = new QLabel(message, dlg.body());
    msgLbl->setWordWrap(true);
    msgLbl->setStyleSheet("color: #d0d0d0; font-size: 13px; background: transparent;");
    msgRow->addWidget(msgLbl, 1);
    vl->addLayout(msgRow);

    // Кнопки
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    QPushButton *cancelBtn = new QPushButton(QObject::tr("Отмена"), dlg.body());
    QPushButton *okBtn     = new QPushButton(QObject::tr("Да"),     dlg.body());
    okBtn->setStyleSheet(
        danger
        ? "QPushButton{background:rgba(100,20,20,220);color:#ffaaaa;"
          "border:1px solid rgba(200,60,60,160);border-radius:6px;"
          "padding:5px 18px;font-size:13px;min-width:60px;}"
          "QPushButton:hover{background:rgba(160,30,30,240);color:#fff;}"
        : "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
          "border:1px solid rgba(120,160,255,160);border-radius:6px;"
          "padding:5px 18px;font-size:13px;min-width:60px;}"
          "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    cancelBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(255,255,255,200);"
        "border:1px solid rgba(255,255,255,55);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:60px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    okBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    vl->addLayout(btnRow);

    QObject::connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    return dlg.exec() == QDialog::Accepted;
}

// Длительность аудио/видео из свойств файла (как в Проводнике), мс; 0 — нет
static qint64 mediaDurationMs(const QString &path)
{
    qint64 ms = 0;
    IShellItem2 *item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(QDir::toNativeSeparators(path).toStdWString().c_str(),
                                              nullptr, IID_PPV_ARGS(&item))) && item) {
        static const PROPERTYKEY kDuration = { { 0x64440490, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } }, 3 };
        ULONGLONG d = 0;
        if (SUCCEEDED(item->GetUInt64(kDuration, &d))) ms = qint64(d / 10000);
        item->Release();
    }
    return ms;
}

// «Свойства» файла из полоски: имя, где лежит, тип, размер, даты и т.п.
static void fileInfoDialog(QWidget *parent, const QString &path)
{
    const QFileInfo fi(path);
    AppDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Свойства"));
    dlg.setMinimumWidth(420);
    auto *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(14);
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(8);
    int row = 0;
    auto add = [&](const QString &key, const QString &value) {
        auto *k = new QLabel(key, dlg.body());
        k->setStyleSheet("color: rgba(255,255,255,120); font-size: 12px; background: transparent;");
        k->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        auto *v = new QLabel(value, dlg.body());
        v->setStyleSheet("color: #e8e8f0; font-size: 12px; background: transparent;");
        v->setWordWrap(true);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(k, row, 0);
        grid->addWidget(v, row, 1);
        ++row;
    };
    const QString cat = Files::category(path);
    const QString kind = cat == "image"    ? QObject::tr("Картинка")
                       : cat == "video"    ? QObject::tr("Видео")
                       : cat == "audio"    ? QObject::tr("Аудио")
                       : cat == "docs"     ? QObject::tr("Документ")
                       : cat == "archives" ? QObject::tr("Архив")
                       : cat == "programs" ? QObject::tr("Программа")
                                           : QObject::tr("Файл");
    const QLocale loc;
    add(QObject::tr("Имя"), fi.fileName());
    add(QObject::tr("Папка"), QDir::toNativeSeparators(fi.absolutePath()));
    if (!fi.exists()) {
        add(QObject::tr("Состояние"), QObject::tr("Файл не найден — его переместили или удалили"));
    } else {
        add(QObject::tr("Тип"), fi.suffix().isEmpty() ? kind : kind + "  (." + fi.suffix().toUpper() + ")");
        add(QObject::tr("Размер"), QObject::tr("%1  (%2 байт)").arg(Files::sizeText(fi.size()),
                                                                   loc.toString(fi.size())));
        if (cat == "image") {
            const QSize sz = QImageReader(path).size();
            if (sz.isValid()) add(QObject::tr("Разрешение"), QString("%1 × %2").arg(sz.width()).arg(sz.height()));
        }
        if (cat == "audio" || cat == "video") {
            const qint64 ms = mediaDurationMs(path);
            if (ms > 0) add(QObject::tr("Длительность"), Media::timeText(ms));
        }
        if (fi.birthTime().isValid())
            add(QObject::tr("Создан"), loc.toString(fi.birthTime(), QLocale::ShortFormat));
        add(QObject::tr("Изменён"), loc.toString(fi.lastModified(), QLocale::ShortFormat));
    }
    grid->setColumnStretch(1, 1);
    vl->addLayout(grid);

    auto *btnRow = new QHBoxLayout();
    auto *okBtn = new QPushButton(QObject::tr("Закрыть"), dlg.body());
    okBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
        "border:1px solid rgba(120,160,255,160);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:60px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    okBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addStretch();
    btnRow->addWidget(okBtn);
    vl->addLayout(btnRow);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

// Выбор элемента из списка в нашем стиле — замена QInputDialog::getItem
static QString askItem(QWidget *parent,
                       const QString &title,
                       const QString &label,
                       const QStringList &items,
                       int current,
                       bool *ok)
{
    AppDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(320);

    QVBoxLayout *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    QLabel *lbl = new QLabel(label, dlg.body());
    lbl->setStyleSheet("color: #c0c0d0; font-size: 13px; background: transparent;");

    QComboBox *combo = new QComboBox(dlg.body());
    combo->addItems(items);
    combo->setCurrentIndex(qBound(0, current, items.size() - 1));
    combo->setStyleSheet(
        "QComboBox { background: rgba(28,28,44,245); color: #f0f0f0;"
        "  border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
        "  padding: 5px 10px; font-size: 13px; }"
        "QComboBox:hover { border: 1px solid rgba(255,255,255,90); }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow { image: none; width: 0; }"
        "QComboBox QAbstractItemView {"
        "  background: rgba(22,22,36,252); color: #e0e0e0;"
        "  border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
        "  selection-background-color: rgba(255,255,255,18);"
        "  outline: none; padding: 2px; }");

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    QPushButton *cancelBtn = new QPushButton(QObject::tr("Отмена"), dlg.body());
    QPushButton *okBtn     = new QPushButton(QObject::tr("ОК"),     dlg.body());
    okBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
        "border:1px solid rgba(120,160,255,160);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:60px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    cancelBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(255,255,255,200);"
        "border:1px solid rgba(255,255,255,55);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:60px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    okBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);

    vl->addWidget(lbl);
    vl->addWidget(combo);
    vl->addStretch();
    vl->addLayout(btnRow);

    QObject::connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    bool accepted = (dlg.exec() == QDialog::Accepted);
    if (ok) *ok = accepted;
    return accepted ? combo->currentText() : QString{};
}

// Строка поиска: контейнер рисует скруглённый фон через QPainter (как SmartButton),
// внутри прозрачный QLineEdit без рамки — никаких артефактов на углах
class SearchBar : public QWidget
{
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAutoFillBackground(false);
        m_edit = new QLineEdit(this);
        m_edit->setFrame(false);
        m_edit->setStyleSheet(
            "background: transparent; border: none; "
            "color: #f0f0f0; font-size: 13px; "
            "selection-background-color: rgba(100,150,255,150);"
        );
        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 0, 12, 0);
        lay->addWidget(m_edit);
        // Перерисовка при смене фокуса (рамка меняет яркость)
        connect(m_edit, &QLineEdit::textChanged, this, [this](){ update(); });
        m_edit->installEventFilter(this);
    }
    QLineEdit *edit() { return m_edit; }

    bool eventFilter(QObject *o, QEvent *e) override {
        if (o == m_edit && (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut))
            update();
        return QWidget::eventFilter(o, e);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        bool focused = m_edit->hasFocus();
        QColor border = focused ? QColor(255,255,255,90) : QColor(255,255,255,55);
        QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        QPainterPath path;
        path.addRoundedRect(r, 8, 8);
        p.fillPath(path, QColor(20, 20, 32, 245));
        p.setPen(QPen(border, 2));
        p.drawPath(path);
    }
private:
    QLineEdit *m_edit;
};

// QMenu с настоящими скруглёнными углами через QPainter — без артефактов
// Фон рисуем сами, QMenu рисует пункты поверх прозрачного фона
class RoundedMenu : public QMenu
{
public:
    explicit RoundedMenu(QWidget *parent = nullptr) : QMenu(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        // Делаем фон QMenu прозрачным — рисуем его сами в paintEvent
        setStyleSheet(
            "QMenu { background: transparent; border: none; padding: 4px 0; }"
            "QMenu::item { color: #e0e0e0; padding: 6px 20px; border-radius: 5px; margin: 1px 4px; font-size: 13px; }"
            "QMenu::item:selected { background: rgba(255,255,255,18); color: #ffffff; border: 1px solid rgba(255,255,255,35); }"
            "QMenu::item:disabled { color: rgba(255,255,255,55); }"
            "QMenu::separator { height: 1px; background: rgba(255,255,255,18); margin: 3px 8px; }"
        );
    }
protected:
    // Меню разворачивается сверху вниз, как шторка (см. Anim.h)
    void showEvent(QShowEvent *e) override
    {
        QMenu::showEvent(e);
        Anim::menuIn(this);
    }

    void paintEvent(QPaintEvent *e) override
    {
        // 1. Рисуем скруглённый фон через QPainter (без артефактов)
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
        p.fillPath(path, QColor(18, 18, 30, 250));
        p.setPen(QPen(QColor(255, 255, 255, 55), 2));
        p.drawPath(path);
        p.end();
        // 2. QMenu рисует пункты поверх (фон у него прозрачный)
        QMenu::paintEvent(e);
    }
};

// ─── ViewPill — «пилюля» активной вкладки (История / Закрепы) ─────────────────
// Светящаяся рамка поверх активной кнопки. При переключении вкладки плавно
// переезжает на другую кнопку с пружинкой (вид «Вкладки и разделы» в Настройки → Оформление).
// Клики проходят сквозь неё к кнопкам.
class ViewPill : public QWidget
{
public:
    explicit ViewPill(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_anim.setTargetObject(this);
        m_anim.setPropertyName("geometry");
    }

    // Переместить на кнопку btn (с анимацией, если разрешено)
    void moveTo(QWidget *btn, bool animate)
    {
        const QRect to = btn->geometry();
        if (to.isEmpty()) return;
        show();
        raise();
        m_anim.stop();
        if (!animate || !Anim::enabled(Anim::Kind::Tabs) || geometry().isEmpty()) {
            setGeometry(to);
            return;
        }
        QEasingCurve spring(QEasingCurve::OutBack);
        spring.setOvershoot(1.3);
        m_anim.setEasingCurve(spring);
        m_anim.setDuration(Anim::ms(Anim::Kind::Tabs, 340));
        m_anim.setStartValue(geometry());
        m_anim.setEndValue(to);
        m_anim.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
        p.fillPath(path, QColor(160, 200, 255, 22));
        p.setPen(QPen(QColor(160, 200, 255, 170), 1.5));
        p.drawPath(path);
    }

private:
    QPropertyAnimation m_anim;
};

// Тень для кнопок и строки поиска — те же параметры что у карточек
static void applyBtnShadow(QWidget *w)
{
    auto *fx = new QGraphicsDropShadowEffect(w);
    fx->setBlurRadius(40);
    fx->setOffset(0, 8);
    fx->setColor(QColor(0, 0, 0, 220));
    w->setGraphicsEffect(fx);
}

// ─── Нечёткий поиск (Fuzzy Search) ──────────────────────────────────────────
//
// Как работает поиск в SmartClip:
//   Пользователь пишет запрос из нескольких слов.
//   Для каждой карточки проверяем: КАЖДОЕ слово запроса должно найти совпадение
//   хотя бы в одном слове текста карточки.
//
//   Пример: запрос "прив мир" → находит "Привет, мир!" потому что:
//     "прив" → subsequence в слове "привет"  ✓
//     "мир"  → точное вхождение в слово "мир" ✓
//
// Алгоритмы используемые в поиске:
//   1. levenshtein() — редакционное расстояние (количество операций вставки/
//      удаления/замены для превращения строки a в строку b).
//      Пример: levenshtein("клип", "клипс") = 1 (одна вставка 'с')
//   2. isSubsequence() — все буквы needle встречаются в haystack по порядку.
//      Пример: isSubsequence("кл", "клипборд") = true (к...л присутствуют)
//   3. wordFuzzyMatch() — применяет все стратегии к паре слов
//   4. fuzzyMatch() — объединяет результаты для всего запроса и текста карточки

// Расстояние Левенштейна между двумя строками.
// Оптимизация: две строки вместо полной матрицы (O(n) памяти вместо O(m*n)).
static int levenshtein(const QString &a, const QString &b)
{
    int m = a.size(), n = b.size();
    QVector<int> prev(n + 1), curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;
    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            if (a[i-1] == b[j-1])
                curr[j] = prev[j-1];
            else
                curr[j] = 1 + std::min({prev[j], curr[j-1], prev[j-1]});
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

// Subsequence: все буквы needle встречаются в haystack по порядку (не обязательно подряд).
// Пример: "ктс" — subsequence "короткое" (к..т..с — все три присутствуют по порядку).
static bool isSubsequence(const QString &needle, const QString &haystack)
{
    int ni = 0;
    for (int hi = 0; hi < haystack.size() && ni < needle.size(); ++hi)
        if (haystack[hi] == needle[ni]) ++ni;
    return ni == needle.size();
}

// Нечёткое совпадение слова запроса qw с одним словом текста tw.
// qw  — слово из поисковой строки (что пользователь написал)
// tw  — слово из текста карточки (где ищем)
// maxDist — максимально допустимое расстояние Левенштейна:
//   слово ≥8 символов → 3 ошибки допускается
//   слово ≥5 → 2 ошибки, слово ≥3 → 1 ошибка, меньше → только точное совпадение
static bool wordFuzzyMatch(const QString &qw, const QString &tw, int maxDist)
{
    // 1. Точное вхождение qw в tw (contains учитывает подстроки)
    if (tw.contains(qw)) return true;

    // 2. Subsequence: буквы qw встречаются в tw по порядку
    if (isSubsequence(qw, tw)) return true;

    // 3. Levenshtein на полных словах (если длины близки)
    if (std::abs(tw.size() - qw.size()) <= maxDist + 1 &&
        levenshtein(qw, tw) <= maxDist) return true;

    // 4. Prefix fuzzy: сравниваем qw с началом tw только если tw заметно длиннее
    //    (пользователь ввёл начало длинного слова, как "клипб" → "клипборд").
    //    Если слова одинаковой длины — prefix не нужен, иначе ложные совпадения.
    if (tw.size() > qw.size() + 1) {
        int lo = std::max(2, (int)qw.size() - 1);
        int hi = std::min((int)qw.size() + 1, (int)tw.size());
        for (int plen = lo; plen <= hi; ++plen) {
            if (levenshtein(qw, tw.left(plen)) <= maxDist) return true;
        }
    }

    return false;
}

// fuzzyMatch — умный нечёткий поиск запроса в тексте карточки.
// Каждое слово запроса (≥2 букв) должно найти совпадение хотя бы в одном слове текста.
// Допуск ошибок по длине слова:
//   2 букв:   0 (только точно)
//   3–4 букв: 1 ошибка
//   5–7 букв: 2 ошибки
//   8+ букв:  3 ошибки
static bool fuzzyMatch(const QString &text, const QString &query)
{
    if (query.isEmpty()) return true;

    const QString tl = text.toLower();
    const QString ql = query.toLower();

    // Быстрый путь: точное вхождение всего запроса
    if (tl.contains(ql)) return true;

    // Разбиваем текст на слова по пробелам и знакам препинания
    const QStringList queryWords = ql.split(' ', Qt::SkipEmptyParts);
    const QStringList textWords  = tl.split(QRegularExpression("[\\s\\-_/\\\\.,;:]+"),
                                             Qt::SkipEmptyParts);

    for (const QString &qw : queryWords) {
        if (qw.size() <= 1) continue; // одиночные символы пропускаем

        int maxDist;
        if      (qw.size() >= 8) maxDist = 3;
        else if (qw.size() >= 5) maxDist = 2;
        else if (qw.size() >= 3) maxDist = 1;
        else                     maxDist = 0;

        bool found = false;
        for (const QString &tw : textWords) {
            if (wordFuzzyMatch(qw, tw, maxDist)) { found = true; break; }
        }
        if (!found) return false;
    }
    return !queryWords.isEmpty();
}

// ─── Подсветка совпадений поиска ─────────────────────────────────────────────
// Ищет слова запроса в plain-тексте карточки и рисует жёлтый <span>.
// При пустом query восстанавливает обычный plain text.
// Использует displayText (обрезанный текст), не fullText.
static void applyHighlight(QLabel *card, const QString &query)
{
    QString plain = card->property("displayText").toString();
    if (plain.isEmpty()) return;

    if (query.isEmpty()) {
        card->setTextFormat(Qt::PlainText);
        card->setText(plain);
        return;
    }

    // Собираем позиции прямых совпадений каждого слова запроса в тексте
    QString lower = plain.toLower();
    QStringList queryWords = query.toLower().split(' ', Qt::SkipEmptyParts);
    QList<QPair<int,int>> hits; // {start, length}

    for (const QString &qw : queryWords) {
        if (qw.size() < 2) continue;
        int pos = 0, idx;
        while ((idx = lower.indexOf(qw, pos)) != -1) {
            hits.append({idx, (int)qw.size()});
            pos = idx + qw.size();
        }
    }

    if (hits.isEmpty()) {
        card->setTextFormat(Qt::PlainText);
        card->setText(plain);
        return;
    }

    // Сортируем и мерджим пересекающиеся интервалы (чтобы не рисовать <span> внутри <span>)
    std::sort(hits.begin(), hits.end());
    QList<QPair<int,int>> merged;
    for (auto &h : hits) {
        if (!merged.isEmpty() && h.first < merged.last().first + merged.last().second)
            merged.last().second = std::max(merged.last().second,
                                            h.first + h.second - merged.last().first);
        else
            merged.append(h);
    }

    // Строим HTML: \n → <br>, спецсимволы экранируем через toHtmlEscaped()
    QString html;
    int pos = 0;
    for (auto &[start, len] : merged) {
        html += plain.mid(pos, start - pos).toHtmlEscaped().replace('\n', "<br>");
        html += "<span style='background:#e8b800;color:#111;border-radius:2px;"
                "padding:0 1px;'>";
        html += plain.mid(start, len).toHtmlEscaped();
        html += "</span>";
        pos = start + len;
    }
    html += plain.mid(pos).toHtmlEscaped().replace('\n', "<br>");

    card->setTextFormat(Qt::RichText);
    card->setText(html);
}

// ─── clearLayout ─────────────────────────────────────────────────────────────
// Убрать все виджеты из колонки/полосы. deleteLater, а не delete: этот код часто
// вызывается из обработчика клика или меню САМОЙ удаляемой карточки/кнопки —
// мгновенное удаление = вылет. keepLast — сколько элементов оставить в конце
// (например, растяжку в списке тегов).
static void clearLayout(QLayout *layout, int keepLast = 0)
{
    while (layout->count() > keepLast) {
        QLayoutItem *item = layout->takeAt(0);
        if (QWidget *w = item->widget()) { w->hide(); w->deleteLater(); }
        delete item;
    }
}

// ─── softWrap ────────────────────────────────────────────────────────────────
// Вставляет мягкие переносы (\n) внутри длинных «слов» без пробелов.
// Ломает ТОЛЬКО после естественных разделителей: / \ _ - . : чтобы не резать посередине токена.
// Пример: "src/ui/MainWindow.cpp" → "src/ui/\nMainWindow.cpp" (если длина > threshold).
// Нужно чтобы ClickableCard не растягивался из-за длинных путей/URL.
static QString softWrap(const QString &text, int threshold = 28)
{
    static const QString breakAfter = QStringLiteral("\\/._-:,;@");
    QString result;
    result.reserve(text.size() + 16);
    int segLen = 0;

    for (int i = 0; i < text.size(); ++i) {
        QChar c = text[i];
        result += c;

        if (c == ' ' || c == '\n' || c == '\t') {
            segLen = 0;
        } else {
            ++segLen;
            // Если сегмент уже длинный и текущий символ — хорошее место для разрыва
            if (segLen >= threshold && breakAfter.contains(c)) {
                result += '\n';
                segLen = 0;
            }
        }
    }
    return result;
}

// ─── Конструктор MainWindow ───────────────────────────────────────────────────
// Создаёт главное окно SmartClip.
//
// Флаги окна:
//   FramelessWindowHint — нет системного заголовка/рамки (рисуем сами)
//   WindowStaysOnTopHint — окно всегда поверх всех других окон
//   Qt::Tool — не появляется в таскбаре (только в трее)
//   WA_TranslucentBackground — фон окна прозрачный (видно рабочий стол)
//
// Параметры конструктора:
//   db     — указатель на базу данных SQLite (создана в main.cpp)
//   parent — родительский виджет (nullptr — окно без родителя)
MainWindow::MainWindow(Database *db, QWidget *parent)
    : QWidget(parent)
    , m_db(db)
    , m_leftLayout(nullptr)
    , m_rightLayout(nullptr)
    , m_leftScroll(nullptr)
    , m_rightScroll(nullptr)
    , m_topPanel(nullptr)
    , m_bottomPanel(nullptr)
    , m_centerSpacer(nullptr)
    , m_searchEdit(nullptr)
    , m_scale(AppSettings::get().uiScale())
{
    setObjectName("mainWindow");
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);  // Не отбираем фокус при показе
    setupLayout();

    m_imageViewer = new ImageViewer(this);
    m_videoViewer = new VideoViewer(this);
    // Файл не играет (формат, который Windows не умеет) — предложить внешний плеер
    Media::Playback::get().onUnsupported = [this](const QString &file) {
        if (confirmDialog(this, tr("Воспроизведение"),
                          tr("SmartClip не может воспроизвести этот файл.\nОткрыть в стандартном плеере?"), false))
            QDesktopServices::openUrl(QUrl::fromLocalFile(file));
    };

    // Клик мимо (в другую программу или на рабочий стол), пока поверх SmartClip
    // открыто окно (настройки, диалог) или меню: закрываем всё — окно, меню и
    // сам SmartClip, каждое со своей анимацией. Раньше в этом случае ничего
    // не закрывалось: «уходило» неактивным окно настроек, а не главное.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState st) {
        if (st != Qt::ApplicationInactive || !isVisible()) return;
        // Проверяем на следующем шаге, когда активное окно уже сменилось
        QTimer::singleShot(0, this, [this]() { closeAllOnDeactivate(); });
    });

    // Подготовить окно заранее, пока оно скрыто (см. prewarm)
    QTimer::singleShot(1500, this, &MainWindow::prewarm);
}

// Первое открытие окна было долгим (~0,3 с): Qt один раз готовит оформление
// всех кнопок и карточек (стили, раскладку). Делаем это заранее, вскоре после
// запуска, пока окно скрыто, — и первое открытие такое же быстрое, как следующие.
void MainWindow::prewarm()
{
    if (isVisible()) return;
    if (m_viewMode == ViewMode::History && m_historyDirty) {
        refreshHistory();
        m_historyDirty = false;
    }
    ensurePolished();  // Стили — у окна и всех его элементов
    if (layout()) layout()->activate();
}


void MainWindow::setupLayout()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── Верхняя панель ────────────────────────────────────────────────────────
    // Содержит кнопки переключения вида (История/Закрепы) и управляющие кнопки.
    // Высота фиксирована через sc(BASE_TOP) — масштабируется вместе с UI.
    m_topPanel = new QWidget(this);
    m_topPanel->setObjectName("topPanel");
    m_topPanel->setFixedHeight(sc(BASE_TOP));
    QHBoxLayout *topLayout = new QHBoxLayout(m_topPanel);
    topLayout->setContentsMargins(12, 8, 12, 18);
    topLayout->setSpacing(8);

    // Группа «переключатели вида» — слева, одинаковая ширина по самому длинному
    const int BTN_H = 32;
    QFont btnFont = font();
    btnFont.setPixelSize(13);

    m_historyBtn = new SmartButton(tr("История"), m_topPanel);
    m_historyBtn->setFixedHeight(BTN_H);
    m_historyBtn->setFont(btnFont);
    m_historyBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,150), 8);
    m_historyBtn->setActiveBtnStyle(QColor(40,40,58,245), Qt::white);
    m_historyBtn->setActiveState(true);
    applyBtnShadow(m_historyBtn);

    m_pinsBtn = new SmartButton(tr("⭐ Закрепы"), m_topPanel);
    m_pinsBtn->setFixedHeight(BTN_H);
    m_pinsBtn->setFont(btnFont);
    m_pinsBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,150), 8);
    m_pinsBtn->setActiveBtnStyle(QColor(40,40,58,245), Qt::white);
    m_pinsBtn->setActiveState(false);
    applyBtnShadow(m_pinsBtn);

    connect(m_historyBtn, &QPushButton::clicked, this, [this]() { switchView(ViewMode::History); });
    connect(m_pinsBtn,    &QPushButton::clicked, this, [this]() { switchView(ViewMode::Pins);    });

    // Группа «правые действия»
    m_profilesBtn = new SmartButton(tr("📋  Профили"), m_topPanel);
    m_profilesBtn->setFixedHeight(BTN_H);
    m_profilesBtn->setFont(btnFont);
    m_profilesBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,160), 6);
    applyBtnShadow(m_profilesBtn);
    connect(m_profilesBtn, &QPushButton::clicked, this, &MainWindow::showProfilesMenu);

    m_sortBtn = new SmartButton(sortLabel(), m_topPanel);
    m_sortBtn->setFixedHeight(BTN_H);
    m_sortBtn->setFont(btnFont);
    m_sortBtn->setMinimumWidth(145);
    m_sortBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,160), 6);
    applyBtnShadow(m_sortBtn);
    connect(m_sortBtn, &QPushButton::clicked, this, &MainWindow::showSortMenu);

    m_clearBtn = new SmartButton(tr("✕  Очистить всё"), m_topPanel);
    m_clearBtn->setFixedHeight(BTN_H);
    m_clearBtn->setFont(btnFont);
    m_clearBtn->setBtnStyle(QColor(55,12,12,245), QColor(255,130,130,220), 6);
    applyBtnShadow(m_clearBtn);
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::showClearMenu);

    // Иконка настроек — квадратная
    m_settingsBtn = new SmartButton(QString(), m_topPanel);
    m_settingsBtn->setIconGlyph(QChar(0xE713));  // Шестерёнка
    m_settingsBtn->setFixedSize(BTN_H, BTN_H);
    m_settingsBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,160), 6);
    m_settingsBtn->setToolTip(tr("Настройки"));
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    applyBtnShadow(m_settingsBtn);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::showSettings);

    // Кнопка режима мультивыбора — видна только в Закрепах
    m_editBtn = new SmartButton(QString(), m_topPanel);
    m_editBtn->setIconGlyph(QChar(0xE70F));  // Карандаш
    m_editBtn->setFixedSize(BTN_H, BTN_H);
    m_editBtn->setFont(btnFont);
    m_editBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,150), 8);
    m_editBtn->setActiveBtnStyle(QColor(80,60,10,245), QColor(255,220,80));
    m_editBtn->setActiveState(false);
    m_editBtn->setToolTip(tr("Режим выбора нескольких закрепов"));
    m_editBtn->setCursor(Qt::PointingHandCursor);
    m_editBtn->setVisible(false);
    applyBtnShadow(m_editBtn);
    connect(m_editBtn, &QPushButton::clicked, this, &MainWindow::toggleEditMode);

    topLayout->addWidget(m_historyBtn);
    topLayout->addWidget(m_pinsBtn);

    // «Пилюля» активной вкладки — переезжает между История и Закрепы.
    // Следим за кнопками: если они сдвинулись/изменили размер (раскладка,
    // масштаб) — ставим «пилюлю» на место без анимации.
    m_viewPill = new ViewPill(m_topPanel);
    m_historyBtn->installEventFilter(this);
    m_pinsBtn->installEventFilter(this);
    topLayout->addStretch();
    topLayout->addWidget(m_editBtn);
    topLayout->addWidget(m_profilesBtn);
    topLayout->addWidget(m_sortBtn);
    topLayout->addWidget(m_clearBtn);
    topLayout->addWidget(m_settingsBtn);

    // ── Полоса папок (показывается только в режиме закрепов) ──────────────────
    m_foldersBar = new QWidget(this);
    m_foldersBar->setObjectName("foldersBar");
    m_foldersBar->setFixedHeight(sc(BASE_FOLD));
    m_foldersLayout = new QHBoxLayout(m_foldersBar);
    m_foldersLayout->setContentsMargins(12, 6, 12, 6);
    m_foldersLayout->setSpacing(6);
    m_foldersLayout->setAlignment(Qt::AlignVCenter);
    m_foldersLayout->addStretch();
    m_foldersBar->hide(); // скрыта пока не перешли в режим закрепов

    // ── Средняя зона ──────────────────────────────────────────────────────────
    // Три части: левая колонка (тексты) + прозрачный центр + правая колонка (картинки).
    // Прозрачный центр (m_centerSpacer) пропускает клики насквозь к рабочему столу
    // через nativeEvent WM_NCHITTEST → HTTRANSPARENT.
    QHBoxLayout *middleLayout = new QHBoxLayout();
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);

    // Левая колонка — тексты и аудио
    m_leftScroll = new ColumnScroll(this);
    m_leftScroll->setObjectName("leftScroll");
    // Шире на запас к центру: выдвинутая мышью карточка не должна обрезаться краем колонки
    m_leftScroll->setFixedWidth(sc(BASE_LEFT) + sc(ARC_AMP));
    m_leftScroll->setWidgetResizable(true);
    m_leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Полосу прокрутки не показываем (листать — колёсиком/тачпадом): она
    // ломалась во время анимаций и только занимала место
    m_leftScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_leftScroll->setFocusPolicy(Qt::WheelFocus);
    m_leftScroll->viewport()->setStyleSheet("background: transparent;");
    QScrollArea *leftScroll = m_leftScroll;
    QWidget *leftContent = new QWidget();
    leftContent->setStyleSheet("background-color: transparent;");
    auto *leftArcLayout = new ArcColumnLayout(leftContent);
    m_leftLayout = leftArcLayout;
    m_leftLayout->setAlignment(Qt::AlignTop);
    // Справа — запас под выдвинутую мышью карточку (она едет к центру экрана)
    m_leftLayout->setContentsMargins(14, 8, 14 + sc(ARC_AMP), 24); // увеличено для тени (blurRadius=40)
    m_leftLayout->setSpacing(12);
    leftScroll->setWidget(leftContent);

    // Прозрачная середина — не перехватывает клики, нажатия проваливаются к рабочему столу
    m_centerSpacer = new QWidget(this);
    m_centerSpacer->setStyleSheet("background-color: transparent;");
    m_centerSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QWidget *centerSpacer = m_centerSpacer;

    // Правая колонка — картинки и видео
    m_rightScroll = new QScrollArea(this);
    m_rightScroll->setObjectName("rightScroll");
    m_rightScroll->setFixedWidth(sc(BASE_RIGHT) + sc(ARC_AMP));
    m_rightScroll->setWidgetResizable(true);
    m_rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Полосу прокрутки не показываем (листать — колёсиком/тачпадом): она
    // ломалась во время анимаций и только занимала место
    m_rightScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_rightScroll->setFocusPolicy(Qt::WheelFocus);
    m_rightScroll->viewport()->setStyleSheet("background: transparent;");
    QScrollArea *rightScroll = m_rightScroll;
    QWidget *rightContent = new QWidget();
    rightContent->setStyleSheet("background-color: transparent;");
    auto *rightArcLayout = new ArcColumnLayout(rightContent);
    m_rightLayout = rightArcLayout;
    m_rightLayout->setAlignment(Qt::AlignTop);
    // Слева — запас под выдвинутую мышью карточку
    m_rightLayout->setContentsMargins(14 + sc(ARC_AMP), 8, 14, 24); // увеличено для тени (blurRadius=40)
    m_rightLayout->setSpacing(12);
    rightScroll->setWidget(rightContent);

    // Плавная прокрутка с инерцией и мягким краем (без анимаций Windows — обычная)
    m_leftScroller  = new SmoothScroller(m_leftScroll);
    m_rightScroller = new SmoothScroller(m_rightScroll);

    // «Дуга»: крайние карточки уходят по дуге наружу, под мышью — выдвигаются к центру
    // Тексты разной высоты уходят «по доле за краем», фото — ровной дугой
    m_leftArc  = new CardArc(m_leftScroll,  leftArcLayout,  +1, sc(90),  sc(12), true);
    m_rightArc = new CardArc(m_rightScroll, rightArcLayout, -1, sc(110), sc(12), false);

    // Кнопка «наверх» — вверху колонки по центру карточек, когда пролистали ~10 карточек
    new ScrollTopButton(m_leftScroll, m_leftLayout, 14 + (sc(BASE_LEFT) - 36) / 2, sc(38),
                        [this]() { m_leftScroller->scrollTo(0); });
    new ScrollTopButton(m_rightScroll, m_rightLayout, 14 + sc(ARC_AMP) + (sc(BASE_RIGHT) - 36) / 2,
                        sc(38), [this]() { m_rightScroller->scrollTo(0); });

    // Пустое место под карточками — бледные «фантомные» карточки до низа экрана
    new GhostCards(m_leftScroll,  m_leftLayout,  false, m_scale);
    new GhostCards(m_rightScroll, m_rightLayout, true,  m_scale);

    // Шкала дат сбоку колонки — проявляется, пока листаешь
    // (зажать у линии шкалы и вести — быстрая перемотка по всей колонке)
    m_leftRuler  = new DateRuler(m_leftScroll,  m_leftLayout,  true,  sc(60),
                                 [this](int v) { m_leftScroller->jumpTo(v); });
    m_rightRuler = new DateRuler(m_rightScroll, m_rightLayout, false, sc(60),
                                 [this](int v) { m_rightScroller->jumpTo(v); });

    // Долистали почти до конца колонки — подгружаем следующую порцию истории.
    // rangeChanged — на случай, когда первая порция не заполнила колонку целиком.
    for (QScrollArea *area : {m_leftScroll, m_rightScroll}) {
        connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this,
                [this, area]() { maybeLoadMore(area); });
        connect(area->verticalScrollBar(), &QScrollBar::rangeChanged, this,
                [this, area]() { QTimer::singleShot(0, this, [this, area]() { maybeLoadMore(area); }); });
    }

    middleLayout->addWidget(leftScroll);
    middleLayout->addWidget(centerSpacer);
    middleLayout->addWidget(rightScroll);

    // ── Нижняя панель ───────────────────────────────────────────────────────────
    m_bottomPanel = new QWidget(this);
    m_bottomPanel->setObjectName("bottomPanel");
    m_bottomPanel->setFixedHeight(sc(BASE_BOT));
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomPanel);
    bottomLayout->setContentsMargins(12, 6, 12, 14);
    auto *searchBar = new SearchBar(m_bottomPanel);
    applyBtnShadow(searchBar);
    m_searchEdit = searchBar->edit();
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->installEventFilter(this);  // Клик — забрать фокус (окно без активации)
    m_searchEdit->setPlaceholderText(tr("Поиск по тексту..."));
    bottomLayout->addWidget(searchBar);

    // Поиск по истории идёт по ВСЕЙ базе (раньше — только по 40 карточкам на экране,
    // и всё скопированное раньше просто не находилось). Чтобы не тормозить на
    // каждой букве, ищем через 200мс после последнего нажатия.
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(200);
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        const QString q = m_searchEdit->text();
        if (m_viewMode == ViewMode::History) {
            // Перестраиваем ТОЛЬКО колонку текстов — картинки справа не трогаем
            // (иначе на каждую паузу в наборе заново грузились бы 40 картинок)
            loadHistory(true);
            filterHistory(q);
        } else {
            // В режиме закрепов: если есть запрос — загружаем ВСЕ папки и фильтруем;
            // если запрос пуст — возвращаемся к текущей папке
            loadPins(!q.isEmpty());
            filterPins(q);
        }
    });

    // При вводе — мгновенно фильтруем то, что уже на экране,
    // а полную перезагрузку делаем через 200мс после последней буквы
    // (раньше закрепы перестраивались целиком на КАЖДУЮ букву).
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &q) {
        if (m_filesStrip) m_filesStrip->setFilter(q);  // Файлы внизу — тоже по имени
        if (m_viewMode == ViewMode::History)
            filterHistory(q);
        else
            filterPins(q);
        m_searchTimer->start();
    });

    mainLayout->addWidget(m_topPanel);
    // Полоса папок — не на всю ширину, а только над левой колонкой (до правой):
    // правая колонка в Закрепах начинается так же высоко, как в Истории.
    // Левой колонке в Закрепах даём сверху отступ под полосу (см. switchView).
    m_foldersBar->raise();
    m_rightScroll->installEventFilter(this);
    m_topPanel->installEventFilter(this);
    mainLayout->addLayout(middleLayout);
    mainLayout->addWidget(m_bottomPanel);

    // ── Свежие файлы — внизу по центру, над поиском (и в Истории, и в Закрепах)
    m_filesStrip = new FilesStrip(this, m_scale, &fileThumbWork);
    m_filesStrip->onPick = [this](const Files::Entry &e) {
        if (e.pinId) m_db->touchPin(e.pinId);  // Для «недавних» во вкладке «Все»
        pasteFile(e.path);
    };
    m_filesStrip->onMenu = [this](const Files::Entry &e, const QPoint &globalPos) { showFileMenu(e, globalPos); };
    m_filesStrip->onAddTab    = [this]() { addFilesTab(); };
    m_filesStrip->onTabMenu   = [this](const QString &key, const QPoint &g) { showFilesTabMenu(key, g); };
    m_filesStrip->onMoveToTab = [this](const QList<Files::Entry> &f, const QString &t) { moveFilesToTab(f, t); };

    // Рука: фото из колонок и файлы из полоски, которые несёшь (Carry.h)
    m_carry = new Carry(this, m_scale);
    m_filesStrip->carry = m_carry;
    m_carry->inZone = [this](const QPoint &g) { return carryZone(g); };
    m_carry->itemAt = [this](const QPoint &g, CarryItem &it, QRectF &from) {
        return m_filesStrip->carryItemAt(g, it, from) || photoAt(g, it, from);
    };
    m_carry->dropInside = [this](const QPoint &g, const QList<CarryItem> &items) {
        return m_filesStrip->carryDropAt(g, items);
    };
    m_carry->onStart = [this]() { m_filesStrip->holdOpen(); };  // Чтобы добрать и файлы
    m_carry->onChanged = [this]() { syncCarryMarks(); };
    m_carry->onDragStarted = [this]() { startFileDragSqueeze(); };
    m_carry->onDragFinished = [this](bool) { endFileDragSqueeze(); };
    m_centerSpacer->installEventFilter(this);  // Полоска стоит по центру середины окна

    // Импорт видео и аудио — только в Закрепах, маленькие квадратные кнопки
    // по бокам полоски файлов; добавляют в ТЕКУЩУЮ папку закрепов
    auto importBtn = [this](const QString &glyph, const QColor &bg, const QColor &fg, const QString &tip) {
        auto *b = new SmartButton(glyph, this);
        b->setFixedSize(28, 28);
        QFont f = font();
        f.setPixelSize(13);
        b->setFont(f);
        b->setBtnStyle(bg, fg, 5);
        b->setToolTip(tip);
        applyBtnShadow(b);
        b->hide();
        return b;
    };
    m_importVideoBtn = importBtn(QStringLiteral("🎬"), QColor(10,20,48,245), QColor(160,200,255,220),
                                 tr("Импортировать видео в закрепы"));
    m_importAudioBtn = importBtn(QStringLiteral("🎵"), QColor(32,14,55,245), QColor(210,160,255,220),
                                 tr("Импортировать аудио в закрепы"));
    connect(m_importVideoBtn, &QPushButton::clicked, this, &MainWindow::importVideo);
    connect(m_importAudioBtn, &QPushButton::clicked, this, &MainWindow::importAudio);
    m_filesStrip->installEventFilter(this);  // Полоска сдвинулась/появилась — кнопки за ней
}

// ─── loadHistory ─────────────────────────────────────────────────────────────
// Загружает историю буфера обмена из базы данных и отображает карточки.
//
// Схема работы:
//   1. Инкрементируем m_loadGeneration — отменяем ленивую загрузку предыдущего поколения.
//   2. Очищаем обе колонки (leftLayout и rightLayout).
//   3. Загружаем записи из БД (с учётом фильтра по типу и сортировки).
//   4. Фаза 1: тексты → карточки в leftLayout.
//      Фаза 2: изображения → карточки-заглушки в rightLayout + добавляем в m_pendingImages.
//      Фаза 3: видео → аналогично изображениям но с иконкой 🎬.
//   5. loadNextImage() запускается через singleShot(0) — загружает по одному
//      изображению за итерацию главного цикла, не блокируя UI.
void MainWindow::loadHistory(bool textsOnly)
{
    // textsOnly — перестроить только колонку текстов (при поиске).
    // Картинки справа и их ленивая загрузка в этом случае не трогаются.
    if (!textsOnly) {
        // Отменяем незавершённую ленивую загрузку прошлого поколения.
        // QPointer<QLabel> внутри loadNextImage() увидит что gen != m_loadGeneration и остановится.
        ++m_loadGeneration;
        m_pendingImages.clear();
        clearLayout(m_rightLayout);
        m_mediaOffset = 0;
        m_mediaDone   = false;
    }
    clearLayout(m_leftLayout);
    m_textOffset = 0;
    m_textsDone  = false;
    if (!textsOnly) m_searchIndexValid = false;  // Полная перезагрузка — индекс поиска заново

    // Поиск — по ВСЕЙ истории. Совпадения находим один раз, а показываем
    // порциями (как и без поиска). Начала текстов (до 20 000 символов — огромные
    // логи целиком не нужны) берём из базы один раз и держим в памяти, пока
    // история не изменилась: на каждую букву поиск не ходит в базу.
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    m_searchIds.clear();
    m_searching = !query.isEmpty();
    if (m_searching) {
        if (!m_searchIndexValid) {
            m_searchIndex = m_db->getTextHeads(qMax(AppSettings::get().maxTexts(), 40),
                                               m_sortOrder == SortOrder::DateAsc, 20000);
            m_searchIndexValid = true;
        }
        // Точное вхождение — по 20 000 символам (быстро). Нечёткий поиск
        // (Левенштейн по каждому слову — дорого) — по первым 2 000 и только
        // для запроса от 3 букв: на одну-две буквы он находит почти всё подряд
        const bool fuzzy = query.size() >= 3;
        for (const auto &h : m_searchIndex)
            if (h.second.contains(query, Qt::CaseInsensitive)
                || (fuzzy && fuzzyMatch(h.second.left(2000), query)))
                m_searchIds.append(h.first);
    }

    appendTexts();
    if (!textsOnly) appendMedia();
    if (!textsOnly) m_historyLoaded = true;
}

// ─── Быстрое обновление после копирования ─────────────────────────────────────
// Раньше после каждого копирования окно при открытии перестраивало всю
// историю. Теперь добавляем сверху только новые карточки и убираем те, что
// исчезли из базы (вытеснены лимитом или подняты наверх «Без повторов»).
void MainWindow::refreshHistory()
{
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    if (!m_historyLoaded || !query.isEmpty() || m_sortOrder != SortOrder::DateDesc) {
        loadHistory();
        return;
    }
    m_searchIndexValid = false;

    // Новые записи — сверху, в том же порядке (новые первыми)
    const QList<HistoryItem> texts = m_db->getHistoryNewer({"text", "audio"}, m_topTextId);
    for (int i = 0; i < texts.size(); ++i) {
        if (texts[i].type == "audio") addAudioCard(texts[i], i);
        else                          addTextCard(texts[i], i);
    }
    m_textOffset += texts.size();
    if (!texts.isEmpty()) m_topTextId = texts.first().id;

    const QList<HistoryItem> media = m_db->getHistoryNewer({"image", "video"}, m_topMediaId);
    const bool loaderIdle = m_pendingImages.isEmpty();
    for (int i = 0; i < media.size(); ++i) {
        if (media[i].type == "video") addVideoCard(media[i], i);
        else                          addImageCard(media[i], i);
    }
    m_mediaOffset += media.size();
    if (!media.isEmpty()) m_topMediaId = media.first().id;
    if (loaderIdle && !m_pendingImages.isEmpty()) {
        const int gen = m_loadGeneration;
        QTimer::singleShot(0, this, [this, gen]() { loadNextImage(gen); });
    }

    // Карточки записей, которых в базе уже нет, — убираем
    QList<int> ids;
    for (QVBoxLayout *lay : {m_leftLayout, m_rightLayout})
        for (int i = 0; i < lay->count(); ++i)
            if (QWidget *w = lay->itemAt(i)->widget())
                if (w->property("itemId").isValid()) ids << w->property("itemId").toInt();
    const QSet<int> alive = m_db->existingHistoryIds(ids);
    for (QVBoxLayout *lay : {m_leftLayout, m_rightLayout}) {
        for (int i = lay->count() - 1; i >= 0; --i) {
            QWidget *w = lay->itemAt(i)->widget();
            if (!w || !w->property("itemId").isValid() || alive.contains(w->property("itemId").toInt())) continue;
            lay->removeWidget(w);
            w->deleteLater();
            int &off = (lay == m_leftLayout) ? m_textOffset : m_mediaOffset;
            if (off > 0) --off;
        }
    }

    // Появилось новое — показываем колонку с начала, где оно и стоит
    if (!texts.isEmpty())  m_leftScroller->jumpTo(0);
    if (!media.isEmpty())  m_rightScroller->jumpTo(0);
}

// ─── Подгрузка при прокрутке ──────────────────────────────────────────────────
// Сначала показываем TEXT_PAGE текстов и MEDIA_PAGE картинок/видео; когда
// колонку долистали почти до конца — добавляем следующую порцию.

void MainWindow::appendTexts()
{
    if (m_textsDone) return;
    QList<HistoryItem> page;
    int requested = TEXT_PAGE;
    if (m_searching) {
        const QList<int> ids = m_searchIds.mid(m_textOffset, TEXT_PAGE);
        requested = ids.size() < TEXT_PAGE ? -1 : TEXT_PAGE;  // Совпадений больше нет
        page = m_db->getHistoryByIds(ids);
        m_textOffset += ids.size();
    } else {
        // Колонка текстов: тексты и аудио (аудио ищется только без поиска)
        page = m_db->getHistoryPage({"text", "audio"}, TEXT_PAGE, m_textOffset,
                                    m_sortOrder == SortOrder::DateAsc);
        if (m_textOffset == 0) m_topTextId = page.isEmpty() ? 0 : page.first().id;
        m_textOffset += page.size();
    }
    if (requested < 0 || page.size() < TEXT_PAGE) m_textsDone = true;
    for (const HistoryItem &entry : page) {
        if (entry.type == "audio") addAudioCard(entry);
        else                       addTextCard(entry);
    }
}

void MainWindow::appendMedia()
{
    if (m_mediaDone) return;
    const QList<HistoryItem> page = m_db->getHistoryPage({"image", "video"}, MEDIA_PAGE, m_mediaOffset,
                                                         m_sortOrder == SortOrder::DateAsc);
    if (m_mediaOffset == 0) m_topMediaId = page.isEmpty() ? 0 : page.first().id;
    m_mediaOffset += page.size();
    if (page.size() < MEDIA_PAGE) m_mediaDone = true;

    const bool loaderIdle = m_pendingImages.isEmpty();
    for (const HistoryItem &entry : page) {
        if (entry.type == "video") addVideoCard(entry);
        else                       addImageCard(entry);
    }
    // Запускаем ленивую загрузку картинок (если она ещё не идёт)
    if (loaderIdle && !m_pendingImages.isEmpty()) {
        const int gen = m_loadGeneration;
        QTimer::singleShot(0, this, [this, gen]() { loadNextImage(gen); });
    }
}

// Долистали почти до конца колонки — подгружаем следующую порцию
void MainWindow::maybeLoadMore(QScrollArea *area)
{
    if (m_viewMode != ViewMode::History) return;  // В закрепах подгрузки нет
    QScrollBar *sb = area->verticalScrollBar();
    const int nearEnd = area->viewport()->height();  // «Почти» = в пределах одного экрана колонки
    if (sb->maximum() - sb->value() > nearEnd) return;
    if (area == m_leftScroll)       appendTexts();
    else if (area == m_rightScroll) appendMedia();
}

void MainWindow::addTextCard(const HistoryItem &entry, int insertAt)
{
    ClickableCard *card = new ClickableCard();
    applyCardShadow(card);
    card->setWordWrap(true);
    card->setMaximumWidth(sc(BASE_LEFT) - 36); // даём Qt твёрдую ширину — длинные слова ломаются
    card->setCardStyle(QColor(20, 20, 32, 245));
    card->setStyleSheet(QString("color: #f0f0f0; padding: %1; font-size: %2;").arg(spx(8), spx(12)));

    QString display = entry.content;
    if (display.length() > 300)
        display = display.left(300) + "...";
    QString displayWrapped = softWrap(display);
    card->setText(displayWrapped);

    card->setProperty("fullText",    entry.content);
    card->setProperty("displayText", displayWrapped);
    card->setProperty("itemId",      entry.id);
    DateRuler::setCardDate(card, entry.createdAt);  // Для шкалы дат

    // Текст берём из карточки в момент клика: его могли отредактировать
    connect(card, &ClickableCard::clicked, this, [this, card]() {
        pasteText(card->property("fullText").toString());
    });

    // Правый клик — контекстное меню (Редактировать / Закрепить / Удалить)
    int textId = entry.id;
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this,
            [this, textId, card](const QPoint &pos) {
        showTextContextMenu(textId, card->property("fullText").toString(), card, card->mapToGlobal(pos));
    });

    m_leftLayout->insertWidget(insertAt, card);  // -1 — в конец
    // Подсветка поиска для подгруженных карточек
    if (m_searching) applyHighlight(card, m_searchEdit->text());
}

// Аудио из папок автозахвата — в колонке текстов. Клик — вставить файл
// (как скопированный в Проводнике); послушать — из меню, в плеере Windows.
void MainWindow::addAudioCard(const HistoryItem &entry, int insertAt)
{
    ClickableCard *card = new ClickableCard();
    applyCardShadow(card);
    card->setWordWrap(true);
    card->setMaximumWidth(sc(BASE_LEFT) - 36);
    card->setCardStyle(QColor(32, 14, 55, 245));
    card->setStyleSheet(QString("color: #ddaaff; padding: %1; font-size: %2;").arg(spx(8), spx(12)));

    QString name = entry.content.isEmpty() ? QFileInfo(entry.filepath).fileName() : entry.content;
    if (name.length() > 60) name = name.left(57) + "...";
    // В истории — без названия: только ▶ и полоска (название есть у закрепов)
    card->setFixedHeight(sc(40));
    card->setToolTip(name);
    card->setProperty("pinName", name);
    card->setProperty("itemId", entry.id);
    card->setProperty("filepath", entry.filepath);
    DateRuler::setCardDate(card, entry.createdAt);  // Для шкалы дат

    const QString fp = entry.filepath;
    Media::attachAudio(card, fp);  // ▶ слева и полоска с временем — слушать прямо тут
    connect(card, &ClickableCard::clicked, this, [this, fp]() { pasteFile(fp); });

    const int audioId = entry.id;
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this,
            [this, audioId, fp, card](const QPoint &pos) {
        RoundedMenu menu(this);
        menu.addAction(tr("▶  Воспроизвести"), [card, fp]() {
            auto &pb = Media::Playback::get();
            if (pb.isOwner(card)) pb.toggle(); else pb.start(fp, card, nullptr, false);
        });
        menu.addAction(tr("Открыть в плеере"), [fp]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(fp));
        });
        menu.addAction(tr("📂  Показать в папке"), [fp]() {
            QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(fp)});
        });
        menu.addSeparator();
        menu.addAction(tr("📌  Закрепить"), [this, fp]() {
            QString name, folder;
            if (!askPin(tr("Закрепить аудио"), QFileInfo(fp).baseName(), name, folder)) return;
            m_db->addPin(folder, name, "audio", "", fp);
        });
        menu.addSeparator();
        menu.addAction(tr("🗑  Удалить"), [this, audioId, card]() { deleteCard(audioId, card); });
        menu.exec(card->mapToGlobal(pos));
    });

    m_leftLayout->insertWidget(insertAt, card);  // -1 — в конец
}

void MainWindow::addImageCard(const HistoryItem &entry, int insertAt)
{
    ClickableCard *card = new ClickableCard();
    applyCardShadow(card);
    card->setFixedSize(sc(BASE_LEFT) - 36, sc(120));
    card->setAlignment(Qt::AlignCenter);
    // Серая заглушка — отображается мгновенно, до загрузки реального файла
    card->setCardStyle(QColor(20, 20, 32, 245));
    card->setStyleSheet(QString("color: rgba(255,255,255,60); font-size: %1;").arg(spx(20)));
    card->setText("");  // нейтральный плейсхолдер

    card->setProperty("filepath", entry.filepath);
    card->setProperty("itemId", entry.id);
    DateRuler::setCardDate(card, entry.createdAt);  // Для шкалы дат

    QString fp = entry.filepath;
    connect(card, &ClickableCard::clicked, this, [this, fp]() { pasteImage(fp); });
    // Зажал левой — фото летит в руку (см. pickEvent, Carry.h)
    card->setProperty("pickable", true);
    card->installEventFilter(this);
    card->setPicked(m_carry && m_carry->has(fp));

    // Правый клик — контекстное меню (Просмотреть / Закрепить / Удалить)
    int imgId = entry.id;
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this,
            [this, imgId, fp, card](const QPoint &pos) {
        showImageContextMenu(imgId, fp, card, card->mapToGlobal(pos));
    });

    m_rightLayout->insertWidget(insertAt, card);  // -1 — в конец

    // Добавляем в очередь ленивой загрузки
    m_pendingImages.append({QPointer<QLabel>(card), fp});
}

void MainWindow::addVideoCard(const HistoryItem &entry, int insertAt)
{
    ClickableCard *card = new ClickableCard();
    applyCardShadow(card);
    card->setFixedSize(sc(BASE_RIGHT) - 36, sc(148));
    card->setCardStyle(QColor(10, 20, 48, 245));

    QString name = entry.content.isEmpty()
        ? QFileInfo(entry.filepath).fileName()
        : entry.content;
    if (name.length() > 50) name = name.left(47) + "...";
    card->setProperty("pinName", name);
    card->setProperty("itemId", entry.id);
    card->setProperty("filepath", entry.filepath);
    DateRuler::setCardDate(card, entry.createdAt);  // Для шкалы дат

    // Кадр-превью без названия (оно есть у закрепов); ▶ — играет прямо
    // в карточке (без звука), ⤢ — большой просмотр
    const QString fp = entry.filepath;
    QPointer<ClickableCard> cardPtr = card;
    card->setToolTip(name);
    Media::VideoSurface *surface = setupVideoCard(card, name, fp, QString(), 0,
        [this, cardPtr, fp](Media::VideoSurface *s) { if (cardPtr) m_videoViewer->open(cardPtr, s, fp); });

    // Клик по самой карточке (мимо кнопок) — вставить файл, как раньше;
    // зажал левой — видео летит в руку, как фото (см. pickEvent)
    connect(card, &ClickableCard::clicked, this, [this, fp]() {
        pasteFile(fp);
    });
    card->setProperty("pickable", true);
    card->installEventFilter(this);
    card->setPicked(m_carry && m_carry->has(fp));

    int vidId = entry.id;
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this,
            [this, vidId, fp, card, surface](const QPoint &pos) {
        RoundedMenu menu(this);
        menu.addAction(tr("▶  Смотреть"), [this, card, surface, fp]() {
            m_videoViewer->open(card, surface, fp);
        });
        menu.addAction(tr("Открыть в плеере"), [fp]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(fp));
        });
        menu.addAction(tr("📂  Показать в папке"), [fp]() {
            QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(fp)});
        });
        menu.addSeparator();
        menu.addAction(tr("📌  Закрепить"), [this, fp]() {
            QString name, folder;
            if (!askPin(tr("Закрепить видео"), QFileInfo(fp).baseName(), name, folder)) return;
            m_db->addPin(folder, name, "video", "", fp);
        });
        menu.addSeparator();
        menu.addAction(tr("🗑  Удалить"), [this, vidId, card]() {
            deleteCard(vidId, card);
        });
        menu.exec(card->mapToGlobal(pos));
    });

    m_rightLayout->insertWidget(insertAt, card);  // -1 — в конец
}

// ─── loadNextImage ────────────────────────────────────────────────────────────
// Загружает одну картинку из очереди m_pendingImages и сразу возвращает управление.
// Следующая картинка грузится через QTimer::singleShot(0) — это значит «после
// обработки всех текущих событий в очереди Qt», то есть UI успевает перерисоваться
// между каждой картинкой и не зависает.
void MainWindow::loadNextImage(int gen)
{
    // Отдаём все картинки из очереди фоновой загрузке превью (Thumbs.h):
    // интерфейс не ждёт, одновременно грузятся две картинки.
    while (gen == m_loadGeneration && !m_pendingImages.isEmpty()) {
        auto [cardPtr, filepath] = m_pendingImages.takeFirst();
        // QPointer автоматически равен nullptr, если карточку уже удалили
        if (!cardPtr) continue;
        const qreal dpr = cardPtr->devicePixelRatioF();
        const QSize px = (QSizeF(cardPtr->width() - 8, cardPtr->height() - 8) * dpr).toSize();
        QPointer<QLabel> card = cardPtr;
        Thumbs::loadAsync(filepath, px, cardPtr.data(), [this, card, gen, dpr](const QImage &img) {
            if (!card || gen != m_loadGeneration) return;  // Список уже перестроили
            if (!img.isNull()) {
                QPixmap pm = QPixmap::fromImage(img);
                pm.setDevicePixelRatio(dpr);  // Чётко при масштабе Windows 125–200%
                card->setPixmap(pm);
                card->setStyleSheet("");      // Убираем стиль плейсхолдера
            } else {
                card->setText(tr("Изображение"));
                card->setStyleSheet(QString("color: rgba(255,255,255,80); font-size: %1;").arg(spx(12)));
            }
        });
    }
}

// Поднять SmartClip поверх всего, если его перекрыло чужое окно (игра «без
// рамки» ставит себя наверх). Свои окна, которые были выше SmartClip — меню,
// диалоги, полоса «в руке», окошко руки — поднимаются следом в том же порядке:
// раньше они «проваливались» под SmartClip.
static void keepOnTop(HWND main)
{
    const DWORD self = GetCurrentProcessId();
    RECT mr;
    GetWindowRect(main, &mr);
    QList<HWND> ownAbove;  // Снизу вверх
    bool covered = false;
    for (HWND h = GetWindow(main, GW_HWNDPREV); h; h = GetWindow(h, GW_HWNDPREV)) {
        if (!IsWindowVisible(h)) continue;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == self) { ownAbove.append(h); continue; }
        RECT r, x;
        if (GetWindowRect(h, &r) && IntersectRect(&x, &r, &mr)) covered = true;
    }
    if (!covered) return;
    const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    SetWindowPos(main, HWND_TOPMOST, 0, 0, 0, 0, flags);
    for (HWND h : ownAbove) SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, flags);
}

void MainWindow::showWindow()
{
    // Экран, на котором сейчас курсор (раньше — всегда основной монитор,
    // и на втором мониторе приходилось поворачивать голову)
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QApplication::primaryScreen();
    setGeometry(screen->geometry());

    // Свежие файлы — перечитываем папки в фоне при каждом открытии
    if (m_filesStrip) {
        m_filesStrip->collapseNow();
        m_filesStrip->reload();
    }

    // Запоминаем окно, которое было активным ДО нас — вернём ему фокус при вставке
    m_prevFocusHwnd = reinterpret_cast<quintptr>(GetForegroundWindow());

    // Сбрасываем поиск и закрываем просмотр картинки, если остался с прошлого раза
    if (m_searchEdit)
        m_searchEdit->clear();
    if (m_imageViewer)
        m_imageViewer->hide();

    // Окно открывают снова, пока ещё играет уход, — убираем старую сцену
    // и отменяем отложенное скрытие (см. hideAnimated)
    m_hidePending = false;
    ++m_hideSeq;
    if (m_overlay) m_overlay->abort();

    // Показываем окно СРАЗУ — пользователь не ждёт пока загрузятся данные.
    // Анимацию сборки запускаем ниже, когда карточки уже на месте.
    // В новом стиле окно прячем ДО show(): иначе оно на миг мелькало уже
    // собранным, и только потом начиналась анимация.
    // (Без анимаций — наоборот, обязательно полная видимость: после ухода
    //  окно остаётся прозрачным, а анимации в Windows могли выключить уже после этого.)
    setWindowOpacity(Anim::enabled(Anim::Kind::WindowOpen) ? 0.0 : 1.0);
    // Без активации: фокус остаётся у программы пользователя (см. MainWindow.h)
    show();
    // Ещё раз — уже на мониторе, где окно появилось: при переезде на монитор с
    // другим масштабом Windows пересоздаёт окно, и прозрачность сбрасывалась —
    // окно мелькало целиком, а анимация открытия играла поверх
    setWindowOpacity(Anim::enabled(Anim::Kind::WindowOpen) ? 0.0 : 1.0);
    {
        // WS_EX_NOACTIVATE — клик по окну не делает его активным (иначе поле
        // переименования в Проводнике и т.п. закрывалось от потери фокуса).
        // Поиск фокус забирает сам — см. activateForInput().
        HWND h = reinterpret_cast<HWND>(winId());
        SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    installInputHooks();
    if (m_leftArc)  m_leftArc->watchHover(true);
    if (m_rightArc) m_rightArc->watchHover(true);
    // Поверх всего — и держимся там: игры в режиме «без рамки» сами ставят себя
    // наверх и перекрывали SmartClip. Поднимаемся сразу, чуть позже и потом раз
    // в секунду (без активации — фокус не трогаем)
    if (!m_topmostTimer) {
        m_topmostTimer = new QTimer(this);
        m_topmostTimer->setInterval(1000);
        connect(m_topmostTimer, &QTimer::timeout, this, [this]() {
            if (!isVisible()) { m_topmostTimer->stop(); return; }
            keepOnTop(reinterpret_cast<HWND>(winId()));
        });
    }
    m_topmostTimer->start();
    for (int ms : {60, 250})
        QTimer::singleShot(ms, this, [this]() {
            if (isVisible()) keepOnTop(reinterpret_cast<HWND>(winId()));
        });


    // Обновляем данные если нужно
    if (m_viewMode == ViewMode::History && m_historyDirty) {
        refreshHistory();
        m_historyDirty = false;
    } else if (m_viewMode == ViewMode::Pins) {
        loadPins(); // закрепов обычно мало — всегда перегружаем
    }

    // Новый стиль: панели съезжаются с краёв экрана, а карточки выезжают
    // лесенкой — от верхней к нижней, в обеих колонках
    if (Anim::enabled(Anim::Kind::WindowOpen)) {
        // Карточки только что добавлены. Qt показывает их и раскладывает
        // ОТЛОЖЕННО (на следующем шаге цикла событий), поэтому сейчас они ещё
        // невидимы и с нулевыми координатами — показываем и раскладываем сами.
        // Скрытые поиском (явно спрятанные) не трогаем.
        for (QVBoxLayout *lay : {m_leftLayout, m_rightLayout})
            for (int i = 0; i < lay->count(); ++i)
                if (QWidget *w = lay->itemAt(i)->widget())
                    if (!w->testAttribute(Qt::WA_WState_ExplicitShowHide))
                        w->show();
        for (int i = 0; i < 3; ++i)
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        // Дуга — заново под текущий размер окна и прокрутку (иначе карточки,
        // расставленные, пока окно было скрыто, могли остаться сдвинутыми)
        if (m_leftArc)  m_leftArc->refresh();
        if (m_rightArc) m_rightArc->refresh();

        // Сцену оставляем жить после появления, только если закрытие тоже
        // анимированное — тогда уход стартует мгновенно на готовой сцене
        m_overlay = Anim::playIn(Anim::Kind::WindowOpen, this, buildOpenPieces(), 620, 1.3,
                                 /*keepAlive=*/Anim::enabled(Anim::Kind::WindowClose));
    }

    // Есть обновление, а уведомление в трее пропустили — предлагаем один раз,
    // когда пользователь сам открыл SmartClip (после того как окно отрисуется)
    if (!m_pendingUpdateVersion.isEmpty() && !m_updatePromptShown) {
        m_updatePromptShown = true;
        QTimer::singleShot(300, this, [this]() { if (isVisible()) showPendingUpdate(); });
    }
}

// ─── buildOpenPieces ─────────────────────────────────────────────────────────
// Куски окна для анимации сборки/ухода: верхняя и нижняя панели + по полосе на
// каждую видимую карточку (лесенка). Одни и те же куски проигрываются вперёд
// при открытии и задом наперёд (в 2 раза быстрее) при закрытии.
QList<Anim::Piece> MainWindow::buildOpenPieces()
{
    const int W = width();
    const QRect top = m_topPanel->geometry();
    // Полоса папок — только над левой колонкой: правая начинается сразу под панелью
    const QRect fold = m_foldersBar->isVisible() ? m_foldersBar->geometry() : QRect();
    const QRect bottom = m_bottomPanel->geometry();
    const int colTop = top.bottom() + 1;
    const int leftTop = fold.isEmpty() ? colTop : fold.bottom() + 1;
    const int colBot = bottom.top();

    QList<Anim::Piece> pcs;
    auto add = [&](QRect r, QPointF from, qreal delay, qreal scale) {
        if (r.isEmpty()) return;
        Anim::Piece pc;
        pc.src = r; pc.fromOffset = from; pc.delay = delay;
        pc.anchor = QRectF(r).center();
        pc.fromScaleX = pc.fromScaleY = scale;
        pcs << pc;
    };
    add(top,    QPointF(0, -top.height() - 20),    0.00, 0.98);
    add(fold,   QPointF(0, -fold.bottom() - 20),   0.02, 0.98);
    add(bottom, QPointF(0, bottom.height() + 20),  0.06, 0.98);
    // Полоска файлов (и кнопки импорта рядом) — уезжает вниз следом за поиском
    if (m_filesStrip && m_filesStrip->isVisible()) {
        QRect st = m_filesStrip->visibleRect();
        if (m_importVideoBtn && m_importVideoBtn->isVisible())
            st = st.united(m_importVideoBtn->geometry()).united(m_importAudioBtn->geometry());
        add(st, QPointF(0, height() - st.top() + 20), 0.09, 0.98);
    }

    // Колонку режем на горизонтальные полосы по середине промежутков между
    // карточками: каждая полоса = одна карточка со своей тенью, и полосы не
    // перекрываются (иначе края соседних карточек рисовались бы дважды)
    auto column = [&](QVBoxLayout *lay, QScrollArea *area, int x0, int x1, qreal dir, int colTop) {
        const QRect view(area->viewport()->mapTo(this, QPoint(0, 0)), area->viewport()->size());
        QList<QRect> cards;
        for (int i = 0; i < lay->count(); ++i) {
            QWidget *w = lay->itemAt(i)->widget();
            if (!w || !w->isVisible()) continue;
            const QRect r(w->mapTo(this, QPoint(0, 0)), w->size());
            if (r.intersects(view)) cards << r;
        }
        // Пустая колонка — один кусок на всю высоту (тёмный фон тоже должен ехать)
        if (cards.isEmpty()) {
            add(QRect(x0, colTop, x1 - x0, colBot - colTop), QPointF(dir * 110, 0), 0.10, 0.92);
            return;
        }
        for (int i = 0; i < cards.size(); ++i) {
            const int y0 = (i == 0) ? colTop : (cards[i - 1].bottom() + cards[i].top()) / 2;
            const int y1 = (i == cards.size() - 1) ? colBot
                                                    : (cards[i].bottom() + cards[i + 1].top()) / 2;
            const qreal delay = qMin(0.10 + i * 0.06, 0.55);  // Лесенка сверху вниз
            add(QRect(x0, y0, x1 - x0, y1 - y0), QPointF(dir * 110, 0), delay, 0.92);
        }
    };
    // Ширина колонок — с запасом под «Тёмный фон за колонками»: он рисуется
    // на 18px шире колонки (см. paintEvent). Раньше куски резались по ширине
    // колонки — фон ехал узким, а в конце «расширялся» до полной ширины.
    const int leftX1  = qMax(m_leftScroll->geometry().right() + 1, sc(BASE_LEFT) + 18);
    const int rightX0 = qMin(m_rightScroll->geometry().left(), W - sc(BASE_RIGHT) - 18);
    column(m_leftLayout,  m_leftScroll,  0, leftX1, -1.0, leftTop);
    column(m_rightLayout, m_rightScroll, rightX0, W, +1.0, colTop);

    return pcs;
}

// ─── hideAnimated ────────────────────────────────────────────────────────────
// Закрыть окно с анимацией ухода (клик в пустое место, Esc, горячая клавиша).
// Настоящее окно прячется СРАЗУ — фокус и клики уходят без задержки, а
// разлетающиеся карточки — это только картинка поверх экрана.
// Вставка по клику на карточку использует обычный hide() — без анимации.
void MainWindow::hideAnimated()
{
    if (!isVisible() || m_hidePending) return;

    // Почему не hide() сразу: у полноэкранного полупрозрачного окна hide()
    // занимает в Windows ~80мс, и всё это время программа стоит — анимация
    // замирала, а потом прыгала («моргание»). Поэтому на время анимации окно
    // делаем полностью прозрачным (мгновенно; полностью прозрачное окно Windows
    // пропускает клики насквозь), а настоящий hide() — когда анимация доиграла.
    m_hidePending = true;
    dropSqueeze();
    removeInputHooks();  // Окно уходит — клавиши сразу снова идут в программу пользователя
    Media::Playback::get().stop();
    if (m_videoViewer) m_videoViewer->hideNow();
    if (m_imageViewer) m_imageViewer->hide();
    const int seq = ++m_hideSeq;
    auto hideNow = [this, seq]() {
        if (seq == m_hideSeq && m_hidePending) {
            m_hidePending = false;
            hide();
        }
    };
    auto vanish = [this, seq]() {
        if (seq == m_hideSeq && m_hidePending) setWindowOpacity(0.0);
    };
    // Фокус клавиатуры сразу возвращаем окну, где пользователь работал, —
    // иначе нажатые сразу после Esc клавиши уходили бы в невидимый SmartClip
    auto releaseFocus = [this]() {
        HWND prev = reinterpret_cast<HWND>(m_prevFocusHwnd);
        if (isActiveWindow() && prev && IsWindow(prev))
            SetForegroundWindow(prev);
    };

    // Ещё играет появление — разворачиваем его обратно с текущего места.
    // Настоящее окно в этот момент и так прозрачное.
    if (m_overlay && m_overlay->isPlaying() && !m_overlay->isReverse()) {
        if (!Anim::enabled(Anim::Kind::WindowClose)) {
            // Анимации выключены в Windows: просто обрываем появление и прячем окно
            m_overlay->abort();
            m_hidePending = false;
            hide();
            return;
        }
        setWindowOpacity(0.0);
        m_overlay->setOnFinished(hideNow);
        m_overlay->reverseNow();
        releaseFocus();
        QTimer::singleShot(1500, this, hideNow);  // Страховка
        return;
    }

    if (Anim::enabled(Anim::Kind::WindowClose) && windowOpacity() > 0.99) {
        const QList<Anim::Piece> pcs = buildOpenPieces();
        if (m_overlay && m_overlay->isIdle()) {
            // Сцена от появления уже на экране — уход стартует мгновенно
            m_overlay->replayOut(pcs, Anim::ms(Anim::Kind::WindowClose, 310), vanish);  // В 2 раза быстрее появления
        } else {
            if (m_overlay) m_overlay->abort();
            m_overlay = Anim::playOut(Anim::Kind::WindowClose, this, pcs, 310, 1.3);
            if (m_overlay) m_overlay->setOnFirstFrame(vanish);
        }
        if (m_overlay) m_overlay->setOnFinished(hideNow);
        else           hideNow();
        releaseFocus();
        QTimer::singleShot(Anim::ms(Anim::Kind::WindowClose, 310) + 1000, this, hideNow);  // Страховка
        return;
    }
    m_hidePending = false;
    hide();
}

bool MainWindow::isOpen() const
{
    // Окно на экране и не в процессе ухода
    return isVisible() && !m_hidePending;
}

// markHistoryDirty — помечает историю «грязной» (требует перезагрузки).
// Вызывается ClipboardManager::historyChanged() через сигнал/слот.
// При следующем открытии окна loadHistory() будет вызван автоматически.
void MainWindow::markHistoryDirty()
{
    m_historyDirty = true;
    m_searchIndexValid = false;
}

void MainWindow::filterHistory(const QString &query)
{
    for (int i = 0; i < m_leftLayout->count(); ++i) {
        QWidget *widget = m_leftLayout->itemAt(i)->widget();
        if (!widget) continue;
        QString fullText = widget->property("fullText").toString();

        if (query.isEmpty()) {
            widget->show();
        } else {
            const QString head = fullText.left(20000);
            widget->setVisible(head.contains(query.trimmed(), Qt::CaseInsensitive) ||
                               fuzzyMatch(head.left(2000), query));
        }

        // Подсветка совпадений в тексте (только у карточек с displayText)
        if (widget->isVisible()) {
            if (QLabel *label = qobject_cast<QLabel*>(widget))
                applyHighlight(label, query);
        }
    }
}

void MainWindow::filterPins(const QString &query)
{
    bool tagsOnly = query.startsWith('#');
    QString effectiveQuery = tagsOnly ? query.mid(1).trimmed() : query;

    auto filterLayout = [&](QVBoxLayout *layout) {
        for (int i = 0; i < layout->count(); ++i) {
            QWidget *widget = layout->itemAt(i)->widget();
            if (!widget) continue;
            QString name = widget->property("pinName").toString();
            QString tags = widget->property("pinTags").toString();

            if (effectiveQuery.isEmpty()) {
                widget->show();
            } else {
                bool matchesName = !tagsOnly && (name.isEmpty() || fuzzyMatch(name, effectiveQuery));
                bool matchesTags = !tags.isEmpty() && fuzzyMatch(tags, effectiveQuery);
                widget->setVisible(matchesName || matchesTags);
            }

            // Подсветка в текстовых пин-карточках
            if (widget->isVisible()) {
                if (QLabel *label = qobject_cast<QLabel*>(widget))
                    applyHighlight(label, effectiveQuery);
            }
        }
    };
    filterLayout(m_leftLayout);
    filterLayout(m_rightLayout);
}

bool MainWindow::isInteractiveArea(const QPoint &pos) const
{
    // childAt возвращает самый глубокий дочерний виджет в этой точке
    QWidget *child = childAt(pos);

    // Если виджета нет или это прозрачный центр — клик проваливается
    if (!child || child == m_centerSpacer)
        return false;
    // Над видимой частью полоски файлов — прозрачно (она растёт вверх внутри себя)
    if (m_filesStrip && child == m_filesStrip && !m_filesStrip->hitVisible(m_filesStrip->mapFrom(this, pos)))
        return false;

    return true;
}

// ─── Симуляция вставки ────────────────────────────────────────────────────────
// Терминалы, где Ctrl+V не вставляет (отправляет символ ^V): Git Bash (mintty),
// PuTTY/KiTTY, ConEmu. Во всех них вставка — Shift+Insert.
static bool pastesWithShiftInsert(HWND fg)
{
    wchar_t cls[64] = {};
    GetClassNameW(GetAncestor(fg, GA_ROOT), cls, 64);
    for (const wchar_t *c : {L"mintty", L"PuTTY", L"KiTTY", L"VirtualConsoleClass"})
        if (wcscmp(cls, c) == 0) return true;
    return false;
}

// Перед вставкой «отпускаем» лишние модификаторы, если пользователь их ещё держит.
// Иначе хоткей профиля Ctrl+Shift+1 превращал вставку в Ctrl+Shift+V
// (в Chrome/Word это «вставить без форматирования», в других программах —
// вообще другое действие), а Alt+F2 — в Ctrl+Alt+V.
// Одно нажатие или отпускание клавиши
static void sendKey(WORD vk, bool up)
{
    INPUT in = {};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = vk;
    // Скан-код нужен удалённому рабочему столу, виртуальным машинам, играм
    // и части Java-программ: они смотрят на него, а не на код клавиши
    in.ki.wScan   = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (vk == VK_INSERT || vk == VK_LWIN || vk == VK_RWIN)
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in, sizeof(INPUT));
}

// Нажать Ctrl+V (или Shift+Insert) «как рукой»: с паузами между нажатием и
// отпусканием. Раньше всё уходило одной пачкой за одну миллисекунду — игры и
// часть программ проверяют клавиши раз в кадр и такое нажатие не замечали,
// хотя обычный Ctrl+V пальцами у них работает.
static void sendPaste()
{
    const bool shiftIns = pastesWithShiftInsert(GetForegroundWindow());
    const WORD mod = shiftIns ? VK_SHIFT : VK_CONTROL;
    const WORD key = shiftIns ? VK_INSERT : 'V';
    // Модификатор вставки нажимаем ПЕРВЫМ, а уже потом отпускаем Alt/Win:
    // одиночное нажатие-отпускание Alt открывает меню программы (как F10),
    // а Win — Пуск, и тогда вставка уходит в меню.
    sendKey(mod, false);
    for (WORD vk : {WORD(VK_CONTROL), WORD(VK_SHIFT), WORD(VK_MENU), WORD(VK_LWIN), WORD(VK_RWIN)})
        if (vk != mod && (GetAsyncKeyState(vk) & 0x8000))
            sendKey(vk, true);
    QTimer::singleShot(25, [key]() { sendKey(key, false); });
    QTimer::singleShot(25 + 45, [key]() { sendKey(key, true); });
    QTimer::singleShot(25 + 45 + 25, [mod]() { sendKey(mod, true); });
}

// Вернуть фокус окну, куда вставляем. Windows не всегда даёт программе
// переключать фокус — тогда «склеиваем» ввод с текущим активным окном
// (известный обходной путь), и переключение разрешается.
static bool forceForeground(HWND target)
{
    if (!target || !IsWindow(target)) return false;
    HWND root = GetAncestor(target, GA_ROOT);
    if (GetAncestor(GetForegroundWindow(), GA_ROOT) == root) return true;
    if (IsIconic(root)) ShowWindow(root, SW_RESTORE);
    if (!SetForegroundWindow(root)) {
        const DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const DWORD me = GetCurrentThreadId();
        if (fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE)) {
            BringWindowToTop(root);
            SetForegroundWindow(root);
            AttachThreadInput(me, fgThread, FALSE);
        }
    }
    return GetAncestor(GetForegroundWindow(), GA_ROOT) == root;
}

// ─── Окно без захвата фокуса: глобальные хуки ────────────────────────────────
// Пока SmartClip на экране, фокус остаётся у программы пользователя. Поэтому
// клавиши и клики мимо нам нужно ловить самим (см. MainWindow.h).
//
// Хуки клавиатуры и мыши работают в СВОЁМ потоке. Windows ждёт ответа хука на
// каждое движение мыши во всей системе: пока они жили в потоке интерфейса,
// любая заминка SmartClip (перестройка Истории после Закрепов и т.п.) делала
// мышь рваной и «замедленной» на всём компьютере.
namespace {
std::atomic<MainWindow *> s_hookOwner{nullptr};
std::atomic<HMONITOR>     s_ownMonitor{nullptr};  // Монитор, где открыт SmartClip
HANDLE        s_hookThread   = nullptr;
DWORD         s_hookThreadId = 0;
HWINEVENTHOOK s_fgHook       = nullptr;

bool isOwnWindow(HWND h)
{
    DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

bool weAreForeground() { return isOwnWindow(GetForegroundWindow()); }

// Клавиши, пока мы без фокуса. Раньше всё напечатанное при открытом SmartClip
// шло в его поиск — так и оставляем: первая же клавиша забирает фокус и
// попадает в поиск, а не в программу под нами (Delete в Проводнике удалил бы файл).
LRESULT CALLBACK keyboardHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && s_hookOwner && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        const auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lp);
        const UINT vk = kb->vkCode;
        auto held = [](int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; };
        const bool modifier = vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT
                           || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL
                           || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU
                           || vk == VK_LWIN || vk == VK_RWIN || vk == VK_CAPITAL
                           // Служебные клавиши — не печать: F1–F24, PrtSc, громкость,
                           // медиа, кнопки браузера/почты, NumLock/ScrollLock, Pause
                           || (vk >= VK_F1 && vk <= VK_F24) || vk == VK_SNAPSHOT
                           || (vk >= VK_BROWSER_BACK && vk <= VK_LAUNCH_APP2)
                           || vk == VK_NUMLOCK || vk == VK_SCROLL || vk == VK_PAUSE || vk == VK_SLEEP;
        // Сочетания с Ctrl/Alt/Win (горячие клавиши, Alt+Tab) не трогаем
        MainWindow *owner = s_hookOwner;
        // Пользователь работает в окне на ДРУГОМ мониторе (например, играет, а
        // SmartClip открыт рядом с фото) — клавиши его, не перехватываем
        HWND fg = GetForegroundWindow();
        const bool otherMonitor = fg && s_ownMonitor
            && MonitorFromWindow(fg, MONITOR_DEFAULTTONULL) != s_ownMonitor.load();
        if (owner && !otherMonitor && !(kb->flags & LLKHF_INJECTED) && !modifier && !weAreForeground()
            && !held(VK_CONTROL) && !held(VK_MENU) && !held(VK_LWIN) && !held(VK_RWIN)) {
            const unsigned scan = kb->scanCode;
            const bool ext = (kb->flags & LLKHF_EXTENDED) != 0;
            // В поток интерфейса — очередью; сам хук отвечает Windows сразу
            QMetaObject::invokeMethod(owner, [vk, scan, ext]() {
                if (MainWindow *o = s_hookOwner) o->onHookKey(vk, scan, ext);
            }, Qt::QueuedConnection);
            return 1;  // Клавишу «съели» — программа под нами её не получит
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// Любой клик: решаем на следующем шаге цикла событий, был ли он мимо SmartClip
LRESULT CALLBACK mouseHook(int code, WPARAM wp, LPARAM lp)
{
    MainWindow *owner = s_hookOwner;
    if (code == HC_ACTION && owner
        && (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
        QMetaObject::invokeMethod(owner, []() {
            if (MainWindow *o = s_hookOwner) o->onGlobalClick();
        }, Qt::QueuedConnection);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// Сменилось активное окно (Alt+Tab, клик по панели задач и т.п.)
void CALLBACK foregroundHook(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD)
{
    MainWindow *owner = s_hookOwner;
    if (!owner) return;
    const quintptr h = reinterpret_cast<quintptr>(hwnd);
    QMetaObject::invokeMethod(owner, [h]() {
        if (MainWindow *o = s_hookOwner) o->onForegroundChanged(h);
    }, Qt::QueuedConnection);
}

// Окно h — на другом мониторе, не там, где открыт SmartClip (our).
// Работа на соседнем мониторе SmartClip не закрывает.
bool onOtherMonitor(HWND our, HWND h)
{
    if (!our || !h) return false;
    HMONITOR m = MonitorFromWindow(GetAncestor(h, GA_ROOT), MONITOR_DEFAULTTONULL);
    return m && m != MonitorFromWindow(our, MONITOR_DEFAULTTONEAREST);
}

// Поток хуков: ставит хуки и крутит свой цикл сообщений, пока не придёт WM_QUIT
DWORD WINAPI hookThreadProc(LPVOID readyEvent)
{
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // Создаём очередь до сигнала «готов»
    HHOOK kb    = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHook, GetModuleHandleW(nullptr), 0);
    HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, GetModuleHandleW(nullptr), 0);
    SetEvent(static_cast<HANDLE>(readyEvent));
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (kb)    UnhookWindowsHookEx(kb);
    if (mouse) UnhookWindowsHookEx(mouse);
    return 0;
}

// Куда можно вставлять. Панель задач (SmartClip открыли из трея) и сам
// SmartClip — некуда: тогда только кладём в буфер обмена.
bool isPasteTarget(HWND h)
{
    if (!h || !IsWindow(h) || isOwnWindow(h)) return false;
    wchar_t cls[64] = {};
    GetClassNameW(GetAncestor(h, GA_ROOT), cls, 64);
    for (const wchar_t *c : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
                             L"NotifyIconOverflowWindow", L"TopLevelWindowForOverflowXamlIsland"})
        if (wcscmp(cls, c) == 0) return false;
    return true;
}
} // namespace

void MainWindow::installInputHooks()
{
    // Окно, которое не показывается на экране (снимки для проверки), не должно
    // перехватывать клавиатуру и мышь пользователя
    if (testAttribute(Qt::WA_DontShowOnScreen)) return;
    s_hookOwner = this;
    s_ownMonitor = MonitorFromWindow(reinterpret_cast<HWND>(winId()), MONITOR_DEFAULTTONEAREST);
    if (!s_hookThread) {
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        s_hookThread = CreateThread(nullptr, 0, hookThreadProc, ready, 0, &s_hookThreadId);
        if (s_hookThread) {
            SetThreadPriority(s_hookThread, THREAD_PRIORITY_HIGHEST);  // Мышь — без задержек
            WaitForSingleObject(ready, 1000);
        }
        CloseHandle(ready);
    }
    // Смена активного окна — событие без ожидания, его можно и в потоке интерфейса
    if (!s_fgHook)
        s_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                   foregroundHook, 0, 0, WINEVENT_OUTOFCONTEXT);
}

void MainWindow::removeInputHooks()
{
    s_hookOwner = nullptr;
    if (s_hookThread) {
        PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(s_hookThread, 1000);
        CloseHandle(s_hookThread);
        s_hookThread = nullptr;
        s_hookThreadId = 0;
    }
    if (s_fgHook)    { UnhookWinEvent(s_fgHook);         s_fgHook = nullptr; }
}

// Забрать фокус. Windows разрешает SetForegroundWindow только процессу, от
// которого пришёл последний ввод, — поэтому сначала «нажимаем» несуществующую
// клавишу 0x88 (её никто не обрабатывает), и последним вводом становимся мы.
void MainWindow::activateForInput()
{
    if (weAreForeground()) return;
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = 0x88;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
    activateWindow();
    if (m_searchEdit && m_searchEdit->isVisible() && !(m_imageViewer && m_imageViewer->isVisible()))
        m_searchEdit->setFocus(Qt::OtherFocusReason);
}

void MainWindow::onHookKey(unsigned vk, unsigned scan, bool extended)
{
    if (!isOpen()) return;
    if (vk == VK_ESCAPE) {
        if (m_carry->active()) { m_carry->cancel(); return; }  // Сначала — положить всё из руки
        // Как Esc в окне: сначала закрываем просмотр картинки
        if (m_videoViewer && m_videoViewer->isVisible() && !m_videoViewer->isClosing())
            m_videoViewer->closeAnimated();
        else if (m_imageViewer && m_imageViewer->isVisible() && !m_imageViewer->isClosing())
            m_imageViewer->closeAnimated();
        else hideAnimated();
        return;
    }
    // Забираем фокус и «нажимаем» клавишу ещё раз — теперь она придёт к нам
    activateForInput();
    if (!weAreForeground()) return;
    INPUT in[2] = {};
    for (int i = 0; i < 2; ++i) {
        in[i].type       = INPUT_KEYBOARD;
        in[i].ki.wVk     = WORD(vk);
        in[i].ki.wScan   = WORD(scan);
        in[i].ki.dwFlags = (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (i ? KEYEVENTF_KEYUP : 0);
    }
    SendInput(2, in, sizeof(INPUT));
}

void MainWindow::onGlobalClick()
{
    // Окно активно — клики мимо отследит WM_ACTIVATE, как и раньше
    if (!isOpen() || weAreForeground() || m_fileDrag || (m_carry && m_carry->active())) return;
    POINT pt;
    GetCursorPos(&pt);
    // Прозрачный центр SmartClip клики пропускает, и WindowFromPoint вернёт
    // окно под нами — значит, клик мимо
    if (isOwnWindow(GetAncestor(WindowFromPoint(pt), GA_ROOT))) return;
    // Клик на другом мониторе — SmartClip остаётся открытым
    if (MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)
        != MonitorFromWindow(reinterpret_cast<HWND>(winId()), MONITOR_DEFAULTTONEAREST)) return;
    if (QWidget *popup = QApplication::activePopupWidget()) popup->close();
    hideAnimated();
}

void MainWindow::onForegroundChanged(quintptr hwnd)
{
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!isOpen() || !h || isOwnWindow(h) || m_fileDrag || (m_carry && m_carry->active())) return;
    // Фокус вернулся в программу, где работал пользователь (например, закрыли
    // наш диалог) — это не повод закрываться
    HWND prev = reinterpret_cast<HWND>(m_prevFocusHwnd);
    if (prev && GetAncestor(h, GA_ROOT) == GetAncestor(prev, GA_ROOT)) return;
    // Перешли в окно на другом мониторе — остаёмся открытыми, а вставлять
    // теперь будем туда, где пользователь работает
    if (onOtherMonitor(reinterpret_cast<HWND>(winId()), h)) {
        if (isPasteTarget(h)) m_prevFocusHwnd = hwnd;
        return;
    }
    closeAllOnDeactivate();
}

// Спрятаться и вставить в окно, где работал пользователь
void MainWindow::finishPaste()
{
    HWND target = reinterpret_cast<HWND>(m_prevFocusHwnd);
    hide();
    if (!isPasteTarget(target)) return;  // Вставлять некуда — данные уже в буфере обмена
    // Фокус должен быть у окна, куда вставляем: если пользователь печатал в
    // поиске, фокус у нас; бывает, что Windows отдала его кому-то ещё.
    // Ждём (до ~0,4 с), пока окно действительно станет активным, и только
    // потом нажимаем Ctrl+V — иначе вставка уходила в никуда.
    forceForeground(target);
    const quintptr t = reinterpret_cast<quintptr>(target);
    QTimer::singleShot(30, [t]() { pasteWhenFocused(t, 0); });
}

// Ждём, пока окно станет активным (каждые 50 мс, до 8 раз), и вставляем
void MainWindow::pasteWhenFocused(quintptr targetPtr, int tries)
{
    HWND target = reinterpret_cast<HWND>(targetPtr);
    const bool ready = GetAncestor(GetForegroundWindow(), GA_ROOT) == GetAncestor(target, GA_ROOT);
    if (ready || tries >= 8) { QTimer::singleShot(ready ? 40 : 0, &sendPaste); return; }
    forceForeground(target);
    QTimer::singleShot(50, [targetPtr, tries]() { pasteWhenFocused(targetPtr, tries + 1); });
}

// ─── Файлы в полоске внизу ───────────────────────────────────────────────────

// Закреплённые файлы: у папки — свои, во «Все» — из всех папок. Вкладки
// («полки» внутри папки) — сохранённые плюс те, что записаны у самих файлов.
void MainWindow::refreshPinnedFiles(bool allFolders)
{
    if (!m_filesStrip || m_viewMode != ViewMode::Pins) return;
    const QList<PinItem> pins = (allFolders || m_currentFolder.isEmpty()) ? m_db->getAllPins()
                                                                         : m_db->getPins(m_currentFolder);
    QList<Files::Entry> files;
    QStringList tabs = pinFileTabs();
    for (const PinItem &pin : pins) {
        if (pin.type != "file") continue;
        files << Files::fromPin(pin.id, pin.name, pin.filepath, pin.content);
        if (!pin.content.isEmpty() && !tabs.contains(pin.content)) tabs << pin.content;
    }
    m_filesStrip->setPinned(files, tabs);
}

QStringList MainWindow::pinFileTabs() const
{
    return AppSettings::get().pinFileTabs(m_currentFolder);
}

void MainWindow::addFilesTab()
{
    if (m_viewMode == ViewMode::Pins) {
        // Закрепы: новая «полка» в этой папке
        bool ok = false;
        const QString name = askText(this, tr("Новая вкладка"), tr("Название:"), QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        QStringList tabs = pinFileTabs();
        if (!tabs.contains(name)) tabs << name;
        AppSettings::get().setPinFileTabs(m_currentFolder, tabs);
        refreshPinnedFiles(!m_searchEdit->text().isEmpty());
    } else {
        // История: ещё одна папка, файлы из которой показывать
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Папка с файлами"));
        if (dir.isEmpty()) return;
        QStringList folders = Files::configuredFolders();
        for (const QString &f : folders)
            if (QDir::cleanPath(f).compare(QDir::cleanPath(dir), Qt::CaseInsensitive) == 0) return;
        folders << dir;
        AppSettings::get().setFilesFolders(folders);
        m_filesStrip->reload();
    }
}

void MainWindow::showFilesTabMenu(const QString &key, const QPoint &globalPos)
{
    RoundedMenu menu(this);
    if (m_viewMode == ViewMode::Pins) {
        menu.addAction(tr("🏷  Переименовать"), [this, key]() {
            bool ok = false;
            const QString name = askText(this, tr("Переименовать вкладку"), tr("Название:"), key, &ok).trimmed();
            if (!ok || name.isEmpty() || name == key) return;
            QStringList tabs = pinFileTabs();
            const int i = tabs.indexOf(key);
            if (i >= 0) tabs[i] = name; else tabs << name;
            tabs.removeDuplicates();
            AppSettings::get().setPinFileTabs(m_currentFolder, tabs);
            for (const PinItem &pin : m_db->getAllPins())
                if (pin.type == "file" && pin.content == key && (m_currentFolder.isEmpty() || pin.folder == m_currentFolder))
                    m_db->updatePinContent(pin.id, name);
            refreshPinnedFiles(!m_searchEdit->text().isEmpty());
        });
        menu.addSeparator();
        menu.addAction(tr("✕  Удалить вкладку"), [this, key]() {
            // Файлы не удаляются — переезжают во «Все»
            QStringList tabs = pinFileTabs();
            tabs.removeAll(key);
            AppSettings::get().setPinFileTabs(m_currentFolder, tabs);
            for (const PinItem &pin : m_db->getAllPins())
                if (pin.type == "file" && pin.content == key && (m_currentFolder.isEmpty() || pin.folder == m_currentFolder))
                    m_db->updatePinContent(pin.id, QString());
            refreshPinnedFiles(!m_searchEdit->text().isEmpty());
        });
    } else {
        menu.addAction(tr("📂  Открыть папку"), [key]() { QDesktopServices::openUrl(QUrl::fromLocalFile(key)); });
        menu.addSeparator();
        menu.addAction(tr("✕  Убрать вкладку"), [this, key]() {
            QStringList folders = Files::configuredFolders();
            for (int i = folders.size() - 1; i >= 0; --i)
                if (QDir::cleanPath(folders[i]).compare(QDir::cleanPath(key), Qt::CaseInsensitive) == 0)
                    folders.removeAt(i);
            AppSettings::get().setFilesFolders(folders);
            m_filesStrip->reload();
        });
    }
    menu.exec(globalPos);
}

// Переложить закреплённые файлы на другую вкладку («Все» — key пустой)
void MainWindow::moveFilesToTab(const QList<Files::Entry> &files, const QString &tab)
{
    for (const Files::Entry &e : files)
        if (e.pinId) m_db->updatePinContent(e.pinId, tab);
    refreshPinnedFiles(!m_searchEdit->text().isEmpty());
}
void MainWindow::showFileMenu(const Files::Entry &e, const QPoint &globalPos)
{
    RoundedMenu menu(this);
    const QString f = e.path;
    if (!e.missing) {
        menu.addAction(tr("📋  Вставить"), [this, e]() {
            if (e.pinId) m_db->touchPin(e.pinId);
            pasteFile(e.path);
        });
        menu.addAction(tr("Открыть"), [f]() { QDesktopServices::openUrl(QUrl::fromLocalFile(f)); });
        menu.addAction(tr("📂  Показать в папке"), [f]() {
            QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(f)});
        });
    }
    menu.addAction(tr("ℹ  Свойства"), [this, f]() { fileInfoDialog(this, f); });
    menu.addSeparator();
    if (e.pinId) {
        const int pinId = e.pinId;
        const QStringList tabs = pinFileTabs();
        if (!tabs.isEmpty() || !e.group.isEmpty()) {
            auto *sub = new RoundedMenu(&menu);  // В нашем стиле, как и само меню
            sub->setTitle(tr("🗂  Во вкладку"));
            menu.addMenu(sub);
            const Files::Entry one = e;
            QStringList all = tabs;
            if (!e.group.isEmpty() && !all.contains(e.group)) all << e.group;
            sub->addAction(tr("Все"), [this, one]() { moveFilesToTab({one}, QString()); })->setEnabled(!one.group.isEmpty());
            for (const QString &t : all)
                sub->addAction(t, [this, one, t]() { moveFilesToTab({one}, t); })->setEnabled(t != one.group);
        }
        menu.addAction(tr("🗑  Открепить"), [this, pinId]() {
            m_db->deletePin(pinId);  // Сам файл на диске не трогается
            const QString q = m_searchEdit ? m_searchEdit->text() : QString();
            loadPins(!q.isEmpty());
            if (!q.isEmpty()) filterPins(q);
        });
    } else {
        menu.addAction(tr("📌  Закрепить"), [this, f]() { pinFile(f); });
    }
    menu.exec(globalPos);
}

void MainWindow::pinFile(const QString &filepath)
{
    QString name, folder;
    const QString fileName = QFileInfo(filepath).fileName();
    if (!askPin(tr("Закрепить файл"), AppSettings::get().pinsNoName() ? QString() : fileName, name, folder)) return;
    if (name == fileName) name = QStringLiteral("");  // Без своего названия — имя файла
    m_db->addPin(folder, name, "file", "", filepath);
}

// Понесли файл из полоски: SmartClip не прячется, а прижимается к краям
// своего монитора (колонки — вбок, панели — вверх и вниз), чтобы не мешать
// бросить файл в программу под ним.
void MainWindow::startFileDragSqueeze()
{
    m_fileDrag = true;
    if (auto *old = static_cast<DragSqueeze *>(m_squeeze.data())) old->close();
    m_filesStrip->collapseNow();
    const int W = width(), H = height();
    QList<DragSqueeze::Piece> pcs;
    auto add = [&](const QRect &r, QPointF off, qreal sx, qreal sy, QPointF anchor) {
        if (!r.isEmpty()) pcs << DragSqueeze::Piece{r, off, sx, sy, anchor};
    };
    const QRect top = m_topPanel->geometry();
    add(top, QPointF(0, -top.bottom() * 0.62), 1.0, 0.92, QPointF(top.center().x(), 0));
    if (m_foldersBar->isVisible()) {  // Полоса папок — вверх вместе с панелью
        const QRect fold = m_foldersBar->geometry();
        add(fold, QPointF(0, -fold.bottom() * 0.62), 1.0, 0.92, QPointF(fold.center().x(), 0));
    }
    const QRect L = m_leftScroll->geometry(), Rr = m_rightScroll->geometry();
    add(L,  QPointF(-L.right() * 0.62, 0),        0.88, 1.0, QPointF(0, L.center().y()));
    add(Rr, QPointF((W - Rr.left()) * 0.62, 0),   0.88, 1.0, QPointF(W, Rr.center().y()));
    const QRect B = m_bottomPanel->geometry();
    const qreal down = (H - B.top()) * 0.55;
    add(B, QPointF(0, down), 1.0, 0.92, QPointF(B.center().x(), H));
    if (m_filesStrip->isVisible()) {
        // Полоска прижимается вплотную к полю поиска и уезжает вместе с ним
        QRect S = m_filesStrip->collapsedRect();
        if (m_importVideoBtn && m_importVideoBtn->isVisible())
            S = S.united(m_importVideoBtn->geometry()).united(m_importAudioBtn->geometry());
        add(S, QPointF(0, down + (B.top() - S.bottom()) + sc(2)), 1.0, 0.9, QPointF(S.center().x(), H));
    }
    auto *sq = new DragSqueeze(this, pcs);
    m_squeeze = sq;
    sq->squeeze();
}

void MainWindow::dropSqueeze()
{
    m_fileDrag = false;
    if (auto *sq = static_cast<DragSqueeze *>(m_squeeze.data())) {
        sq->detach();  // Окно прячется — видимость ему не возвращаем
        sq->close();
    }
}

void MainWindow::endFileDragSqueeze()
{
    m_fileDrag = false;
    // Пока несли файл, могли переключиться в другую программу — вставлять туда
    HWND fg = GetForegroundWindow();
    if (isPasteTarget(fg)) m_prevFocusHwnd = reinterpret_cast<quintptr>(fg);
    if (auto *sq = static_cast<DragSqueeze *>(m_squeeze.data())) sq->release();
}

void MainWindow::pasteText(const QString &text)
{
    // 1. Сообщаем ClipboardManager: следующее WM_CLIPBOARDUPDATE — наше, не писать в историю
    emit pasteRequested();

    // 2. Кладём текст в системный буфер обмена
    QApplication::clipboard()->setText(text);

    // 3. Прячемся и вставляем в окно, где работал пользователь
    finishPaste();
}

void MainWindow::pasteImage(const QString &filepath)
{
    // Загружаем картинку из файла
    QPixmap pixmap(filepath);
    if (pixmap.isNull()) {
        qDebug() << "pasteImage: не удалось загрузить" << filepath;
        return;
    }

    // Аналогично тексту — подавляем запись в историю
    emit pasteRequested();

    // Qt сам конвертирует QPixmap в нужный формат буфера
    QApplication::clipboard()->setPixmap(pixmap);

    finishPaste();
}

void MainWindow::pasteFile(const QString &filepath)
{
    // Кладём файл в буфер как CF_HDROP (как при копировании файла в проводнике).
    // Это позволяет вставить файл через Ctrl+V в Telegram, Discord, почту и т.д.
    QString nativePath = QDir::toNativeSeparators(filepath);
    std::wstring wPath = nativePath.toStdWString();

    // Размер буфера: SC_DROPFILES + путь (UTF-16) + двойной нуль-терминатор
    size_t pathBytes = (wPath.size() + 2) * sizeof(wchar_t); // путь + 2x '\0'
    size_t bufSize   = sizeof(SC_DROPFILES) + pathBytes;

    HGLOBAL hGlobal = GlobalAlloc(GHND, bufSize);
    if (!hGlobal) return;

    SC_DROPFILES *pDrop = static_cast<SC_DROPFILES*>(GlobalLock(hGlobal));
    pDrop->pFiles = sizeof(SC_DROPFILES); // смещение до первого пути
    pDrop->fWide  = TRUE;                 // пути в UTF-16
    pDrop->pt     = {0, 0};
    pDrop->fNC    = FALSE;

    wchar_t *pPaths = reinterpret_cast<wchar_t*>(
        reinterpret_cast<BYTE*>(pDrop) + sizeof(SC_DROPFILES));
    wmemcpy(pPaths, wPath.c_str(), wPath.size() + 1); // путь + нуль
    pPaths[wPath.size() + 1] = L'\0';                  // второй нуль = конец списка

    GlobalUnlock(hGlobal);

    emit pasteRequested();

    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        SetClipboardData(CF_HDROP, hGlobal);
        CloseClipboard();
    } else {
        GlobalFree(hGlobal);
        return;
    }

    finishPaste();
}

void MainWindow::importVideo()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Импорт видео"), "",
        tr("Видео файлы (*.mp4 *.mkv *.mov *.avi *.wmv *.webm)")
    );
    if (files.isEmpty()) return;

    // В ту папку закрепов, что открыта сейчас (во «Все» — без папки)
    for (const QString &fp : files) {
        QString name = QFileInfo(fp).baseName();
        if (!AppSettings::get().pinsNoName()) {
            bool ok = false;
            name = askText(this, tr("Закрепить видео"), tr("Название закрепа:"), name, &ok);
            if (!ok) continue;
        }
        m_db->addPin(m_currentFolder, name.trimmed(), "video", "", fp);
    }
    loadPins();
}

void MainWindow::importAudio()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Импорт аудио"), "",
        tr("Аудио файлы (*.mp3 *.wav *.ogg *.flac *.aac *.m4a)")
    );
    if (files.isEmpty()) return;

    // В ту папку закрепов, что открыта сейчас (во «Все» — без папки)
    for (const QString &fp : files) {
        QString name = QFileInfo(fp).baseName();
        if (!AppSettings::get().pinsNoName()) {
            bool ok = false;
            name = askText(this, tr("Закрепить аудио"), tr("Название закрепа:"), name, &ok);
            if (!ok) continue;
        }
        m_db->addPin(m_currentFolder, name.trimmed(), "audio", "", fp);
    }
    loadPins();
}

void MainWindow::showTextContextMenu(int id, const QString &content,
                                     QLabel *card, const QPoint &globalPos)
{
    RoundedMenu menu(this);

    menu.addAction(tr("📝  Редактировать"), [this, id, card]() { editHistoryText(id, card); });
    menu.addAction(tr("📌  Закрепить"), [this, content]() { pinText(content); });
    menu.addSeparator();
    menu.addAction(tr("🗑  Удалить"),   [this, id, card]() { deleteCard(id, card); });

    menu.exec(globalPos);
}

void MainWindow::showImageContextMenu(int id, const QString &filepath,
                                      QLabel *card, const QPoint &globalPos)
{
    RoundedMenu menu(this);

    menu.addAction(tr("🔍  Просмотреть"), [this, filepath, card]() { showImageViewer(filepath, card); });
    menu.addAction(tr("🗗  Открыть в окне"), [this, filepath]() { openPhotoWindow(filepath); });
    menu.addSeparator();
    menu.addAction(tr("📌  Закрепить"),   [this, filepath]() { pinImage(filepath); });
    menu.addSeparator();
    menu.addAction(tr("🗑  Удалить"),     [this, id, card]() { deleteCard(id, card); });

    menu.exec(globalPos);
}

// ─── Рука: несём фото (и файлы) ──────────────────────────────────────────────
// Зажал левой фото — оно «вытягивается» и летит в руку (Carry.h). Просто
// кликнул — вставка, как раньше, только по отпусканию: иначе не отличить
// клик от «зажал».
bool MainWindow::pickEvent(ClickableCard *card, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton || m_carry->active()) return false;
        m_pickPressCard = card;
        m_pickPressPos = me->globalPosition().toPoint();
        m_pickView = me->modifiers() & Qt::ControlModifier;
        if (m_pickView) return true;  // Ctrl+клик — просмотр (по отпусканию)
        if (!m_pickHoldTimer) {
            m_pickHoldTimer = new QTimer(this);
            m_pickHoldTimer->setSingleShot(true);
            m_pickHoldTimer->setInterval(220);
            connect(m_pickHoldTimer, &QTimer::timeout, this, [this]() {
                if (m_pickPressCard && CarryUi::leftHeld()) carryPhoto(m_pickPressCard);
            });
        }
        m_pickHoldTimer->start();
        return true;
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_pickPressCard == card && !m_pickView && (me->buttons() & Qt::LeftButton)
            && (me->globalPosition().toPoint() - m_pickPressPos).manhattanLength() > QApplication::startDragDistance()) {
            carryPhoto(card);  // Потянул — тоже берём, не дожидаясь
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && m_pickPressCard == card) {
            if (m_pickHoldTimer) m_pickHoldTimer->stop();
            m_pickPressCard = nullptr;
            if (!card->rect().contains(me->position().toPoint())) return true;
            if (m_pickView) viewCard(card);
            else emit card->clicked();
            return true;
        }
        break;
    }
    default: break;
    }
    return false;
}

// Ctrl+клик: фото — просмотр (как «Просмотреть» в меню), видео — большой плеер
void MainWindow::viewCard(ClickableCard *card)
{
    m_pickView = false;
    const QString fp = card->property("filepath").toString();
    if (fp.isEmpty()) return;
    for (QObject *o : card->children())
        if (auto *surface = dynamic_cast<Media::VideoSurface *>(o)) {
            m_videoViewer->open(card, surface, fp);
            return;
        }
    showImageViewer(fp, card);
}

void MainWindow::carryPhoto(ClickableCard *card)
{
    if (m_pickHoldTimer) m_pickHoldTimer->stop();
    m_pickPressCard = nullptr;
    CarryItem it;
    QRectF from;
    if (card && photoCarryItem(card, it, from)) m_carry->pickUp(it, from);
}

// Фото «вытягивается» ровно такого размера, как в карточке
bool MainWindow::photoCarryItem(ClickableCard *card, CarryItem &it, QRectF &from) const
{
    const QString fp = card->property("filepath").toString();
    if (fp.isEmpty() || !QFileInfo::exists(fp)) return false;
    QPixmap thumb = card->pixmap();
    QRectF cr(QPointF(card->mapToGlobal(QPoint(0, 0))), QSizeF(card->size()));
    // Видео: в руку — кадр-превью (своим виджетом, у самой карточки картинки нет)
    Media::VideoSurface *surface = nullptr;
    for (QObject *o : card->children())
        if ((surface = dynamic_cast<Media::VideoSurface *>(o))) break;
    if (surface) {
        thumb = surface->grab();
        cr = QRectF(QPointF(surface->mapToGlobal(QPoint(0, 0))), QSizeF(surface->size()));
    }
    it = CarryItem{fp, thumb, QFileInfo(fp).fileName()};
    QSizeF sz = it.thumb.isNull() ? cr.size() * 0.8 : QSizeF(it.thumb.size() / it.thumb.devicePixelRatio());
    if (sz.width() > cr.width() || sz.height() > cr.height()) sz.scale(cr.size(), Qt::KeepAspectRatio);
    from = QRectF(cr.center() - QPointF(sz.width() / 2, sz.height() / 2), sz);
    return true;
}

// Фото в колонке под курсором (для руки: правым — добавить)
bool MainWindow::photoAt(const QPoint &g, CarryItem &it, QRectF &from) const
{
    for (QScrollArea *sa : {m_leftScroll, m_rightScroll}) {
        if (!sa || !sa->isVisible() || !sa->widget()) continue;
        if (!QRect(sa->viewport()->mapToGlobal(QPoint(0, 0)), sa->viewport()->size()).contains(g)) continue;
        for (ClickableCard *c : sa->widget()->findChildren<ClickableCard *>())
            if (c->isVisible() && c->property("pickable").toBool()
                && QRect(c->mapToGlobal(QPoint(0, 0)), c->size()).contains(g))
                return photoCarryItem(c, it, from);
    }
    return false;
}

// Курсор над SmartClip: колонки, полоска файлов и полоса между ними на её
// высоте — чтобы от фото дотянуться до файлов, не «бросив» всё в программу
bool MainWindow::carryZone(const QPoint &g) const
{
    if (!isVisible()) return false;
    const QPoint p = mapFromGlobal(g);
    const int m = sc(24);
    const QRect L = m_leftScroll->geometry(), R = m_rightScroll->geometry();
    if (L.adjusted(-m, -m, m, m).contains(p) || R.adjusted(-m, -m, m, m).contains(p)) return true;
    if (m_filesStrip && m_filesStrip->isVisible()) {
        const QRect S = m_filesStrip->visibleRect();
        const QRect band(L.left(), S.top() - m, R.right() - L.left(), height() - S.top() + m);
        if (S.adjusted(-m, -m, m, m).contains(p) || band.contains(p)) return true;
    }
    return false;
}

// Что в руке — бледнеет на своём месте
void MainWindow::syncCarryMarks()
{
    for (QScrollArea *sa : {m_leftScroll, m_rightScroll})
        if (sa && sa->widget())
            for (ClickableCard *c : sa->widget()->findChildren<ClickableCard *>())
                if (c->property("pickable").toBool())
                    c->setPicked(m_carry && m_carry->has(c->property("filepath").toString()));
    if (m_filesStrip) m_filesStrip->update();
}

// Фото в отдельном обычном окне. SmartClip уходит, чтобы окно было видно
// (иначе его закрывали бы колонки SmartClip — они всегда поверх всего).
void MainWindow::openPhotoWindow(const QString &filepath)
{
    auto *w = new PhotoWindow(filepath);
    hideAnimated();
    w->show();
    w->raise();
    w->activateWindow();
}

void MainWindow::pinText(const QString &content)
{
    QString name, folder;
    if (!askPin(tr("Закрепить текст"), AppSettings::get().pinsNoName() ? QString() : content.trimmed().left(40),
                name, folder))
        return;
    m_db->addPin(folder, name, "text", content, "");
}

void MainWindow::pinImage(const QString &filepath)
{
    QString name, folder;
    if (!askPin(tr("Закрепить скриншот"), QString(), name, folder)) return;
    m_db->addPin(folder, name, "image", "", filepath);
}

// deleteCard — удаляет карточку истории из БД и из UI.
void MainWindow::deleteCard(int id, QLabel *card)
{
    // Удаляем из БД
    m_db->deleteHistory(id);
    m_searchIndexValid = false;

    // Удаляем виджет из лейаута и памяти.
    // deleteLater() — безопасно, даже если на карточке есть pending события.
    if (card) {
        // Определяем в каком лейауте лежит карточка и убираем оттуда
        QVBoxLayout *layout = nullptr;
        if (m_leftLayout->indexOf(card) != -1)
            layout = m_leftLayout;
        else if (m_rightLayout->indexOf(card) != -1)
            layout = m_rightLayout;

        if (layout)
            layout->removeWidget(card);

        // Запись ушла из БД — следующая порция начинается на одну раньше,
        // иначе при подгрузке одна запись пропустится
        if (layout == m_leftLayout && !m_searching && m_textOffset > 0) --m_textOffset;
        if (layout == m_rightLayout && m_mediaOffset > 0)              --m_mediaOffset;

        card->deleteLater();
    }
}

void MainWindow::showImageViewer(const QString &filepath, QWidget *source)
{
    m_imageViewer->showImage(filepath, source);
}

// confirmTwice — двойное подтверждение для опасных действий.
// Первый диалог — обычный, второй — с красным предупреждением (danger=true).
// Требует чтобы пользователь нажал «ОК» дважды перед удалением.
bool MainWindow::confirmTwice(const QString &title,
                              const QString &msg1, const QString &msg2)
{
    if (!confirmDialog(this, title, msg1, false)) return false;
    return confirmDialog(this, title, msg2, true);
}

void MainWindow::showClearMenu()
{
    RoundedMenu menu(this);

    // В режиме истории показываем опции очистки истории
    if (m_viewMode == ViewMode::History) {
        int totalTexts  = m_db->countHistory("text");
        int totalImages = m_db->countHistory("image");

        menu.addAction(
            tr("Очистить тексты  (%1)").arg(totalTexts),
            [this]() { clearHistory("text"); }
        );
        menu.addAction(
            tr("Очистить скриншоты  (%1)").arg(totalImages),
            [this]() { clearHistory("image"); }
        );
        menu.addSeparator();
        menu.addAction(
            tr("Очистить всю историю  (%1)").arg(totalTexts + totalImages),
            [this]() { clearHistory(""); }
        );
        menu.addSeparator();
        menu.addAction(tr("Очистить по приложению..."), [this]() { clearHistoryByApp(); });
    } else {
        menu.addAction(tr("(Удаление закрепов — через ПКМ по карточке)"))->setEnabled(false);
    }

    menu.exec(m_clearBtn->mapToGlobal(QPoint(0, m_clearBtn->height() + 6)));
}

void MainWindow::clearHistory(const QString &type)
{
    QString what = type.isEmpty() ? tr("всю историю")
                 : (type == "text" ? tr("историю текстов") : tr("историю скриншотов"));
    int count = m_db->countHistory(type);

    if (!confirmTwice(
        tr("Очистить %1").arg(what),
        tr("Удалить %1?\nЭто действие нельзя отменить.").arg(what),
        tr("Вы точно уверены?\nБудет безвозвратно удалено %1 записей.").arg(count)
    )) return;

    m_db->deleteAllHistory(type);
    m_historyDirty = true;
    loadHistory();
}

void MainWindow::clearHistoryByApp()
{
    QStringList apps = m_db->getAppNames();
    if (apps.isEmpty()) {
        confirmDialog(this, tr("Очистить по приложению"),
                      tr("История пуста или приложения не определены.\nДанные об источнике появятся у новых записей."), false);
        return;
    }

    bool ok = false;
    QString app = askItem(this, tr("Очистить по приложению"),
                          tr("Выберите приложение:"), apps, 0, &ok);
    if (!ok || app.isEmpty()) return;

    int count = m_db->countHistoryByApp(app);

    if (!confirmTwice(
        tr("Очистить историю %1").arg(app),
        tr("Удалить все записи из %1?\nЭто действие нельзя отменить.").arg(app),
        tr("Вы точно уверены?\nБудет удалено %1 записей из %2.").arg(count).arg(app)
    )) return;

    m_db->deleteHistoryByApp(app);
    m_historyDirty = true;
    loadHistory();
}

QString MainWindow::sortLabel() const
{
    switch (m_sortOrder) {
        case SortOrder::DateDesc: return tr("Новые сначала");
        case SortOrder::DateAsc:  return tr("Старые сначала");
        case SortOrder::NameAsc:  return tr("По имени А→Я");
        case SortOrder::NameDesc: return tr("По имени Я→А");
    }
    return "↕";
}

void MainWindow::showSortMenu()
{
    RoundedMenu menu(this);

    // Пункт с галочкой у активного варианта
    auto addSortAction = [&](const QString &label, SortOrder order) {
        QString text = (m_sortOrder == order) ? "✓  " + label : "    " + label;
        menu.addAction(text, [this, order]() { applySortOrder(order); });
    };

    addSortAction(tr("Новые сначала"),  SortOrder::DateDesc);
    addSortAction(tr("Старые сначала"), SortOrder::DateAsc);

    if (m_viewMode == ViewMode::Pins) {
        menu.addSeparator();
        addSortAction(tr("По имени А→Я"), SortOrder::NameAsc);
        addSortAction(tr("По имени Я→А"), SortOrder::NameDesc);
    }

    // Показываем меню под кнопкой сортировки
    menu.exec(m_sortBtn->mapToGlobal(QPoint(0, m_sortBtn->height() + 6)));
}

void MainWindow::applySortOrder(SortOrder order)
{
    m_sortOrder = order;

    // Обновляем текст кнопки сортировки
    if (m_sortBtn)
        m_sortBtn->setText(sortLabel());

    // Перезагружаем текущий вид с новой сортировкой
    if (m_viewMode == ViewMode::History)
        loadHistory();
    else
        loadPins();
}

// ─── loadFolderBar ────────────────────────────────────────────────────────────
// Перестраивает панель папок (FoldersBar) под текущий уровень навигации.
//
// Режим «корень» (m_currentFolder == ""):
//   [Все] [Работа] [Личное] [Проекты]  [🎬 Видео] [🎵 Аудио] [+]
//
// Режим «внутри папки» (m_currentFolder == "Работа"):
//   [←] [📁 Работа] [Клиент1] [Клиент2]  [🎬 Видео] [🎵 Аудио] [+]
//
// Кнопки создаются заново при каждом вызове (recreate-on-demand).
void MainWindow::loadFolderBar()
{
    // Очищаем все виджеты полосы папок
    clearLayout(m_foldersLayout);

    const int FH = 28;
    QFont fbarFont = font();
    fbarFont.setPixelSize(12);

    if (!m_currentFolder.isEmpty()) {
        // ── Внутри папки: кнопка "←" + хлебная крошка ──────────────────────
        SmartButton *backBtn = new SmartButton(QString(), m_foldersBar);
        backBtn->setIconGlyph(QChar(0xE72B), 12);  // Стрелка «назад»
        backBtn->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,150), 5);
        backBtn->setFixedSize(FH, FH);
        backBtn->setFont(fbarFont);
        backBtn->setToolTip(tr("Назад"));
        applyBtnShadow(backBtn);
        connect(backBtn, &QPushButton::clicked, this, [this]() {
            int sep = m_currentFolder.lastIndexOf('/');
            goToFolder((sep >= 0) ? m_currentFolder.left(sep) : QString());
        });
        m_foldersLayout->addWidget(backBtn);

        // Глубоко в папках — «домой»: сразу к корню закрепов
        if (m_currentFolder.contains('/')) {
            SmartButton *homeBtn = new SmartButton(QString(), m_foldersBar);
            homeBtn->setIconGlyph(QChar(0xE80F), 12);  // Домик
            homeBtn->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,150), 5);
            homeBtn->setFixedSize(FH, FH);
            homeBtn->setToolTip(tr("Ко всем закрепам"));
            applyBtnShadow(homeBtn);
            connect(homeBtn, &QPushButton::clicked, this, [this]() { goToFolder(QString()); });
            m_foldersLayout->addWidget(homeBtn);
        }

        // Хлебная крошка — SmartButton в активном стиле (показывает текущий путь)
        QString leafName = m_currentFolder.mid(m_currentFolder.lastIndexOf('/') + 1);
        SmartButton *breadcrumb = new SmartButton("📁 " + QString(leafName).replace('&', "&&"), m_foldersBar);
        breadcrumb->setFixedHeight(FH);
        breadcrumb->setFont(fbarFont);
        breadcrumb->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,120), 5);
        breadcrumb->setActiveBtnStyle(QColor(48,33,6,245), QColor(0xFF,0xDD,0x88));
        breadcrumb->setActiveState(true);
        applyBtnShadow(breadcrumb);
        connect(breadcrumb, &QPushButton::clicked, this, [this]() {
            loadFolderBar(); loadPins();
        });
        m_foldersLayout->addWidget(breadcrumb);

    } else {
        // ── Корень: кнопка "Все" ───────────────────────────────────────────────
        SmartButton *allBtn = new SmartButton(tr("Все"), m_foldersBar);
        allBtn->setFixedHeight(FH);
        allBtn->setFont(fbarFont);
        allBtn->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,150), 5);
        allBtn->setActiveBtnStyle(QColor(48,33,6,245), QColor(0xFF,0xDD,0x88));
        allBtn->setActiveState(true);
        applyBtnShadow(allBtn);
        connect(allBtn, &QPushButton::clicked, this, [this]() { goToFolder(QString()); });
        m_foldersLayout->addWidget(allBtn);
    }

    // ── Подпапки текущего уровня ────────────────────────────────────────────────
    QList<FolderItem> folders = m_db->getFolders(m_currentFolder);
    for (const FolderItem &folder : folders) {
        // Показываем только листовое имя (без родительского пути)
        QString displayName = folder.name.mid(folder.name.lastIndexOf('/') + 1);
        QString fullName    = folder.name;
        // Закреплённые папки помечаем булавкой 📌
        QString btnLabel = folder.pinned ? ("📌 " + displayName) : displayName;
        btnLabel.replace('&', "&&");  // Иначе «Tom & Jerry» показывалось как «Tom  Jerry»

        SmartButton *btn = new SmartButton(btnLabel, m_foldersBar);
        btn->setFixedHeight(FH);
        btn->setFont(fbarFont);
        btn->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,150), 5);
        btn->setActiveBtnStyle(QColor(48,33,6,245), QColor(0xFF,0xDD,0x88));
        btn->setActiveState(folder.name == m_currentFolder);
        applyBtnShadow(btn);

        // Клик — навигация вглубь папки
        connect(btn, &QPushButton::clicked, this, [this, fullName]() { goToFolder(fullName); });

        // ПКМ: Закрепить / Переименовать / Создать подпапку / Удалить
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QWidget::customContextMenuRequested, this,
                [this, fullName, displayName, btn, isPinned = folder.pinned](const QPoint &pos) {
            RoundedMenu menu(this);

            // Закрепить / открепить папку (всегда первая)
            QString pinLabel = isPinned ? tr("📌  Открепить папку") : tr("📌  Закрепить первой");
            menu.addAction(pinLabel, [this, fullName, isPinned]() {
                m_db->setFolderPinned(fullName, !isPinned);
                loadFolderBar();
            });
            menu.addSeparator();

            menu.addAction(tr("✏️  Переименовать"), [this, fullName, displayName]() {
                bool ok = false;
                QString newLeaf = askText(this, tr("Переименовать папку"), tr("Новое название:"), displayName, &ok);
                // '/' — разделитель вложенности, в имени папки его быть не может
                newLeaf = newLeaf.trimmed().replace('/', '-');
                if (!ok || newLeaf.isEmpty() || newLeaf == displayName) return;
                // Строим новый полный путь (меняем только листовое имя)
                int sep = fullName.lastIndexOf('/');
                QString newFullName = (sep >= 0)
                    ? fullName.left(sep + 1) + newLeaf
                    : newLeaf;
                if (!m_db->renameFolder(fullName, newFullName)) {
                    // Раньше ошибка молча игнорировалась, а текущая папка
                    // «переключалась» на несуществующую
                    confirmDialog(this, tr("Переименовать папку"),
                                  tr("Папка «%1» уже существует.").arg(newLeaf), false);
                    return;
                }
                if (m_currentFolder == fullName)
                    m_currentFolder = newFullName;
                else if (m_currentFolder.startsWith(fullName + "/"))
                    m_currentFolder = newFullName + m_currentFolder.mid(fullName.length());
                loadFolderBar();
                loadPins();
            });

            menu.addAction(tr("📁  Создать подпапку"), [this, fullName]() {
                bool ok = false;
                QString name = askText(this, tr("Новая подпапка"), tr("Название подпапки:"), "", &ok);
                name = name.trimmed().replace('/', '-');
                if (ok && !name.isEmpty()) {
                    m_db->addFolder(name, fullName);
                    loadFolderBar();
                }
            });

            menu.addSeparator();
            menu.addAction(tr("🗑  Удалить папку"), [this, fullName, displayName]() {
                // Раньше папка удалялась сразу, без вопроса
                if (!confirmDialog(this, tr("Удалить папку"),
                        tr("Удалить папку «%1» и все её подпапки?\n"
                           "Закрепы из неё не удалятся — они переместятся во «Все».")
                            .arg(displayName), true))
                    return;
                m_db->deleteFolder(fullName);
                if (m_currentFolder == fullName ||
                    m_currentFolder.startsWith(fullName + "/"))
                    m_currentFolder = "";
                loadFolderBar();
                loadPins();
            });

            menu.exec(btn->mapToGlobal(pos));
        });

        m_foldersLayout->addWidget(btn);
    }

    // (Импорт видео и аудио — кнопки по бокам полоски файлов внизу)

    // Кнопка "+" — квадратная, как backBtn
    SmartButton *addBtn = new SmartButton(QString(), m_foldersBar);
    addBtn->setIconGlyph(QChar(0xE710), 12);  // Плюс
    addBtn->setFixedSize(FH, FH);
    addBtn->setFont(fbarFont);
    addBtn->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,150), 5);
    addBtn->setToolTip(m_currentFolder.isEmpty() ? tr("Новая папка") : tr("Новая подпапка"));
    applyBtnShadow(addBtn);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        QString title = m_currentFolder.isEmpty() ? tr("Новая папка") : tr("Новая подпапка");
        QString name  = askText(this, title, tr("Название:"), "", &ok);
        name = name.trimmed().replace('/', '-');  // '/' — разделитель вложенности
        if (ok && !name.isEmpty()) {
            m_db->addFolder(name, m_currentFolder);
            loadFolderBar();
        }
    });
    m_foldersLayout->addWidget(addBtn);
    m_foldersLayout->addStretch();
}

// ─── Закрепить: название и папка в одном окне ───────────────────────────────
// Раньше было два окна подряд: «Название», потом «Папка». Пока первое
// закрывалось, Windows на миг отдавала фокус другой программе, SmartClip
// принимал это за «клик мимо» и тут же закрывал второе окно — отмену папки
// считали отменой закрепа, и он не сохранялся (а папку будто не спрашивали).
// Одно окно — закрыть «посередине» нечему.
// Что спрашивать — по настройкам: «не спрашивать название» / «не спрашивать
// папку»; папок нет — выбирать не из чего. Не о чем спросить — окна нет вовсе.
bool MainWindow::askPin(const QString &title, const QString &defaultName, QString &name, QString &folder)
{
    name = defaultName;
    folder = QString();
    const bool askName = !AppSettings::get().pinsNoName();
    const QList<FolderItem> folders = AppSettings::get().pinsNoFolder() ? QList<FolderItem>() : m_db->getAllFolders();
    const bool askFolderToo = !folders.isEmpty();
    if (!askName && !askFolderToo) return true;

    AppDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(360);
    auto *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(8);
    const QString lblCss = "color: #c0c0d0; font-size: 13px; background: transparent;";

    QLineEdit *edit = nullptr;
    if (askName) {
        auto *lbl = new QLabel(tr("Название:"), dlg.body());
        lbl->setStyleSheet(lblCss);
        edit = new QLineEdit(defaultName, dlg.body());
        edit->setStyleSheet(
            "background: rgba(28,28,44,245); color: #f0f0f0;"
            "border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
            "padding: 5px 10px; font-size: 13px;"
            "selection-background-color: rgba(100,150,255,150);");
        edit->selectAll();
        vl->addWidget(lbl);
        vl->addWidget(edit);
    }

    QComboBox *combo = nullptr;
    if (askFolderToo) {
        auto *lbl = new QLabel(tr("Папка:"), dlg.body());
        lbl->setStyleSheet(lblCss);
        combo = new QComboBox(dlg.body());
        combo->addItem(tr("Без папки"), QString());
        int current = 0;
        for (const FolderItem &f : folders) {
            combo->addItem(f.name, f.name);
            if (f.name == m_currentFolder) current = combo->count() - 1;
        }
        combo->setCurrentIndex(current);
        combo->setStyleSheet(
            "QComboBox { background: rgba(28,28,44,245); color: #f0f0f0;"
            "  border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
            "  padding: 5px 10px; font-size: 13px; }"
            "QComboBox:hover { border: 1px solid rgba(255,255,255,90); }"
            "QComboBox::drop-down { border: none; width: 24px; }"
            "QComboBox::down-arrow { image: none; width: 0; }"
            "QComboBox QAbstractItemView {"
            "  background: rgba(22,22,36,252); color: #e0e0e0;"
            "  border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
            "  selection-background-color: rgba(255,255,255,18);"
            "  outline: none; padding: 2px; }");
        if (askName) vl->addSpacing(4);
        vl->addWidget(lbl);
        vl->addWidget(combo);
    }

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto *cancelBtn = new QPushButton(QObject::tr("Отмена"), dlg.body());
    auto *okBtn     = new QPushButton(tr("📌  Закрепить"), dlg.body());
    okBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
        "border:1px solid rgba(120,160,255,160);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:70px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    cancelBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(255,255,255,200);"
        "border:1px solid rgba(255,255,255,55);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:70px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    okBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setDefault(true);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    vl->addSpacing(6);
    vl->addLayout(btnRow);
    QObject::connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (edit) QObject::connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted) return false;
    if (edit) name = edit->text().trimmed();
    if (combo) folder = combo->currentData().toString();
    return true;
}

QString MainWindow::askFolder(bool &cancelled)
{
    // getAllFolders() возвращает все папки включая подпапки (полные пути)
    QList<FolderItem> folders = m_db->getAllFolders();

    if (folders.isEmpty()) {
        cancelled = false;
        return ""; // папок нет — кладём без папки
    }

    QStringList options;
    options << tr("Без папки");
    for (const FolderItem &f : folders)
        options << f.name;

    int defaultIdx = 0;
    if (!m_currentFolder.isEmpty()) {
        int idx = options.indexOf(m_currentFolder);
        if (idx >= 0) defaultIdx = idx;
    }

    bool ok = false;
    QString selected = askItem(this, tr("Выбрать папку"),
                               tr("В какую папку закрепить?"), options, defaultIdx, &ok);

    cancelled = !ok;
    if (!ok || selected == tr("Без папки"))
        return "";
    return selected;
}

// Открыть папку закрепов: старые карточки уезжают, новые въезжают
// (вглубь — справа налево, назад/наверх — слева направо)
void MainWindow::goToFolder(const QString &folder)
{
    const QString old = m_currentFolder;
    if (folder == old) { loadFolderBar(); loadPins(); return; }
    auto depth = [](const QString &f) { return f.isEmpty() ? 0 : int(f.count('/')) + 1; };
    int dir = depth(folder) > depth(old) ? 1 : -1;
    if (depth(folder) == depth(old)) {
        // Соседние папки — в какую сторону по порядку кнопок
        const int sep = folder.lastIndexOf('/');
        const QList<FolderItem> sibs = m_db->getFolders(sep >= 0 ? folder.left(sep) : QString());
        int iOld = -1, iNew = -1;
        for (int i = 0; i < sibs.size(); ++i) {
            if (sibs[i].name == old) iOld = i;
            if (sibs[i].name == folder) iNew = i;
        }
        dir = iNew >= iOld ? 1 : -1;
    }

    // Колонки уезжают в разные стороны, зеркально: левая — влево, правая — вправо
    QList<FolderSlide *> slides;
    if (isVisible() && Anim::enabled(Anim::Kind::Tabs)) {
        slides << new FolderSlide(m_leftScroll, m_leftScroll->grab(), dir);
        slides << new FolderSlide(m_rightScroll, m_rightScroll->grab(), -dir);
    }

    m_currentFolder = folder;
    loadFolderBar();
    loadPins();

    if (!slides.isEmpty()) {
        // Новые карточки Qt показывает «потом» (на следующем шаге) — показываем
        // сразу, иначе в снимок новой папки попадали одни фантомы
        for (QVBoxLayout *lay : {m_leftLayout, m_rightLayout})
            for (int i = 0; i < lay->count(); ++i)
                if (QWidget *w = lay->itemAt(i)->widget())
                    if (!w->isVisible() && !w->testAttribute(Qt::WA_WState_ExplicitShowHide))
                        w->show();
        for (int i = 0; i < 3; ++i) QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        for (FolderSlide *sl : slides) sl->play();
        // Полоса папок, полоска файлов и кнопки — поверх анимации
        m_foldersBar->raise();
        if (m_filesStrip) m_filesStrip->raise();
        placeImportButtons();
    }
}

// Кнопки импорта — слева и справа от свёрнутой полоски файлов, только в Закрепах
void MainWindow::placeImportButtons()
{
    if (!m_importVideoBtn || !m_importAudioBtn) return;
    const bool show = m_viewMode == ViewMode::Pins && m_filesStrip && m_filesStrip->isVisible();
    m_importVideoBtn->setVisible(show);
    m_importAudioBtn->setVisible(show);
    if (!show) return;
    const QRect r = m_filesStrip->collapsedRect();
    const int sz = m_importVideoBtn->width(), gap = sc(8);
    const int y = r.center().y() - sz / 2;
    m_importVideoBtn->move(r.left() - gap - sz, y);
    m_importAudioBtn->move(r.right() + 1 + gap, y);
    m_importVideoBtn->raise();
    m_importAudioBtn->raise();
}

// Полоса папок: от левого края до правой колонки, сразу под верхней панелью
void MainWindow::placeFoldersBar()
{
    if (!m_foldersBar || !m_rightScroll || !m_topPanel) return;
    const int top = m_topPanel->geometry().bottom() + 1;
    m_foldersBar->setGeometry(0, top, qMax(sc(200), m_rightScroll->geometry().left() - sc(8)), sc(BASE_FOLD));
}

void MainWindow::switchView(ViewMode mode)
{
    // Уходим из Истории в Закрепы — запоминаем, где остановились в Истории
    // (сколько карточек подгружено и где прокрутка), чтобы вернуть туда же
    if (m_viewMode == ViewMode::History && mode == ViewMode::Pins) {
        m_histPos = { m_leftScroll->verticalScrollBar()->value(),
                      m_rightScroll->verticalScrollBar()->value(),
                      m_textOffset, m_mediaOffset, true };
    }
    m_viewMode = mode;

    // Обновляем активную вкладку через SmartButton::setActiveState
    m_historyBtn->setActiveState(mode == ViewMode::History);
    m_pinsBtn->setActiveState(   mode == ViewMode::Pins);
    // «Пилюля» переезжает на новую вкладку
    if (m_viewPill)
        static_cast<ViewPill*>(m_viewPill)->moveTo(mode == ViewMode::History ? m_historyBtn : m_pinsBtn, true);

    // Меняем плейсхолдер и сбрасываем поиск при смене режима
    if (m_searchEdit) {
        m_searchEdit->clear();
        m_searchEdit->setPlaceholderText(
            mode == ViewMode::History ? tr("Поиск по тексту...") : tr("Поиск по названию...")
        );
    }

    // Кнопка мультивыбора видна только в Закрепах
    if (m_editBtn) m_editBtn->setVisible(mode == ViewMode::Pins);

    // При уходе из Закрепов сбрасываем режим выбора
    if (mode != ViewMode::Pins && m_editMode) {
        m_editMode = false;
        m_selectedPinIds.clear();
        if (m_editBtn) m_editBtn->setActiveState(false);
    }

    // Полоска внизу: в Истории — свежие файлы, в Закрепах — закреплённые
    if (m_filesStrip) m_filesStrip->setPinsMode(mode == ViewMode::Pins);
    placeImportButtons();

    if (mode == ViewMode::History) {
        m_foldersBar->hide();
        static_cast<ColumnScroll *>(m_leftScroll)->setViewportMargins(0, 0, 0, 0);
        loadHistory();
        restoreHistoryPos();
    } else {
        // Папку НЕ сбрасываем: вернулся в Закрепы — ты там же, где был
        static_cast<ColumnScroll *>(m_leftScroll)->setViewportMargins(0, sc(BASE_FOLD), 0, 0);
        placeFoldersBar();
        m_foldersBar->show();
        m_foldersBar->raise();
        loadFolderBar();
        loadPins();
        // Закрепы — всегда с начала (раньше оставалась прокрутка Истории,
        // и Закрепы открывались сразу в самом низу)
        m_leftScroller->jumpTo(0);
        m_rightScroller->jumpTo(0);
    }
}

// Вернуться в Истории туда, где остановились перед переходом в Закрепы:
// догружаем столько же карточек и ставим прокрутку на прежнее место
void MainWindow::restoreHistoryPos()
{
    if (!m_histPos.valid) return;
    const HistoryPos pos = m_histPos;
    m_histPos.valid = false;
    while (!m_textsDone && m_textOffset < pos.texts) appendTexts();
    while (!m_mediaDone && m_mediaOffset < pos.media) appendMedia();
    // Карточки раскладываются отложенно — раскладываем сейчас, иначе полоса
    // прокрутки ещё «короткая» и позиция обрежется
    for (QVBoxLayout *lay : {m_leftLayout, m_rightLayout})
        for (int i = 0; i < lay->count(); ++i)
            if (QWidget *w = lay->itemAt(i)->widget())
                if (!w->testAttribute(Qt::WA_WState_ExplicitShowHide)) w->show();
    for (int i = 0; i < 3; ++i)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    m_leftScroller->jumpTo(pos.left);
    m_rightScroller->jumpTo(pos.right);
}

// toggleEditMode — включает/выключает режим мультивыбора закрепов.
// В режиме выбора на каждой карточке ClickableCard появляется чекбокс.
// Клик по карточке добавляет/убирает её из m_selectedPinIds.
void MainWindow::toggleEditMode()
{
    m_editMode = !m_editMode;
    m_selectedPinIds.clear();
    if (m_editBtn) m_editBtn->setActiveState(m_editMode);

    auto updateLayout = [&](QVBoxLayout *layout) {
        for (int i = 0; i < layout->count(); ++i) {
            if (auto *card = qobject_cast<ClickableCard*>(layout->itemAt(i)->widget())) {
                card->setEditMode(m_editMode);
                card->setSelected(false);
            }
        }
    };
    updateLayout(m_leftLayout);
    updateLayout(m_rightLayout);
}

void MainWindow::showPinBatchContextMenu(const QPoint &globalPos)
{
    int n = m_selectedPinIds.count();
    RoundedMenu menu(this);

    QList<FolderItem> folders = m_db->getAllFolders();
    if (!folders.isEmpty()) {
        QMenu *moveMenu = menu.addMenu(tr("📁  Переместить в папку (%1)").arg(n));
        moveMenu->addAction(tr("Без папки"), [this]() {
            for (int id : std::as_const(m_selectedPinIds))
                m_db->movePinToFolder(id, "");
            m_selectedPinIds.clear();
            loadFolderBar();
            loadPins();
        });
        moveMenu->addSeparator();
        for (const FolderItem &f : folders) {
            QString fName = f.name;
            moveMenu->addAction(fName, [this, fName]() {
                for (int id : std::as_const(m_selectedPinIds))
                    m_db->movePinToFolder(id, fName);
                m_selectedPinIds.clear();
                loadFolderBar();
                loadPins();
            });
        }
        menu.addSeparator();
    }

    menu.addAction(tr("🗑  Удалить выбранные (%1)").arg(n), [this, n]() {
        if (!confirmDialog(this, tr("Удалить закрепы"),
                           tr("Удалить выбранные закрепы (%1)?\nЭто действие нельзя отменить.").arg(n),
                           true))
            return;
        for (int id : std::as_const(m_selectedPinIds))
            m_db->deletePin(id);
        m_selectedPinIds.clear();
        loadPins();
    });

    menu.exec(globalPos);
}

void MainWindow::loadPins(bool allFolders)
{
    // Карточки сейчас будут удалены — перетаскивание (если шло) прерываем
    stopCardDrag();

    // Отменяем незавершённую ленивую загрузку картинок из прошлого вызова
    ++m_loadGeneration;
    m_pendingImages.clear();

    // Чистим колонки
    clearLayout(m_leftLayout);
    clearLayout(m_rightLayout);

    // allFolders=true используется при поиске, чтобы найти закрепы из любой папки
    QList<PinItem> pins = allFolders ? m_db->getAllPins() : m_db->getPins(m_currentFolder);

    // Сортировка
    switch (m_sortOrder) {
        case SortOrder::DateAsc:
            std::reverse(pins.begin(), pins.end()); // из БД пришло по дате DESC, разворачиваем
            break;
        case SortOrder::NameAsc:
            std::sort(pins.begin(), pins.end(), [](const PinItem &a, const PinItem &b) {
                return a.name.toLower() < b.name.toLower();
            });
            break;
        case SortOrder::NameDesc:
            std::sort(pins.begin(), pins.end(), [](const PinItem &a, const PinItem &b) {
                return a.name.toLower() > b.name.toLower();
            });
            break;
        default: break; // DateDesc — уже правильный порядок из БД
    }

    // Закреплённые файлы — не в колонках, а в полоске внизу
    refreshPinnedFiles(allFolders);

    for (const PinItem &pin : pins) {
        if (pin.type == "text") {
            // ── Текстовый закреп ───────────────────────────────────────────────
            ClickableCard *card = new ClickableCard();
            applyCardShadow(card);
            card->setWordWrap(true);
            card->setMaximumWidth(sc(BASE_LEFT) - 36);
            card->setCardStyle(QColor(42, 30, 8, 245));
            card->setStyleSheet(QString("color: #f0e0c0; padding: %1; font-size: %2;").arg(spx(8), spx(12)));

            // Название закрепа жирным + превью контента
            QString preview = pin.content.trimmed().left(200);
            if (pin.content.length() > 200) preview += "...";
            QString pinDisplay = QString("⭐ %1\n%2").arg(pin.name, softWrap(preview));
            card->setText(pinDisplay);

            // Сохраняем полный текст, имя и теги — нужны для поиска и редактирования
            card->setProperty("fullText",    pin.content);
            card->setProperty("displayText", pinDisplay);
            card->setProperty("pinName",     pin.name);
            card->setProperty("pinTags",     pin.tags);
            card->setProperty("pinId",       pin.id);
            card->setProperty("pinFolder", pin.folder);
            card->setEditMode(m_editMode);

            // Читаем fullText из свойства — так вставится отредактированный текст
            connect(card, &ClickableCard::clicked, this, [this, card, pid = pin.id]() {
                if (m_editMode) {
                    bool sel = !m_selectedPinIds.contains(pid);
                    if (sel) m_selectedPinIds.insert(pid); else m_selectedPinIds.remove(pid);
                    card->setSelected(sel);
                    return;
                }
                m_db->touchPin(pid);
                pasteText(card->property("fullText").toString());
            });

            int pinId = pin.id;
            QString pinFolder = pin.folder;
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, this,
                    [this, pinId, pinFolder, card](const QPoint &pos) {
                if (m_editMode && !m_selectedPinIds.isEmpty()) {
                    showPinBatchContextMenu(card->mapToGlobal(pos));
                    return;
                }
                showPinContextMenu(pinId, pinFolder, card, card->mapToGlobal(pos));
            });

            connect(card, &ClickableCard::dragStarted, this, [this, card](QPoint) {
                m_dragCard = card; m_dragLayout = m_leftLayout;
            });
            connect(card, &ClickableCard::dragMoved, this, [this](QPoint gp) {
                onCardDragMoved(gp);
            });
            connect(card, &ClickableCard::dragFinished, this, [this]() {
                onCardDragFinished();
            });

            m_leftLayout->addWidget(card);

        } else if (pin.type == "image") {
            // ── Изображение-закреп ─────────────────────────────────────────────
            // Раскладка: тот же фон что у текстовых закрепов, сверху "⭐ Название"
            ClickableCard *card = new ClickableCard();
            applyCardShadow(card);
            card->setFixedSize(sc(BASE_RIGHT) - 36, sc(148));
            card->setCardStyle(QColor(42, 30, 8, 245));
            card->setProperty("pinName", pin.name);
            card->setProperty("pinTags", pin.tags);
            card->setProperty("pinId",   pin.id);
            card->setProperty("pinFolder", pin.folder);
            card->setEditMode(m_editMode);

            // ── Строка с именем сверху (как у текстовых) ──────────────────────
            QString displayName = pin.name.isEmpty() ? "⭐" : QString("⭐ %1").arg(pin.name);
            QLabel *nameLabel = new QLabel(displayName, card);
            nameLabel->setStyleSheet(
                QString("color: #f0e0c0; font-size: %1; font-weight: bold;"
                        "background: transparent; padding: %2 %3 %4 %3;")
                .arg(spx(12), spx(5), spx(8), spx(3))
            );
            nameLabel->setFixedWidth(card->width());
            nameLabel->setFixedHeight(sc(24));
            nameLabel->move(0, 0);
            nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            nameLabel->show();

            // ── Изображение ниже строки с именем ───────────────────────────────
            QLabel *imgLabel = new QLabel(card);
            const int imgTop = 26;
            imgLabel->setFixedSize(card->width() - 4, card->height() - imgTop - 2);
            imgLabel->move(2, imgTop);
            imgLabel->setAlignment(Qt::AlignCenter);
            imgLabel->setStyleSheet("background: transparent;");
            imgLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

            // Превью грузим в фоне (раньше все картинки закрепов декодировались
            // разом при открытии вкладки — окно замирало)
            {
                const qreal dpr = imgLabel->devicePixelRatioF();
                const QSize px = (QSizeF(imgLabel->size()) * dpr).toSize();
                QPointer<QLabel> lbl = imgLabel;
                Thumbs::loadAsync(pin.filepath, px, imgLabel, [this, lbl, dpr](const QImage &img) {
                    if (!lbl) return;
                    if (!img.isNull()) {
                        QPixmap pm = QPixmap::fromImage(img);
                        pm.setDevicePixelRatio(dpr);
                        lbl->setPixmap(pm);
                    } else {
                        // Раньше здесь по ошибке выводилась строка «Название для «%1»:»
                        lbl->setText(tr("Файл изображения не найден"));
                        lbl->setStyleSheet("color: #888; background: transparent;");
                    }
                });
            }
            imgLabel->show();

            connect(card, &ClickableCard::clicked, this, [this, card, fp = pin.filepath, pid = pin.id]() {
                if (m_editMode) {
                    bool sel = !m_selectedPinIds.contains(pid);
                    if (sel) m_selectedPinIds.insert(pid); else m_selectedPinIds.remove(pid);
                    card->setSelected(sel);
                    return;
                }
                m_db->touchPin(pid);
                pasteImage(fp);
            });

            int pinId = pin.id;
            QString imgPinFolder = pin.folder;
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, this,
                    [this, pinId, imgPinFolder, pin, card, nameLabel](const QPoint &pos) {
                if (m_editMode && !m_selectedPinIds.isEmpty()) {
                    showPinBatchContextMenu(card->mapToGlobal(pos));
                    return;
                }
                RoundedMenu menu(this);

                menu.addAction(tr("🗗  Открыть в окне"), [this, fp = pin.filepath]() { openPhotoWindow(fp); });
                menu.addAction(tr("🔍  Просмотреть"), [this, fp = pin.filepath, card]() {
                    showImageViewer(fp, card);
                });
                menu.addSeparator();

                // Переименовать
                menu.addAction(tr("🏷  Переименовать"), [this, pinId, card, nameLabel]() {
                    // Берём текущее имя без "⭐ " — оно хранится отдельно в pinName
                    QString cur = card->property("pinName").toString();
                    bool ok = false;
                    QString newName = askText(this, tr("Переименовать закреп"), tr("Новое название:"), cur, &ok);
                    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == cur) return;
                    m_db->updatePinName(pinId, newName.trimmed());
                    card->setProperty("pinName", newName.trimmed());
                    nameLabel->setText(QString("⭐ %1").arg(newName.trimmed()));
                });
                menu.addAction(tr("#  Теги"), [this, pinId, card]() {
                    showTagsDialog(pinId, card);
                });
                menu.addSeparator();

                // Переместить в папку
                QList<FolderItem> folders = m_db->getAllFolders();
                if (!folders.isEmpty()) {
                    QMenu *moveMenu = menu.addMenu(tr("📁  Переместить в папку"));
                    if (!imgPinFolder.isEmpty()) {
                        moveMenu->addAction(tr("Без папки"), [this, pinId]() {
                            m_db->movePinToFolder(pinId, "");
                            loadFolderBar(); loadPins();
                        });
                        moveMenu->addSeparator();
                    }
                    for (const FolderItem &f : folders) {
                        if (f.name == imgPinFolder) continue;
                        QString fName = f.name;
                        moveMenu->addAction(fName, [this, pinId, fName]() {
                            m_db->movePinToFolder(pinId, fName);
                            loadFolderBar(); loadPins();
                        });
                    }
                    menu.addSeparator();
                }

                menu.addAction(tr("🗑  Открепить"), [this, pinId, card]() {
                    deletePinCard(pinId, card);
                });
                menu.exec(card->mapToGlobal(pos));
            });

            connect(card, &ClickableCard::dragStarted, this, [this, card](QPoint) {
                m_dragCard = card; m_dragLayout = m_rightLayout;
            });
            connect(card, &ClickableCard::dragMoved, this, [this](QPoint gp) {
                onCardDragMoved(gp);
            });
            connect(card, &ClickableCard::dragFinished, this, [this]() {
                onCardDragFinished();
            });

            m_rightLayout->addWidget(card);

        } else if (pin.type == "video") {
            // ── Видео-закреп ───────────────────────────────────────────────────
            ClickableCard *card = new ClickableCard();
            applyCardShadow(card);
            card->setFixedSize(sc(BASE_RIGHT) - 36, sc(148));
            card->setCardStyle(QColor(10, 20, 48, 245));
            card->setProperty("pinName", pin.name);
            card->setProperty("pinTags", pin.tags);
            card->setProperty("pinId",   pin.id);
            card->setProperty("pinFolder", pin.folder);
            card->setEditMode(m_editMode);

            // Строка с именем сверху, ниже — кадр; ▶ — играет прямо в карточке
            QString displayName = pin.name.isEmpty()
                ? QFileInfo(pin.filepath).fileName() : pin.name;
            QPointer<ClickableCard> cardPtr = card;
            const QString vfp = pin.filepath;
            Media::VideoSurface *surface = setupVideoCard(card, displayName, vfp,
                QString("color: #aaccff; font-size: %1; font-weight: bold;"
                        "background: transparent; padding: %2 %3 %4 %3;")
                    .arg(spx(12), spx(5), spx(8), spx(3)), sc(24),
                [this, cardPtr, vfp](Media::VideoSurface *sf) { if (cardPtr) m_videoViewer->open(cardPtr, sf, vfp); });
            QLabel *nameLabel = card->findChild<QLabel *>("videoName");

            connect(card, &ClickableCard::clicked, this, [this, card, fp = pin.filepath, pid = pin.id]() {
                if (m_editMode) {
                    bool sel = !m_selectedPinIds.contains(pid);
                    if (sel) m_selectedPinIds.insert(pid); else m_selectedPinIds.remove(pid);
                    card->setSelected(sel);
                    return;
                }
                m_db->touchPin(pid);
                pasteFile(fp);
            });

            int pinId = pin.id;
            QString pinFolder = pin.folder;
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, this,
                    [this, pinId, pinFolder, card, nameLabel, surface, fp = pin.filepath](const QPoint &pos) {
                if (m_editMode && !m_selectedPinIds.isEmpty()) {
                    showPinBatchContextMenu(card->mapToGlobal(pos));
                    return;
                }
                RoundedMenu menu(this);
                menu.addAction(tr("▶  Смотреть"), [this, card, surface, fp]() {
                    m_videoViewer->open(card, surface, fp);
                });
                menu.addAction(tr("Открыть в плеере"), [fp]() {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(fp));
                });
                menu.addSeparator();
                menu.addAction(tr("🏷  Переименовать"), [this, pinId, card, nameLabel]() {
                    bool ok = false;
                    QString cur = card->property("pinName").toString();
                    QString newName = askText(this, tr("Переименовать закреп"), tr("Новое название:"), cur, &ok);
                    if (!ok || newName.trimmed().isEmpty()) return;
                    m_db->updatePinName(pinId, newName.trimmed());
                    card->setProperty("pinName", newName.trimmed());
                    nameLabel->setText(QString("🎬  %1").arg(newName.trimmed()));
                });
                menu.addAction(tr("#  Теги"), [this, pinId, card]() {
                    showTagsDialog(pinId, card);
                });
                menu.addSeparator();
                QList<FolderItem> folders = m_db->getAllFolders();
                if (!folders.isEmpty()) {
                    QMenu *mv = menu.addMenu(tr("📁  Переместить в папку"));
                    if (!pinFolder.isEmpty()) {
                        mv->addAction(tr("Без папки"), [this, pinId]() {
                            m_db->movePinToFolder(pinId, ""); loadFolderBar(); loadPins();
                        });
                        mv->addSeparator();
                    }
                    for (const FolderItem &f : folders) {
                        if (f.name == pinFolder) continue;
                        QString fn = f.name;
                        mv->addAction(fn, [this, pinId, fn]() {
                            m_db->movePinToFolder(pinId, fn); loadFolderBar(); loadPins();
                        });
                    }
                    menu.addSeparator();
                }
                menu.addAction(tr("🗑  Открепить"), [this, pinId, card]() {
                    deletePinCard(pinId, card);
                });
                menu.exec(card->mapToGlobal(pos));
            });

            connect(card, &ClickableCard::dragStarted, this, [this, card](QPoint) {
                m_dragCard = card; m_dragLayout = m_rightLayout;
            });
            connect(card, &ClickableCard::dragMoved, this, [this](QPoint gp) {
                onCardDragMoved(gp);
            });
            connect(card, &ClickableCard::dragFinished, this, [this]() {
                onCardDragFinished();
            });

            m_rightLayout->addWidget(card);

        } else if (pin.type == "audio") {
            // ── Аудио-закреп ───────────────────────────────────────────────────
            ClickableCard *card = new ClickableCard();
            applyCardShadow(card);
            card->setWordWrap(true);
            card->setMaximumWidth(sc(BASE_LEFT) - 36);
            card->setCardStyle(QColor(32, 14, 55, 245));
            card->setStyleSheet(QString("color: #ddaaff; padding: %1; font-size: %2;").arg(spx(8), spx(12)));

            QString displayName = pin.name.isEmpty()
                ? QFileInfo(pin.filepath).fileName() : pin.name;
            card->setText(QString("🎵  %1").arg(displayName));
            Media::attachAudio(card, pin.filepath);  // ▶ слева — слушать прямо в карточке
            card->setProperty("pinName", pin.name);
            card->setProperty("pinTags", pin.tags);
            card->setProperty("pinId",   pin.id);
            card->setProperty("pinFolder", pin.folder);
            card->setEditMode(m_editMode);

            connect(card, &ClickableCard::clicked, this, [this, card, fp = pin.filepath, pid = pin.id]() {
                if (m_editMode) {
                    bool sel = !m_selectedPinIds.contains(pid);
                    if (sel) m_selectedPinIds.insert(pid); else m_selectedPinIds.remove(pid);
                    card->setSelected(sel);
                    return;
                }
                m_db->touchPin(pid);
                pasteFile(fp);
            });

            int pinId = pin.id;
            QString pinFolder = pin.folder;
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, this,
                    [this, pinId, pinFolder, card, fp = pin.filepath](const QPoint &pos) {
                if (m_editMode && !m_selectedPinIds.isEmpty()) {
                    showPinBatchContextMenu(card->mapToGlobal(pos));
                    return;
                }
                RoundedMenu menu(this);
                menu.addAction(tr("▶  Воспроизвести"), [card, fp]() {
                    auto &pb = Media::Playback::get();
                    if (pb.isOwner(card)) pb.toggle(); else pb.start(fp, card, nullptr, false);
                });
                menu.addAction(tr("Открыть в плеере"), [fp]() {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(fp));
                });
                menu.addSeparator();
                menu.addAction(tr("🏷  Переименовать"), [this, pinId, card]() {
                    bool ok = false;
                    QString cur = card->property("pinName").toString();
                    QString newName = askText(this, tr("Переименовать закреп"), tr("Новое название:"), cur, &ok);
                    if (!ok || newName.trimmed().isEmpty()) return;
                    m_db->updatePinName(pinId, newName.trimmed());
                    card->setProperty("pinName", newName.trimmed());
                    card->setText(QString("🎵  %1").arg(newName.trimmed()));
                });
                menu.addAction(tr("#  Теги"), [this, pinId, card]() {
                    showTagsDialog(pinId, card);
                });
                menu.addSeparator();
                QList<FolderItem> folders = m_db->getAllFolders();
                if (!folders.isEmpty()) {
                    QMenu *mv = menu.addMenu(tr("📁  Переместить в папку"));
                    if (!pinFolder.isEmpty()) {
                        mv->addAction(tr("Без папки"), [this, pinId]() {
                            m_db->movePinToFolder(pinId, ""); loadFolderBar(); loadPins();
                        });
                        mv->addSeparator();
                    }
                    for (const FolderItem &f : folders) {
                        if (f.name == pinFolder) continue;
                        QString fn = f.name;
                        mv->addAction(fn, [this, pinId, fn]() {
                            m_db->movePinToFolder(pinId, fn); loadFolderBar(); loadPins();
                        });
                    }
                    menu.addSeparator();
                }
                menu.addAction(tr("🗑  Открепить"), [this, pinId, card]() {
                    deletePinCard(pinId, card);
                });
                menu.exec(card->mapToGlobal(pos));
            });

            connect(card, &ClickableCard::dragStarted, this, [this, card](QPoint) {
                m_dragCard = card; m_dragLayout = m_leftLayout;
            });
            connect(card, &ClickableCard::dragMoved, this, [this](QPoint gp) {
                onCardDragMoved(gp);
            });
            connect(card, &ClickableCard::dragFinished, this, [this]() {
                onCardDragFinished();
            });

            m_leftLayout->addWidget(card);
        }
    }

}

// ─── Drag-to-reorder (перетаскивание для изменения порядка) ──────────────────
// ClickableCard::dragStarted/Moved/Finished — сигналы которые испускаются
// когда пользователь начинает тянуть карточку. Здесь мы:
//   dragStarted   → запоминаем m_dragCard и m_dragLayout
//   dragMoved     → пересчитываем позицию вставки и двигаем карточку в лейауте
//   dragFinished  → сохраняем новый порядок в БД

// onCardDragMoved — вызывается при каждом движении мыши во время перетаскивания.
// Находит позицию вставки (перед первой карточкой центр которой ниже курсора)
// и перемещает m_dragCard в нужное место через removeWidget + insertWidget.
// ─── Перетаскивание закрепов ─────────────────────────────────────────────────
// Как в современных системах: карточка «приподнимается» (полупрозрачная и чуть
// меньше), свободно едет за мышью, а на месте, куда её вставят, раскрывается
// пустое место — соседние карточки плавно расступаются. Отпустили — карточка
// доезжает в это место. У края колонки список сам прокручивается.

CardArc *MainWindow::arcFor(QVBoxLayout *layout) const
{
    return layout == m_leftLayout ? m_leftArc : layout == m_rightLayout ? m_rightArc : nullptr;
}

QScrollArea *MainWindow::scrollFor(QVBoxLayout *layout) const
{
    return layout == m_leftLayout ? m_leftScroll : layout == m_rightLayout ? m_rightScroll : nullptr;
}

// Плавно «поднять» (to = 1) или «опустить» (to = 0) карточку
static void animateLift(QWidget *card, qreal to, int ms)
{
    auto *anim = new QVariantAnimation(card);
    anim->setStartValue(card->property("lift").toReal());
    anim->setEndValue(to);
    anim->setDuration(Anim::enabled(Anim::Kind::Scroll) ? Anim::ms(Anim::Kind::Scroll, ms) : 0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(anim, &QVariantAnimation::valueChanged, card, [card](const QVariant &v) {
        card->setProperty("lift", v.toReal());
        // Пока карточку тащат, она вне колонки — вид задаём сами
        if (auto *fx = dynamic_cast<CardShadowEffect *>(card->graphicsEffect()))
            fx->setLook(1.0 - 0.05 * v.toReal(), 1.0 - 0.35 * v.toReal());
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// Прервать перетаскивание без сохранения (список перестраивают целиком).
// Раньше в этом случае продолжал крутиться таймер плавной перестройки.
void MainWindow::stopCardDrag()
{
    if (m_dragScrollTimer) m_dragScrollTimer->stop();
    if (m_leftArc)  m_leftArc->setReflowAnimated(false);
    if (m_rightArc) m_rightArc->setReflowAnimated(false);
    m_dragPlaceholder = nullptr;  // Удалится вместе с колонкой
    m_dragCard   = nullptr;
    m_dragLayout = nullptr;
}

void MainWindow::beginCardDrag(const QPoint &globalPos)
{
    QWidget *content = m_dragLayout->parentWidget();
    const int idx = m_dragLayout->indexOf(m_dragCard);
    if (!content || idx < 0) return;

    // Карточка выходит из колонки и едет за мышью; на её месте — пустое место
    m_dragPlaceholder = new QWidget(content);
    m_dragPlaceholder->setFixedSize(m_dragCard->size());
    m_dragPlaceholder->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dragGrab = m_dragCard->mapFromGlobal(globalPos);
    if (CardArc *arc = arcFor(m_dragLayout)) arc->setReflowAnimated(true);
    m_dragLayout->removeWidget(m_dragCard);
    m_dragLayout->insertWidget(idx, m_dragPlaceholder);
    m_dragCard->raise();
    animateLift(m_dragCard, 1.0, 140);

    // Автопрокрутка у края колонки, пока карточку держат
    if (!m_dragScrollTimer) {
        m_dragScrollTimer = new QTimer(this);
        m_dragScrollTimer->setInterval(16);
        connect(m_dragScrollTimer, &QTimer::timeout, this, [this]() {
            if (!m_dragCard || !m_dragLayout) { m_dragScrollTimer->stop(); return; }
            QScrollArea *area = scrollFor(m_dragLayout);
            if (!area) return;
            const QPoint c = area->viewport()->mapFromGlobal(QCursor::pos());
            const int edge = 56, h = area->viewport()->height();
            int step = 0;
            if (c.y() < edge)          step = -qMax(2, (edge - c.y()) / 3);
            else if (c.y() > h - edge) step =  qMax(2, (c.y() - (h - edge)) / 3);
            if (!step) return;
            QScrollBar *sb = area->verticalScrollBar();
            const int before = sb->value();
            sb->setValue(before + step);
            if (sb->value() != before) onCardDragMoved(QCursor::pos());
        });
    }
    m_dragScrollTimer->start();
}

void MainWindow::onCardDragMoved(const QPoint &globalPos)
{
    if (!m_dragCard || !m_dragLayout) return;
    if (!m_dragPlaceholder) beginCardDrag(globalPos);
    if (!m_dragPlaceholder) return;

    // Карточка едет за мышью (по горизонтали — в пределах колонки)
    QWidget *content = m_dragLayout->parentWidget();
    QPoint p = content->mapFromGlobal(globalPos) - m_dragGrab;
    p.setX(qBound(-m_dragCard->width() / 4, p.x(), content->width() - m_dragCard->width() * 3 / 4));
    m_dragCard->move(p);

    // Куда вставить: перед первой карточкой, середина которой ниже середины перетаскиваемой
    const int centerY = p.y() + m_dragCard->height() / 2;
    const int from = m_dragLayout->indexOf(m_dragPlaceholder);
    int insertIdx = 0;
    for (int i = 0; i < m_dragLayout->count(); ++i) {
        QWidget *w = m_dragLayout->itemAt(i)->widget();
        if (!w || w == m_dragPlaceholder || w->isHidden()) continue;
        const QPoint base = w->property("arcBase").isValid() ? w->property("arcBase").toPoint() : w->pos();
        if (centerY > base.y() + w->height() / 2) insertIdx = (i < from) ? i + 1 : i;
    }
    if (insertIdx != from) {
        m_dragLayout->removeWidget(m_dragPlaceholder);
        m_dragLayout->insertWidget(insertIdx, m_dragPlaceholder);
    }
}

// onCardDragFinished — карточка встаёт на освободившееся место, порядок — в БД
void MainWindow::onCardDragFinished()
{
    if (m_dragScrollTimer) m_dragScrollTimer->stop();
    if (m_dragPlaceholder && m_dragCard && m_dragLayout) {
        const int idx = m_dragLayout->indexOf(m_dragPlaceholder);
        m_dragLayout->removeWidget(m_dragPlaceholder);
        m_dragPlaceholder->deleteLater();
        m_dragLayout->insertWidget(qMax(0, idx), m_dragCard);  // Доедет сама (плавная перестройка)
        animateLift(m_dragCard, 0.0, 200);
        // Когда всё доехало — выключаем плавную перестройку
        if (CardArc *arc = arcFor(m_dragLayout)) {
            QPointer<CardArc> guard(arc);
            QTimer::singleShot(Anim::ms(Anim::Kind::Scroll, 450), this, [guard]() {
                if (guard) guard->setReflowAnimated(false);
            });
        }
    }
    m_dragPlaceholder = nullptr;
    if (m_dragLayout) savePinsColumnOrder(m_dragLayout);
    m_dragCard   = nullptr;
    m_dragLayout = nullptr;
}

void MainWindow::savePinsColumnOrder(QVBoxLayout *layout)
{
    QList<QPair<int,int>> orders;
    for (int i = 0; i < layout->count(); ++i) {
        QWidget *w = layout->itemAt(i)->widget();
        if (!w) continue;
        int pid = w->property("pinId").toInt();
        // На «Все» есть и недавно использованные закрепы из папок — их порядок
        // в папке не трогаем (на «Все» они стоят по дате использования)
        if (m_currentFolder.isEmpty() && !w->property("pinFolder").toString().isEmpty()) continue;
        if (pid > 0) orders.append({pid, i + 1});
    }
    if (!orders.isEmpty())
        m_db->updatePinsSortOrder(orders);
}

void MainWindow::showTagsDialog(int pinId, QWidget *card)
{
    QString currentTags = card ? card->property("pinTags").toString() : "";
    QStringList tags = currentTags.split(' ', Qt::SkipEmptyParts);

    AppDialog dlg(this);
    dlg.setWindowTitle(tr("Теги закрепа"));
    dlg.setMinimumWidth(440);
    dlg.setMinimumHeight(360);
    dlg.resize(440, 420);
    dlg.body()->setStyleSheet(
        "QLabel     { color: #aaa; font-size: 12px; }"
        "QScrollArea{ background: rgba(28,28,44,200); border: 1px solid rgba(255,255,255,40); border-radius: 6px; }"
        "QWidget#chipsWidget { background: transparent; }"
        "QLineEdit  { background: rgba(28,28,44,245); color: #eee; border: 1px solid rgba(255,255,255,55);"
        "             border-radius: 4px; padding: 6px; font-size: 13px; }"
        "QPushButton { background: rgba(40,40,60,220); color: #ccc; border: 1px solid rgba(255,255,255,40);"
        "              border-radius: 4px; padding: 6px 18px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(60,60,90,240); color: #fff; }"
        "QPushButton#addBtn  { background: rgba(30,80,40,220); color: #aaffaa;"
        "                      border: 1px solid rgba(100,200,100,80);"
        "                      border-radius: 4px; padding: 6px 14px; font-size: 16px; font-weight: bold; }"
        "QPushButton#addBtn:hover { background: rgba(40,120,55,240); color: #fff; }"
        "QPushButton#deleteBtn { background: transparent; color: #666; padding: 2px 6px;"
        "                        font-size: 12px; border: none; border-radius: 3px; }"
        "QPushButton#deleteBtn:hover { background: rgba(180,60,60,180); color: #fff; }"
    );

    QVBoxLayout *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    // ── Прокручиваемый список тегов ─────────────────────────────────────────────
    QScrollArea *scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(120);

    QWidget *chipsWidget = new QWidget();
    chipsWidget->setObjectName("chipsWidget");
    QVBoxLayout *chipsLayout = new QVBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(8, 6, 8, 6);
    chipsLayout->setSpacing(4);
    chipsLayout->addStretch();

    scroll->setWidget(chipsWidget);
    vl->addWidget(scroll);

    // Функция пересборки списка тегов (std::function — чтобы лямбда могла захватить себя)
    std::function<void()> rebuildChips = [&]() {
        // Удаляем все строки кроме последнего stretch
        // deleteLater: функция вызывается из кнопки ✕ самой удаляемой строки
        clearLayout(chipsLayout, 1);  // Оставляем растяжку в конце

        for (int i = 0; i < tags.size(); ++i) {
            QWidget *row = new QWidget(chipsWidget);
            row->setStyleSheet("background: transparent;");
            QHBoxLayout *hl = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(6);

            QLabel *lbl = new QLabel(QString("<b>#%1</b>").arg(tags[i].toHtmlEscaped()), row);
            lbl->setStyleSheet("color: #aaddff; font-size: 13px; background: transparent;");

            QPushButton *del = new QPushButton("✕", row);
            del->setObjectName("deleteBtn");
            del->setFixedSize(22, 22);
            del->setCursor(Qt::PointingHandCursor);
            del->setToolTip(tr("Удалить"));

            int idx = i;
            connect(del, &QPushButton::clicked, [&tags, idx, &rebuildChips]() {
                tags.removeAt(idx);
                rebuildChips();
            });

            hl->addWidget(lbl);
            hl->addStretch();
            hl->addWidget(del);

            // Вставляем перед stretch
            chipsLayout->insertWidget(chipsLayout->count() - 1, row);
        }

        if (tags.isEmpty()) {
            QLabel *empty = new QLabel(tr("Нет тегов. Добавь первый тег ниже."), chipsWidget);
            empty->setStyleSheet(QString("color: #555; font-size: %1; background: transparent;").arg(spx(12)));
            empty->setAlignment(Qt::AlignCenter);
            chipsLayout->insertWidget(0, empty);
        }
    };

    rebuildChips();

    // ── Копировать / вставить теги ──────────────────────────────────────────────
    QHBoxLayout *clipRow = new QHBoxLayout();
    clipRow->setSpacing(6);

    QPushButton *copyBtn  = new QPushButton(tr("Копировать теги"), &dlg);
    QPushButton *pasteBtn = new QPushButton(tr("Вставить теги"),   &dlg);
    copyBtn->setCursor(Qt::PointingHandCursor);
    pasteBtn->setCursor(Qt::PointingHandCursor);

    // Копировать: собираем текущие теги с # и кладём в буфер
    connect(copyBtn, &QPushButton::clicked, [&]() {
        if (tags.isEmpty()) return;
        QStringList withHash;
        for (const QString &t : tags) withHash << "#" + t;
        QApplication::clipboard()->setText(withHash.join(" "));
    });

    // Вставить: читаем из буфера, парсим теги, добавляем без дубликатов
    connect(pasteBtn, &QPushButton::clicked, [&]() {
        QString raw = QApplication::clipboard()->text();
        raw.replace(',', ' ').replace('#', ' ');
        QStringList parts = raw.split(' ', Qt::SkipEmptyParts);
        bool added = false;
        for (const QString &p : parts) {
            QString t = p.toLower().trimmed();
            if (!t.isEmpty() && !tags.contains(t)) {
                tags.append(t);
                added = true;
            }
        }
        if (added) rebuildChips();
    });

    clipRow->addWidget(copyBtn);
    clipRow->addWidget(pasteBtn);
    clipRow->addStretch();
    vl->addLayout(clipRow);

    // ── Поле добавления нового тега ─────────────────────────────────────────────
    QHBoxLayout *addRow = new QHBoxLayout();
    addRow->setSpacing(6);
    QLineEdit *addEdit = new QLineEdit(&dlg);
    addEdit->setPlaceholderText(tr("Новый тег..."));
    QPushButton *addBtn = new QPushButton("+", &dlg);
    addBtn->setObjectName("addBtn");
    addBtn->setFixedWidth(42);
    addRow->addWidget(addEdit);
    addRow->addWidget(addBtn);
    vl->addLayout(addRow);

    auto addTag = [&]() {
        QString raw = addEdit->text();
        raw.replace(',', ' ').replace('#', ' ');
        QStringList parts = raw.split(' ', Qt::SkipEmptyParts);
        bool added = false;
        for (const QString &p : parts) {
            QString t = p.toLower().trimmed();
            if (!t.isEmpty() && !tags.contains(t)) {
                tags.append(t);
                added = true;
            }
        }
        if (added) { addEdit->clear(); rebuildChips(); }
    };
    connect(addBtn,  &QPushButton::clicked, addTag);
    connect(addEdit, &QLineEdit::returnPressed, addTag);

    // ── Кнопки ─────────────────────────────────────────────────────────────────
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    QPushButton *cancelBtn = new QPushButton(tr("Отмена"), &dlg);
    QPushButton *okBtn     = new QPushButton(tr("Сохранить"), &dlg);
    okBtn->setDefault(true);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    vl->addLayout(btnRow);

    connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    QString newTags = tags.join(' ');
    m_db->updatePinTags(pinId, newTags);
    if (card) card->setProperty("pinTags", newTags);
}

void MainWindow::showPinContextMenu(int pinId, const QString &currentFolder,
                                    QLabel *card, const QPoint &globalPos)
{
    RoundedMenu menu(this);

    // Переименовать закреп
    menu.addAction(tr("✏️  Переименовать"), [this, pinId, card]() {
        if (!card) return;
        // Имя берём из свойства, а не из текста карточки: при активном поиске
        // там HTML-подсветка, и имя «разбиралось» с тегами внутри.
        QString oldName = card->property("pinName").toString();

        bool ok = false;
        QString newName = askText(this, tr("Переименовать закреп"), tr("Новое название:"), oldName, &ok);
        newName = newName.trimmed();
        if (!ok || newName.isEmpty() || newName == oldName) return;

        m_db->updatePinName(pinId, newName);

        // Обновляем карточку без перезагрузки — так же, как её строит loadPins()
        QString content = card->property("fullText").toString();
        QString preview = content.trimmed().left(200);
        if (content.length() > 200) preview += "...";
        QString pinDisplay = QString("⭐ %1\n%2").arg(newName, softWrap(preview));
        card->setTextFormat(Qt::PlainText);
        card->setText(pinDisplay);
        card->setProperty("pinName",     newName);     // иначе поиск искал по старому имени
        card->setProperty("displayText", pinDisplay);
    });

    // Редактировать текст закрепа
    menu.addAction(tr("📝  Редактировать"), [this, pinId, card]() {
        QString content = card ? card->property("fullText").toString() : "";
        editPin(pinId, content, card);
    });

    menu.addAction(tr("#  Теги"), [this, pinId, card]() {
        showTagsDialog(pinId, card);
    });
    menu.addSeparator();

    // Подменю "Переместить в папку"
    QList<FolderItem> folders = m_db->getAllFolders();
    if (!folders.isEmpty()) {
        QMenu *moveMenu = menu.addMenu(tr("📁  Переместить в папку"));

        // "Без папки" если закреп сейчас в папке
        if (!currentFolder.isEmpty()) {
            moveMenu->addAction(tr("Без папки"), [this, pinId, card]() {
                m_db->movePinToFolder(pinId, "");
                loadFolderBar();
                loadPins();
            });
            moveMenu->addSeparator();
        }

        for (const FolderItem &f : folders) {
            if (f.name == currentFolder) continue; // уже в этой папке
            QString fName = f.name;
            moveMenu->addAction(fName, [this, pinId, fName]() {
                m_db->movePinToFolder(pinId, fName);
                loadFolderBar();
                loadPins();
            });
        }
    }

    menu.addSeparator();
    menu.addAction(tr("🗑  Удалить"), [this, pinId, card]() {
        deletePinCard(pinId, card);
    });

    menu.exec(globalPos);
}

void MainWindow::deletePinCard(int pinId, QLabel *card)
{
    m_db->deletePin(pinId);

    if (card) {
        QVBoxLayout *layout = nullptr;
        if (m_leftLayout->indexOf(card) != -1)
            layout = m_leftLayout;
        else if (m_rightLayout->indexOf(card) != -1)
            layout = m_rightLayout;

        if (layout)
            layout->removeWidget(card);

        card->deleteLater();
    }
}

void MainWindow::pasteTextSilent(const QString &text)
{
    // Вставить текст профиля без открытия/закрытия окна SmartClip
    emit pasteRequested();
    QApplication::clipboard()->setText(text);

    QTimer::singleShot(150, &sendPaste);
}

void MainWindow::showProfilesMenu()
{
    QList<ProfileItem> profiles = m_db->getProfiles();

    RoundedMenu menu(this);

    if (profiles.isEmpty()) {
        menu.addAction(tr("Нет профилей"))->setEnabled(false);
    } else {
        for (const ProfileItem &p : profiles) {
            QString label = QString("%1  [%2]").arg(p.name, p.hotkey);
            menu.addAction(label, [this, text = p.text]() {
                pasteText(text); // вставляем и прячем окно
            });
        }
    }

    menu.addSeparator();
    menu.addAction(tr("✏️  Управление профилями"), [this]() {
        showProfileManager();
    });

    menu.exec(m_profilesBtn->mapToGlobal(QPoint(0, m_profilesBtn->height() + 6)));
}

void MainWindow::showProfileManager()
{
    AppDialog dialog(this);
    dialog.setWindowTitle(tr("Профили"));
    dialog.setMinimumSize(480, 420);

    QVBoxLayout *mainLayout = new QVBoxLayout(dialog.body());
    mainLayout->setContentsMargins(16, 16, 16, 12);
    mainLayout->setSpacing(8);

    QLabel *hint = new QLabel(
        tr("Профили быстрой вставки — нажми хоткей или выбери из меню."), &dialog);
    hint->setStyleSheet("color: #888888; font-size: 11px;");
    mainLayout->addWidget(hint);

    QScrollArea *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("background: transparent; border: none;");
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget    *listWidget = new QWidget();
    QVBoxLayout *listLayout = new QVBoxLayout(listWidget);
    listLayout->setAlignment(Qt::AlignTop);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(6);
    listWidget->setStyleSheet("background: transparent;");
    scroll->setWidget(listWidget);
    mainLayout->addWidget(scroll, 1);

    // ── Вспомогательная функция: открыть форму добавления/редактирования ───────
    auto openForm = [&](int editId, const QString &initName,
                        const QString &initHotkey, const QString &initText) -> bool
    {
        AppDialog form(&dialog);
        form.setWindowTitle(editId < 0 ? tr("Новый профиль") : tr("Редактировать профиль"));
        form.setMinimumSize(400, 340);

        QVBoxLayout *fl = new QVBoxLayout(form.body());
        fl->setContentsMargins(16, 16, 16, 12);
        fl->setSpacing(8);

        auto mkField = [&](const QString &label) -> QLineEdit* {
            fl->addWidget(new QLabel(label, &form));
            QLineEdit *f = new QLineEdit(&form);
            f->setStyleSheet(
                "background: #1e1e1e; color: #ddd; border: 1px solid #555;"
                "border-radius: 5px; padding: 5px 8px;"
            );
            fl->addWidget(f);
            return f;
        };

        QLineEdit *nameField = mkField(tr("Название:"));

        // Поле захвата хоткея — кликни и нажми комбинацию
        fl->addWidget(new QLabel(tr("Горячая клавиша:"), &form));
        HotkeyEdit *hotkeyField = new HotkeyEdit(&form);
        fl->addWidget(hotkeyField);

        fl->addWidget(new QLabel(tr("Текст:"), &form));
        QTextEdit *textField = new QTextEdit(&form);
        textField->setStyleSheet(
            "background: #1e1e1e; color: #ddd; border: 1px solid #555;"
            "border-radius: 5px; padding: 5px;"
        );
        fl->addWidget(textField, 1);

        nameField->setText(initName);
        hotkeyField->setText(initHotkey);
        textField->setPlainText(initText);

        QHBoxLayout *btns = new QHBoxLayout();
        QPushButton *cancelBtn = new QPushButton(tr("Отмена"), &form);
        QPushButton *saveBtn   = new QPushButton(tr("Сохранить"), &form);
        cancelBtn->setStyleSheet(
            "QPushButton { background: rgba(60,60,60,200); color: #aaa; "
            "border-radius: 6px; padding: 6px 20px; border: none; }"
            "QPushButton:hover { background: rgba(80,80,80,220); }"
        );
        saveBtn->setStyleSheet(
            "QPushButton { background: rgba(40,100,40,200); color: #ccffcc; "
            "border-radius: 6px; padding: 6px 20px; border: none; }"
            "QPushButton:hover { background: rgba(60,140,60,220); color: #fff; }"
        );
        saveBtn->setDefault(true);
        connect(cancelBtn, &QPushButton::clicked, &form, &QDialog::reject);
        connect(saveBtn,   &QPushButton::clicked, &form, &QDialog::accept);
        btns->addStretch();
        btns->addWidget(cancelBtn);
        btns->addWidget(saveBtn);
        fl->addLayout(btns);

        if (form.exec() != QDialog::Accepted) return false;

        QString n = nameField->text().trimmed();
        QString h = hotkeyField->text().trimmed();
        QString t = textField->toPlainText();
        if (n.isEmpty() || t.isEmpty()) return false;

        if (editId < 0)
            m_db->addProfile(n, h, t);
        else
            m_db->updateProfile(editId, n, h, t);

        emit profilesChanged();
        return true;
    };

    // ── Пересборка списка (через QTimer чтобы не удалять виджет из его хендлера)
    std::function<void()> rebuild = [&]() {
        clearLayout(listLayout);

        QList<ProfileItem> profiles = m_db->getProfiles();
        if (profiles.isEmpty()) {
            QLabel *empty = new QLabel(tr("Нет профилей"), listWidget);
            empty->setAlignment(Qt::AlignCenter);
            empty->setStyleSheet("color: #666; padding: 20px;");
            listLayout->addWidget(empty);
            return;
        }

        for (const ProfileItem &prof : profiles) {
            QWidget *row = new QWidget(listWidget);
            row->setStyleSheet(
                // "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                // "stop:0 rgba(255,255,255,38),stop:0.2 rgba(22,22,35,242),stop:1 rgba(15,15,25,248));"
                "background: rgba(20,20,32,245);"
                "border-radius: 8px;"
                "border: 2px solid rgba(255,255,255,55);"
            );
            QHBoxLayout *rowL = new QHBoxLayout(row);
            rowL->setContentsMargins(10, 8, 10, 8);
            rowL->setSpacing(8);

            QVBoxLayout *info = new QVBoxLayout();
            QLabel *nameLabel = new QLabel(
                QString("<b>%1</b>  <span style='color:#888;font-size:11px;'>[%2]</span>")
                    .arg(prof.name.toHtmlEscaped(), prof.hotkey.toHtmlEscaped()),
                row
            );
            nameLabel->setStyleSheet("color: #dddddd;");
            QString preview = prof.text.trimmed().left(60);
            if (prof.text.length() > 60) preview += "...";
            QLabel *prevLabel = new QLabel(preview.toHtmlEscaped(), row);
            prevLabel->setStyleSheet("color: #888888; font-size: 11px;");
            info->addWidget(nameLabel);
            info->addWidget(prevLabel);
            rowL->addLayout(info, 1);

            QPushButton *editBtn = new QPushButton("✏️", row);
            editBtn->setFixedSize(28, 28);
            editBtn->setStyleSheet(
                "QPushButton { background: rgba(60,60,60,200); color: #aaa; "
                "border-radius: 5px; border: none; font-size: 14px; }"
                "QPushButton:hover { background: rgba(80,80,80,220); color: #fff; }"
            );
            editBtn->setCursor(Qt::PointingHandCursor);

            QPushButton *delBtn = new QPushButton("✕", row);
            delBtn->setFixedSize(28, 28);
            delBtn->setStyleSheet(
                "QPushButton { background: rgba(60,60,60,200); color: #cc4444; "
                "border-radius: 5px; border: none; font-size: 14px; }"
                "QPushButton:hover { background: rgba(100,30,30,220); color: #ff6666; }"
            );
            delBtn->setCursor(Qt::PointingHandCursor);

            rowL->addWidget(editBtn);
            rowL->addWidget(delBtn);
            listLayout->addWidget(row);

            int id = prof.id;
            QString pName = prof.name, pHotkey = prof.hotkey, pText = prof.text;

            // Редактировать: открываем форму, после — отложенная пересборка
            connect(editBtn, &QPushButton::clicked, [=, &openForm, &rebuild]() {
                if (openForm(id, pName, pHotkey, pText))
                    QTimer::singleShot(0, [&rebuild]() { rebuild(); });
            });

            // Удалить: сначала возвращаемся из хендлера, потом удаляем виджеты
            connect(delBtn, &QPushButton::clicked, [=, &rebuild, &dialog]() {
                if (!confirmDialog(&dialog, tr("Удалить профиль"),
                                   tr("Удалить профиль «%1»?").arg(pName), true))
                    return;
                m_db->deleteProfile(id);
                emit profilesChanged();
                QTimer::singleShot(0, [&rebuild]() { rebuild(); });
            });
        }
    };

    rebuild();

    // ── Нижняя панель ───────────────────────────────────────────────────────────
    QHBoxLayout *bottomRow = new QHBoxLayout();

    QPushButton *addBtn = new QPushButton(tr("+ Добавить профиль"), &dialog);
    addBtn->setStyleSheet(
        "QPushButton { background: rgba(50,80,50,200); color: #aaddaa; "
        "border-radius: 6px; padding: 6px 16px; border: none; }"
        "QPushButton:hover { background: rgba(60,120,60,220); color: #fff; }"
    );
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, [&]() {
        if (openForm(-1, "", "", ""))
            QTimer::singleShot(0, [&rebuild]() { rebuild(); });
    });

    QPushButton *closeBtn = new QPushButton(tr("Закрыть"), &dialog);
    closeBtn->setStyleSheet(
        "QPushButton { background: rgba(60,60,60,200); color: #aaa; "
        "border-radius: 6px; padding: 6px 20px; border: none; }"
        "QPushButton:hover { background: rgba(80,80,80,220); color: #ddd; }"
    );
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    bottomRow->addWidget(addBtn);
    bottomRow->addStretch();
    bottomRow->addWidget(closeBtn);
    mainLayout->addLayout(bottomRow);

    dialog.exec();
}

// Окно правки длинного текста (закреп, запись Истории). false — передумали
static bool editTextDialog(QWidget *parent, const QString &title, const QString &label,
                           const QString &content, QString &result)
{
    AppDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumSize(420, 300);

    QVBoxLayout *layout = new QVBoxLayout(dialog.body());
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    QLabel *hint = new QLabel(label, &dialog);
    layout->addWidget(hint);

    QTextEdit *edit = new QTextEdit(&dialog);
    edit->setPlainText(content);
    edit->setStyleSheet(
        "QTextEdit { background-color: #1e1e1e; color: #dddddd; "
        "border: 1px solid #555; border-radius: 6px; "
        "font-size: 13px; padding: 6px; }"
    );
    layout->addWidget(edit);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);

    QPushButton *cancelBtn = new QPushButton(QObject::tr("Отмена"), &dialog);
    cancelBtn->setStyleSheet(
        "QPushButton { background: rgba(60,60,60,200); color: #aaaaaa; "
        "border-radius: 6px; padding: 6px 20px; border: none; }"
        "QPushButton:hover { background: rgba(80,80,80,220); color: #dddddd; }"
    );

    QPushButton *saveBtn = new QPushButton(QObject::tr("Сохранить"), &dialog);
    saveBtn->setStyleSheet(
        "QPushButton { background: rgba(40,100,40,200); color: #ccffcc; "
        "border-radius: 6px; padding: 6px 20px; border: none; }"
        "QPushButton:hover { background: rgba(60,140,60,220); color: #ffffff; }"
    );
    saveBtn->setDefault(true);

    QObject::connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(saveBtn,   &QPushButton::clicked, &dialog, &QDialog::accept);

    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    layout->addLayout(btnRow);

    if (dialog.exec() != QDialog::Accepted) return false;
    result = edit->toPlainText();
    return true;
}

void MainWindow::editPin(int id, const QString &content, QLabel *card)
{
    QString newContent;
    if (!editTextDialog(this, tr("Редактировать закреп"), tr("Текст закрепа:"), content, newContent)) return;
    if (newContent == content) return; // ничего не изменилось

    m_db->updatePinContent(id, newContent);

    // Обновляем карточку без полной перезагрузки
    if (card) {
        card->setProperty("fullText", newContent);
        // Имя — из свойства (в тексте карточки может быть HTML-подсветка поиска).
        // displayText обязательно обновляем: из него поиск восстанавливает текст,
        // и раньше после поиска на карточке снова появлялся СТАРЫЙ текст.
        QString preview = newContent.trimmed().left(200);
        if (newContent.length() > 200) preview += "...";
        QString pinDisplay = QString("⭐ %1\n%2")
            .arg(card->property("pinName").toString(), softWrap(preview));
        card->setTextFormat(Qt::PlainText);
        card->setText(pinDisplay);
        card->setProperty("displayText", pinDisplay);
    }
}

// Редактировать текст записи Истории (как у закрепов)
void MainWindow::editHistoryText(int id, QLabel *card)
{
    const QString content = card ? card->property("fullText").toString() : QString();
    QString newContent;
    if (!editTextDialog(this, tr("Редактировать текст"), tr("Текст:"), content, newContent)) return;
    if (newContent == content || newContent.trimmed().isEmpty()) return;
    m_db->updateHistoryContent(id, newContent);
    m_searchIndexValid = false;
    if (card) {
        QString display = newContent;
        if (display.length() > 300) display = display.left(300) + "...";
        const QString wrapped = softWrap(display);
        card->setTextFormat(Qt::PlainText);
        card->setText(wrapped);
        card->setProperty("fullText", newContent);
        card->setProperty("displayText", wrapped);
        if (m_searching) applyHighlight(card, m_searchEdit->text());
    }
}

// ─── showSettings ─────────────────────────────────────────────────────────────
// Открывает диалог настроек и подключает его сигналы к MainWindow:
//   mainHotkeyChanged — горячая клавиша изменилась (HotkeyManager нужно перерегистрировать)
//   settingsChanged   — общие настройки изменились (история, закрепы и т.д.)
//   updateRequested   — пользователь нажал «Установить» в баннере обновления
void MainWindow::showSettings()
{
    SettingsDialog dlg(m_db, this);

    connect(&dlg, &SettingsDialog::mainHotkeyChanged,
            this, &MainWindow::mainHotkeyChanged);
    connect(&dlg, &SettingsDialog::settingsChanged,
            this, &MainWindow::settingsChanged);
    // Напрямую в showUpdateDialog (НЕ onUpdateAvailable!) — иначе patch-версии
    // отфильтруются ранним return и кнопка «Обновить» не сделает ничего.
    // Qt::QueuedConnection — окно обновления откроется, когда настройки уже закрыты
    connect(&dlg, &SettingsDialog::updateRequested,
            this, &MainWindow::showUpdateDialog, Qt::QueuedConnection);
    // После импорта: новые профили должны сразу заработать по хоткеям,
    // а новые закрепы — появиться в окне (раньше — только после перезапуска).
    connect(&dlg, &SettingsDialog::dataImported, this, [this]() {
        emit profilesChanged();
        loadPins();
        markHistoryDirty();
    });

    if (!m_pendingUpdateVersion.isEmpty())
        dlg.setPendingUpdate(m_pendingUpdateVersion, m_pendingUpdateUrl, m_pendingReleaseNotes);

    dlg.exec();

    // Оформление могло поменяться: дуга/выезд карточек, шкала дат и тёмный фон — сразу
    if (m_leftArc)  m_leftArc->refresh();
    if (m_rightArc) m_rightArc->refresh();
    if (m_leftRuler)  m_leftRuler->refresh();
    if (m_rightRuler) m_rightRuler->refresh();
    if (m_filesStrip) m_filesStrip->reload();  // Папки, типы, вид полоски файлов
    update();
}

// ─── nativeEvent ──────────────────────────────────────────────────────────────
// Перехватывает сырые Win32 сообщения до того как Qt их обработает.
// Это единственный способ получить WM_NCHITTEST и WM_ACTIVATE напрямую.
//
// WM_NCHITTEST (тест попадания курсора):
//   HTTRANSPARENT — клик проваливается насквозь к окну под нами.
//   Возвращаем это для центральной прозрачной зоны (m_centerSpacer).
//   Для колонок с карточками возвращаем false (Qt обработает сам).
//
// WM_ACTIVATE:
//   WA_INACTIVE — наше окно теряет фокус. Скрываемся ТОЛЬКО если фокус
//   уходит в другой ПРОЦЕСС. Если фокус уходит в наш собственный диалог
//   (например QInputDialog) — не скрываемся.
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG *msg = static_cast<MSG*>(message);

    if (msg->message == WM_NCHITTEST) {
        // QCursor::pos() уже в логических пикселях, без DPI проблем
        QPoint localPos = mapFromGlobal(QCursor::pos());

        if (!isInteractiveArea(localPos)) {
            *result = HTTRANSPARENT;
            return true;
        }
        return false;
    }

    // Клик по SmartClip не забирает фокус у программы пользователя — кроме
    // клика в поле ввода (поиск): туда нужно печатать
    if (msg->message == WM_MOUSEACTIVATE) {
        QWidget *w = QApplication::widgetAt(QCursor::pos());
        bool input = false;
        for (; w && w != this; w = w->parentWidget())
            if (qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w)) { input = true; break; }
        *result = input ? MA_ACTIVATE : MA_NOACTIVATE;
        return true;
    }

    if (msg->message == WM_ACTIVATE) {
        if (LOWORD(msg->wParam) == WA_INACTIVE) {
            // lParam — хэндл окна которое ЗАХВАТИТ фокус
            // Если это окно нашего же процесса (диалог QInputDialog и т.п.) — не прячемся
            HWND activating = reinterpret_cast<HWND>(msg->lParam);
            DWORD activatingPid = 0;
            if (activating)
                GetWindowThreadProcessId(activating, &activatingPid);

            // Фокус ушёл в окно на другом мониторе — не прячемся
            if (activatingPid != GetCurrentProcessId()
                && !onOtherMonitor(reinterpret_cast<HWND>(winId()), activating))
                hideAnimated();
        }
    }

    return QWidget::nativeEvent(eventType, message, result);
}

// ─── closeAllOnDeactivate ────────────────────────────────────────────────────
// SmartClip потерял фокус, пока поверх него было открыто окно/меню.
void MainWindow::closeAllOnDeactivate()
{
    if (!isVisible() || m_fileDrag) return;
    // Фокус ушёл в окно нашего же процесса (например, системное окно выбора
    // файла или папки) — это не «клик мимо», ничего не закрываем
    DWORD pid = 0;
    if (HWND fg = GetForegroundWindow())
        GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return;
    // Пользователь работает на другом мониторе — ничего не закрываем
    if (onOtherMonitor(reinterpret_cast<HWND>(winId()), GetForegroundWindow())) return;

    // Открытые окна поверх SmartClip — сверху вниз
    QList<QPointer<QDialog>> dialogs;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *d = qobject_cast<QDialog*>(w))
            if (d->isVisible() && d->isModal()) {
                if (d->property("keepOnDeactivate").toBool()) return;  // Например, идёт обновление
                dialogs.prepend(d);
            }

    // Закрыли наш диалог, и Windows вернула фокус программе, где работал
    // пользователь: окно SmartClip остаётся открытым (клики мимо ловит onGlobalClick)
    HWND prev = reinterpret_cast<HWND>(m_prevFocusHwnd);
    if (dialogs.isEmpty() && !QApplication::activePopupWidget() && prev
        && GetAncestor(GetForegroundWindow(), GA_ROOT) == GetAncestor(prev, GA_ROOT))
        return;

    if (QWidget *popup = QApplication::activePopupWidget())
        popup->close();                       // Меню
    for (const QPointer<QDialog> &d : dialogs)
        if (d) d->reject();                   // Окна — каждое со своей анимацией ухода
    hideAnimated();                           // И сам SmartClip
}

// Кнопки История/Закрепы сдвинулись или изменили размер — «пилюля» за ними
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj->property("pickable").toBool())
        if (auto *card = qobject_cast<ClickableCard *>(obj))
            if (pickEvent(card, event)) return true;

    // Клик в поиск — забираем фокус, чтобы туда можно было печатать
    if (obj == m_searchEdit && event->type() == QEvent::MouseButtonPress)
        activateForInput();

    if (obj == m_centerSpacer && m_filesStrip &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize))
        m_filesStrip->place(m_centerSpacer->geometry());
    if ((obj == m_rightScroll || obj == m_topPanel) &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize))
        placeFoldersBar();
    if (obj == m_filesStrip && (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
                                event->type() == QEvent::Show || event->type() == QEvent::Hide))
        placeImportButtons();

    if ((obj == m_historyBtn || obj == m_pinsBtn) && m_viewPill &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        SmartButton *active = (m_viewMode == ViewMode::History) ? m_historyBtn : m_pinsBtn;
        if (obj == active)
            static_cast<ViewPill*>(m_viewPill)->moveTo(active, false);
    }
    return QWidget::eventFilter(obj, event);
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_carry && m_carry->holding()) m_carry->cancel();  // Спрятались, пока держали
    if (m_leftArc)  m_leftArc->watchHover(false);
    if (m_rightArc) m_rightArc->watchHover(false);
    if (m_topmostTimer) m_topmostTimer->stop();
    dropSqueeze();
    removeInputHooks();
    Media::Playback::get().stop();  // Закрыли SmartClip — звук и видео останавливаются
    if (m_videoViewer) m_videoViewer->hideNow();
    if (m_imageViewer) m_imageViewer->hide();  // Отдельное окно — прячем вместе с SmartClip
    // Окно спрятано без анимации ухода (вставка по клику на карточку) —
    // убираем пустую сцену. Прозрачное окно поверх экрана в полноэкранной
    // игре может снижать FPS, держать его без дела нельзя.
    if (m_overlay && m_overlay->isIdle())
        m_overlay->abort();
    m_hidePending = false;
}

// keyPressEvent — клавиша Escape скрывает окно SmartClip.
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_carry->active()) { m_carry->cancel(); return; }
    if (event->key() == Qt::Key_Escape) {
        // Сначала закрываем просмотр картинки, если он открыт. Раньше Esc прятал
        // всё окно, а просмотрщик оставался «открытым» и при следующем показе
        // SmartClip висел поверх всего.
        if (m_videoViewer && m_videoViewer->isVisible() && !m_videoViewer->isClosing())
            m_videoViewer->closeAnimated();
        else if (m_imageViewer && m_imageViewer->isVisible() && !m_imageViewer->isClosing())
            m_imageViewer->closeAnimated();
        else
            hideAnimated();
    } else
        QWidget::keyPressEvent(event);
}

// ─── paintEvent ───────────────────────────────────────────────────────────────
// Рисует тёмную подложку за панелями, когда включён «Тёмный фон за колонками»
// (solidPanels). Без него окно прозрачное — видны только карточки и кнопки.
// Когда solidPanels = true — рисуем тёмный полупрозрачный прямоугольник
// для каждой панели (верх, полоса папок, левая/правая колонка, низ).
void MainWindow::paintEvent(QPaintEvent *event)
{
    if (AppSettings::get().solidPanels()) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Тёмный полупрозрачный цвет панелей (почти непрозрачный)
        const QColor panelColor(12, 12, 24, 210);
        const int    radius = 0; // колонки — без скругления, они у краёв экрана

        const int W = width();
        const int H = height();

        // ── Верхняя панель ────────────────────────────────────────────
        p.fillRect(0, 0, W, sc(BASE_TOP), panelColor);

        // ── Полоса папок (если видима) — над левой колонкой ───────────
        if (m_foldersBar && m_foldersBar->isVisible())
            p.fillRect(QRect(m_foldersBar->x() + sc(BASE_LEFT) + 18, m_foldersBar->y(),
                             qMax(0, m_foldersBar->width() - sc(BASE_LEFT) - 18), m_foldersBar->height()),
                       panelColor);

        // ── Левая колонка (с полосой папок сверху) ────────────────────
        int colTop = sc(BASE_TOP);
        int colH   = H - colTop - sc(BASE_BOT);
        p.fillRect(0, colTop, sc(BASE_LEFT) + 18, colH, panelColor);

        // ── Правая колонка ────────────────────────────────────────────
        p.fillRect(W - sc(BASE_RIGHT) - 18, colTop, sc(BASE_RIGHT) + 18, colH, panelColor);

        // ── Нижняя панель ─────────────────────────────────────────────
        p.fillRect(0, H - sc(BASE_BOT), W, sc(BASE_BOT), panelColor);
    }

    QWidget::paintEvent(event);
}

// ─── Авто-обновление ─────────────────────────────────────────────────────────
// onUpdateAvailable вызывается из UpdateChecker через сигнал updateAvailable().
// Запоминаем данные для плашки в настройках и сразу показываем диалог.
// Раньше для patch-версий (1.0.1 → 1.0.2) диалог не показывался вообще —
// только плашка в настройках, которую никто не замечал. А все релизы SmartClip
// пока как раз patch, поэтому друзья просто не узнавали об обновлениях.
void MainWindow::onUpdateAvailable(const QString &version, const QString &downloadUrl,
                                   const QString &releaseNotes, const QString &sha256)
{
    m_pendingUpdateVersion = version;
    m_pendingUpdateUrl     = downloadUrl;
    m_pendingReleaseNotes  = releaseNotes;
    m_pendingUpdateSha256  = sha256;
    // Сам диалог НЕ открываем: проверка идёт через 10с после запуска, и окно
    // выскочило бы поверх игры/полноэкранного видео. main.cpp показывает
    // уведомление в трее, а диалог откроется при клике по нему или когда
    // пользователь сам откроет SmartClip (см. showWindow).
    // Мелкое обновление (2.0.1) само не предлагаем: точка на шестерёнке и
    // плашка вверху настроек — посмотреть, когда удобно.
    m_updatePromptShown = !UpdateChecker::isMajorUpdate(version);
    if (m_settingsBtn) {
        m_settingsBtn->setBadge(true);
        m_settingsBtn->setToolTip(tr("Настройки — доступно обновление %1").arg(version));
    }
}

void MainWindow::showPendingUpdate()
{
    if (m_pendingUpdateVersion.isEmpty()) return;
    m_updatePromptShown = true;
    showUpdateDialog(m_pendingUpdateVersion, m_pendingUpdateUrl, m_pendingReleaseNotes);
}

// ─── showUpdateDialog ─────────────────────────────────────────────────────────
// Диалог «Доступна новая версия» с прогрессом скачивания.
// Отдельный метод (без фильтра major/minor) — вызывается и из onUpdateAvailable,
// и напрямую из плашки в настройках (сигнал updateRequested).
void MainWindow::showUpdateDialog(const QString &version, const QString &downloadUrl,
                                  const QString &releaseNotes)
{
    // Флаг ДО showWindow(): иначе showWindow() решит, что обновление ещё
    // не предлагали, и откроет второй такой же диалог
    m_updatePromptShown = true;

    // Показываем окно чтобы диалог не спрятался за треем
    showWindow();

    // ── Окно «Доступно обновление» ────────────────────────────────────────────
    const bool major = UpdateChecker::isMajorUpdate(version);
    AppDialog dlg(this);
    dlg.setWindowTitle(tr("Обновление SmartClip"));
    // Случайный клик мимо не должен обрывать скачивание обновления
    dlg.setProperty("keepOnDeactivate", true);
    dlg.setMinimumWidth(520);

    // Кладём всё прямо в dlg.body() — AppDialog уже создал его внутри
    QWidget *bodyW = dlg.body();
    auto *layout = new QVBoxLayout(bodyW);
    layout->setContentsMargins(22, 6, 22, 16);
    layout->setSpacing(12);

    // Шапка: значок программы, версия и вид обновления
    auto *head = new QHBoxLayout();
    head->setSpacing(14);
    // Значок программы (в .ico есть размеры до 256 px — чёткий на любом масштабе)
    auto *icon = new QLabel(bodyW);
    icon->setPixmap(QIcon(":/icons/smartclip.ico").pixmap(QSize(56, 56), devicePixelRatioF()));
    icon->setFixedSize(56, 56);
    icon->setAlignment(Qt::AlignCenter);
    auto *headText = new QVBoxLayout();
    headText->setSpacing(4);
    auto *title = new QLabel(tr("SmartClip %1").arg(version), bodyW);
    title->setStyleSheet("color:#fff; font-size:18px; font-weight:600;");
    auto *chips = new QHBoxLayout();
    chips->setSpacing(8);
    auto *kind = new QLabel(major ? tr("Большое обновление") : tr("Небольшое обновление"), bodyW);
    kind->setStyleSheet(major
        ? "color:#0e1a2c; background:rgba(120,190,255,235); border-radius:9px; padding:2px 9px; font-size:11px; font-weight:600;"
        : "color:#cfe3ff; background:rgba(120,190,255,40); border-radius:9px; padding:2px 9px; font-size:11px;");
    auto *current = new QLabel(tr("у вас сейчас %1").arg(QLatin1String(APP_VERSION)), bodyW);
    current->setStyleSheet("color:rgba(255,255,255,120); font-size:11px;");
    chips->addWidget(kind);
    chips->addWidget(current);
    chips->addStretch();
    headText->addWidget(title);
    headText->addLayout(chips);
    head->addWidget(icon);
    head->addLayout(headText, 1);
    layout->addLayout(head);

    // «Что нового» — на языке интерфейса (в описании релиза языки разделены метками)
    const QString notes = UpdateChecker::localizedNotes(releaseNotes, AppSettings::get().language());
    auto *notesHdr = new QLabel(tr("Что нового"), bodyW);
    notesHdr->setStyleSheet("color:rgba(255,255,255,200); font-size:12px; font-weight:600;");
    layout->addWidget(notesHdr);
    if (!notes.isEmpty()) {
        auto *notesView = new QTextBrowser(bodyW);
        notesView->setOpenExternalLinks(true);
        // Описание на GitHub — в Markdown (списки, жирный и т.п.); взятое со
        // страницы релиза (запасной путь) — уже в HTML
        if (UpdateChecker::notesAreHtml(releaseNotes)) notesView->setHtml(notes);
        else                                           notesView->setMarkdown(notes);
        notesView->setFrameShape(QFrame::NoFrame);
        notesView->setStyleSheet(
            "QTextBrowser{background:rgba(255,255,255,7); color:rgba(235,238,250,220);"
            "border:1px solid rgba(255,255,255,22); border-radius:10px; padding:10px; font-size:12px;}");
        notesView->document()->setDocumentMargin(4);
        // Высота — по тексту: короткое описание без пустого места, длинное — с прокруткой
        notesView->ensurePolished();  // Сначала шрифт из стиля — иначе высота считается крупным шрифтом
        notesView->document()->setDefaultFont(notesView->font());
        // Высота — по тексту: короткое описание без пустого места, длинное —
        // с прокруткой. Размер известен, когда текст свёрстан по ширине окна.
        notesView->setFixedHeight(120);
        connect(notesView->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                notesView, [notesView](const QSizeF &sz) {
            notesView->setFixedHeight(qBound(70, int(sz.height()) + 24, 300));
        });
        layout->addWidget(notesView);
    } else {
        // Описание не пришло (запасной путь проверки обновлений) — ссылка на GitHub
        auto *link = new QLabel(QString("<a style='color:#9cc8ff' href='https://github.com/%1/releases/latest'>%2</a>")
                                    .arg(QLatin1String(GITHUB_REPO), tr("Посмотреть, что нового, на GitHub")), bodyW);
        link->setOpenExternalLinks(true);
        link->setStyleSheet("font-size:12px;");
        layout->addWidget(link);
    }

    // Прогресс-бар (скрыт до начала загрузки)
    auto *progress = new QProgressBar(bodyW);
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setTextVisible(false);
    progress->setFixedHeight(6);
    progress->setStyleSheet(
        "QProgressBar{background:rgba(255,255,255,20);border:none;border-radius:3px;}"
        "QProgressBar::chunk{background:rgba(120,190,255,235);border-radius:3px;}");
    progress->hide();

    auto *statusLbl = new QLabel("", bodyW);
    statusLbl->setStyleSheet("color:rgba(255,255,255,140); font-size:11px;");
    statusLbl->setAlignment(Qt::AlignCenter);
    statusLbl->setWordWrap(true);
    statusLbl->hide();

    auto *btnRow    = new QHBoxLayout();
    auto *updateBtn = new SmartButton(tr("Скачать и установить"), bodyW);
    auto *laterBtn  = new SmartButton(tr("Позже"), bodyW);
    updateBtn->setBtnStyle(QColor(28, 52, 86, 245), QColor(200, 228, 255));
    updateBtn->setFixedHeight(36);
    updateBtn->setMinimumWidth(190);
    laterBtn->setBtnStyle(QColor(30, 30, 50, 200), QColor(180, 180, 200));
    laterBtn->setFixedHeight(36);
    btnRow->addStretch();
    btnRow->addWidget(laterBtn);
    btnRow->addWidget(updateBtn);

    layout->addSpacing(2);
    layout->addWidget(progress);
    layout->addWidget(statusLbl);
    layout->addLayout(btnRow);

    connect(laterBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    // ── Скачивание ────────────────────────────────────────────────────────────
    // Ожидаемый SHA-256 (GitHub отдаёт его в API) — только если это тот же файл
    const QString expectedSha = (downloadUrl == m_pendingUpdateUrl) ? m_pendingUpdateSha256 : QString();
    QDialog *dlgPtr = &dlg;

    // Показать ошибку и дать пользователю повторить попытку или закрыть окно.
    // (Раньше кнопки оставались выключенными — окно «зависало» навсегда.)
    auto fail = [=](const QString &msg) {
        progress->setRange(0, 100);
        progress->hide();
        statusLbl->setText(msg);
        updateBtn->setText(tr("Повторить"));
        updateBtn->setEnabled(true);
        laterBtn->setEnabled(true);
    };

    // Запустить установщик с правами администратора. Если пользователь
    // отказался в окне UAC — SmartClip НЕ закрываем (раньше закрывался молча).
    auto launchInstaller = [=](const QString &exePath, bool updateMode) {
        const std::wstring file = QDir::toNativeSeparators(exePath).toStdWString();
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = L"runas";
        sei.lpFile       = file.c_str();
        sei.lpParameters = updateMode ? L"/update" : nullptr;
        sei.nShow        = SW_SHOW;
        if (!ShellExecuteExW(&sei)) {
            const DWORD err = GetLastError();
            if (err == ERROR_CANCELLED)
                fail(tr("Установка отменена. Нажмите «Повторить», когда будете готовы."));
            else
                fail(tr("Не удалось запустить установщик (код %1).").arg(err));
            return;
        }
        statusLbl->setText(tr("Запуск установщика..."));
        dlgPtr->accept();
        // Установщик сам закроет SmartClip, но выходим и сами — чтобы файлы освободились.
        // После обновления SmartClip сам откроет окно — видно, что всё готово
        AppSettings::get().setOpenOnStart(true);
        QTimer::singleShot(300, qApp, []() { QCoreApplication::quit(); });
    };

    // Сколько раз подряд связь обрывалась при скачивании (один раз повторяем сами)
    auto dropped = std::make_shared<int>(0);

    connect(updateBtn, &QPushButton::clicked, &dlg, [=]() {
        updateBtn->setEnabled(false);
        laterBtn->setEnabled(false);
        progress->setValue(0);
        progress->show();
        statusLbl->show();
        statusLbl->setText(tr("Подключение..."));

        // Временная папка — старые файлы чистим перед каждой попыткой
        const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                + "/SmartClipUpdate";
        QDir(tempDir).removeRecursively();
        QDir().mkpath(tempDir);

        // Новый формат релиза — один .exe. Старый (до 1.0.5) — .zip с папкой _data.
        const bool isZip = QUrl(downloadUrl).path().endsWith(".zip", Qt::CaseInsensitive);
        const QString tempFile = tempDir + (isZip ? QString("/update.zip")
                                                  : QString("/SmartClip_Installer_v%1.exe").arg(version));

        auto *nam = new QNetworkAccessManager(dlgPtr);
        QNetworkRequest req{QUrl{downloadUrl}};
        // GitHub отдаёт 302 на CDN (objects.githubusercontent.com)
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(60000);  // 60с без данных — считаем что связь пропала
        auto *reply = nam->get(req);

        connect(reply, &QNetworkReply::downloadProgress, dlgPtr,
                [progress, statusLbl](qint64 received, qint64 total) {
            if (total > 0) {
                progress->setValue(static_cast<int>(received * 100 / total));
                statusLbl->setText(QString("%1 / %2 MB")
                    .arg(received / 1048576.0, 0, 'f', 1)
                    .arg(total    / 1048576.0, 0, 'f', 1));
            }
        });

        connect(reply, &QNetworkReply::finished, dlgPtr, [=]() {
            reply->deleteLater();
            nam->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const bool network = (status == 0);  // Ответа сервера нет — оборвалась связь
                // На мобильном интернете связь часто моргает — первый обрыв повторяем сами
                if (network && ++*dropped < 2) {
                    statusLbl->setText(tr("Связь прервалась, пробую ещё раз..."));
                    QTimer::singleShot(3000, dlgPtr, [updateBtn]() { emit updateBtn->clicked(); });
                    return;
                }
                *dropped = 0;
                statusLbl->setToolTip(reply->errorString());  // Подробности — при наведении
                fail(network ? tr("Связь прервалась при скачивании. Проверьте интернет и нажмите «Повторить».")
                             : tr("Не удалось скачать обновление (ошибка %1). Попробуйте позже.").arg(status));
                return;
            }
            *dropped = 0;
            const QByteArray data = reply->readAll();
            if (data.size() < 1024) {
                fail(tr("Сервер вернул пустой файл. Попробуйте позже."));
                return;
            }

            // Проверка целостности: файл скачался полностью и не подменён
            if (!expectedSha.isEmpty()) {
                const QString actual = QString::fromLatin1(
                    QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
                if (actual.compare(expectedSha, Qt::CaseInsensitive) != 0) {
                    fail(tr("Файл повреждён при скачивании (не совпала контрольная сумма)."));
                    return;
                }
            }

            QFile f(tempFile);
            if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
                fail(tr("Ошибка записи файла: ") + f.errorString());
                return;
            }
            f.close();

            if (!isZip) {
                launchInstaller(tempFile, true);
                return;
            }

            // ── Старый формат: распаковываем .zip через PowerShell ───────────
            statusLbl->setText(tr("Распаковка..."));
            progress->setRange(0, 0);  // «Бегущая» полоска — прогресса распаковки нет

            // Родитель — диалог: если его закроют, процесс удалится вместе с ним,
            // и обработчик ниже не обратится к уже удалённым виджетам.
            auto *proc = new QProcess(dlgPtr);
            // Апостроф в пути (папка пользователя O'Brien) ломал команду —
            // внутри '...' в PowerShell его нужно удваивать.
            auto psQuote = [](const QString &p) {
                QString n = QDir::toNativeSeparators(p);
                n.replace("'", "''");
                return "'" + n + "'";
            };
            const QString cmd = QString("Expand-Archive -Force -LiteralPath %1 -DestinationPath %2")
                                    .arg(psQuote(tempFile), psQuote(tempDir));

            connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), dlgPtr,
                    [=](int, QProcess::ExitStatus) {
                proc->deleteLater();
                progress->setRange(0, 100);

                // Ищем Installer.exe рекурсивно (структура папок внутри zip может отличаться)
                QDirIterator it(tempDir, QStringList{"Installer.exe"}, QDir::Files,
                                QDirIterator::Subdirectories);
                if (!it.hasNext()) {
                    fail(tr("Installer.exe не найден в архиве"));
                    return;
                }
                launchInstaller(it.next(), false);
            });

            proc->start("powershell", {"-NoProfile", "-NonInteractive", "-Command", cmd});
        });
    });

    dlg.exec();
}

// Деструктор — Qt сам удаляет дочерние виджеты (m_imageViewer и т.д.).
// = default означает «используй автоматически сгенерированный деструктор».
MainWindow::~MainWindow() = default;

#include "MainWindow.moc"
