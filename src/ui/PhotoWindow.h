#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  PhotoWindow.h — фото в отдельном обычном окне («Открыть в окне»)             ║
// ║                                                                              ║
// ║  Обычное окно Windows: своя кнопка на панели задач, можно таскать,           ║
// ║  свернуть и закрыть, экран не затемняет, остаётся, когда SmartClip закрыт.   ║
// ║  Кнопка-«кнопка» сверху справа — «поверх всех окон», как в Калькуляторе.     ║
// ║                                                                              ║
// ║  Окно всегда ровно по пропорциям фото — без полос: тянешь за край или    ║
// ║  угол, второй размер подстраивается сам (keepAspectSizing).               ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QPainter>
#include <QPixmap>
#include <QImageReader>
#include <QFileInfo>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QAbstractButton>
#include <QKeyEvent>
#include <windows.h>
#include <dwmapi.h>

#include "SmartButton.h"

// Тянут край или угол окна — второй размер подстраиваем, чтобы окно было
// ровно в пропорциях картинки a (ширина/высота): без полос по бокам.
// minW — меньше не сжимаем. Для WM_SIZING; true — размер поправлен
inline bool keepAspectSizing(MSG *msg, double a, int minW, qintptr *result)
{
    if (msg->message != WM_SIZING || a <= 0) return false;
    RECT *r = reinterpret_cast<RECT *>(msg->lParam);
    const int edge = int(msg->wParam);
    RECT wr, cr;
    GetWindowRect(msg->hwnd, &wr);
    GetClientRect(msg->hwnd, &cr);
    const int fw = (wr.right - wr.left) - cr.right;   // Рамка и заголовок
    const int fh = (wr.bottom - wr.top) - cr.bottom;
    double w = qMax<double>(minW, (r->right - r->left) - fw), h = (r->bottom - r->top) - fh;
    const bool left = edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT;
    const bool top  = edge == WMSZ_TOP  || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT;
    const bool byHeight = edge == WMSZ_TOP || edge == WMSZ_BOTTOM
        // Угол: за основу — то, что тянут сильнее
        || ((edge != WMSZ_LEFT && edge != WMSZ_RIGHT) && h / qMax(1L, cr.bottom) > w / qMax(1L, cr.right));
    if (byHeight) { h = qMax(h, minW / a); w = h * a; }
    else h = w / a;
    if (left) r->left = r->right - LONG(w + fw); else r->right = r->left + LONG(w + fw);
    if (top) r->top = r->bottom - LONG(h + fh); else r->bottom = r->top + LONG(h + fh);
    *result = TRUE;
    return true;
}

// Круглая полупрозрачная кнопка с булавкой («поверх всех окон»); нажата —
// светится. Общая для окна фото и окна видео
class WindowPinButton : public QAbstractButton
{
public:
    explicit WindowPinButton(QWidget *parent) : QAbstractButton(parent)
    {
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
    }
    bool on = false;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool hot = underMouse();
        p.setPen(QPen(QColor(255, 255, 255, on ? 160 : (hot ? 110 : 60)), 1));
        p.setBrush(on ? QColor(60, 110, 200, 220) : QColor(20, 20, 30, hot ? 210 : 150));
        p.drawEllipse(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
        QFont f(SmartButton::iconFontFamily());
        f.setPixelSize(13);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, on ? 255 : 200));
        p.drawText(rect(), Qt::AlignCenter, QString(QChar(on ? 0xE841 : 0xE718)));  // Закреплено / булавка
    }
    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e) override { update(); QAbstractButton::leaveEvent(e); }
};

class PhotoWindow : public QWidget
{
public:
    explicit PhotoWindow(const QString &path)
        : QWidget(nullptr, Qt::Window)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" — SmartClip"));
        setWindowIcon(qApp->windowIcon());
        setMinimumSize(80, 60);  // Меньше — держит keepAspectSizing (по пропорциям)

        QImageReader reader(path);
        reader.setAutoTransform(true);  // Поворот из EXIF, как у фото с телефона
        m_pix = QPixmap::fromImage(reader.read());

        // Кнопка «поверх всех окон»
        m_pin = new WindowPinButton(this);
        m_pin->setToolTip(QApplication::translate("PhotoWindow", "Поверх всех окон"));
        QObject::connect(m_pin, &QAbstractButton::clicked, this, [this]() { setOnTop(!m_onTop); });

        // Начальный размер — фото целиком, но не больше 60% экрана
        QScreen *scr = QGuiApplication::screenAt(QCursor::pos());
        if (!scr) scr = QGuiApplication::primaryScreen();
        const QRect av = scr->availableGeometry();
        // Ровно в пропорциях фото: крошечное — увеличиваем до ~240 по меньшей стороне
        QSize sz = m_pix.isNull() ? QSize(480, 360) : m_pix.size() / m_pix.devicePixelRatio();
        if (qMin(sz.width(), sz.height()) < 240) sz.scale(sz * (240.0 / qMax(1, qMin(sz.width(), sz.height()))), Qt::KeepAspectRatio);
        sz.scale(sz.boundedTo(QSize(av.width() * 6 / 10, av.height() * 6 / 10)), Qt::KeepAspectRatio);
        resize(sz);
        move(av.center() - QPoint(sz.width() / 2, sz.height() / 2));

        // Тёмная рамка окна, как у тёмной темы Windows
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/,
                              &dark, sizeof(dark));
    }


    // Открыли из SmartClip, а он остаётся открытым — окно поверх него (колонки
    // SmartClip всегда поверх всего). SmartClip закрылся — обычное окно
    void setAboveSmartClip(bool on) { m_above = on; applyTopmost(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(17, 17, 24));
        if (m_pix.isNull()) return;
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSizeF s = QSizeF(m_pix.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio);
        p.drawPixmap(QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height()),
                     m_pix, QRectF(m_pix.rect()));
    }

    void resizeEvent(QResizeEvent *) override
    {
        m_pin->move(width() - m_pin->width() - 10, 10);
    }

    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape) close();
        else QWidget::keyPressEvent(e);
    }

    // Размер — строго по пропорциям фото (см. шапку файла)
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override
    {
        if (!m_pix.isNull() && keepAspectSizing(static_cast<MSG *>(message), double(m_pix.width()) / m_pix.height(), 160, result))
            return true;
        return QWidget::nativeEvent(type, message, result);
    }

private:
    // Поверх всех окон — через Windows, без пересоздания окна
    void setOnTop(bool on)
    {
        m_onTop = on;
        m_pin->on = on;
        m_pin->setToolTip(on ? QApplication::translate("PhotoWindow", "Не поверх других окон")
                             : QApplication::translate("PhotoWindow", "Поверх всех окон"));
        m_pin->update();
        applyTopmost();
    }
    // Поверх — если закрепили булавкой или пока открыт SmartClip
    void applyTopmost()
    {
        SetWindowPos(reinterpret_cast<HWND>(winId()), (m_onTop || m_above) ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    QPixmap    m_pix;
    WindowPinButton *m_pin = nullptr;
    bool       m_onTop = false;
    bool       m_above = false;  // Поверх открытого SmartClip
};
