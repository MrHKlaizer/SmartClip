#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  VideoWindow.h — видео в отдельном обычном окне («Открыть в окне»)            ║
// ║                                                                              ║
// ║  Как окно фото (PhotoWindow.h): своя кнопка на панели задач, можно таскать,  ║
// ║  свернуть и закрыть, остаётся, когда SmartClip закрыт; булавка — «поверх     ║
// ║  всех окон». Видео не искажается (по бокам — полосы).                        ║
// ║                                                                              ║
// ║  Играет со звуком, своим плеером (видео в карточках — отдельно).            ║
// ║  Клик или пробел — пауза, ← → — на 5 секунд, полоса снизу — перемотка        ║
// ║  (видна, пока мышь над окном или на паузе), Esc — закрыть.                   ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QVideoFrame>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <windows.h>
#include <dwmapi.h>

#include "PhotoWindow.h"   // WindowPinButton

class VideoWindow : public QWidget
{
public:
    // muted — без звука (тесты: не шуметь у пользователя в колонках)
    explicit VideoWindow(const QString &path, bool muted = false)
        : QWidget(nullptr, Qt::Window)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" — SmartClip"));
        setWindowIcon(qApp->windowIcon());
        setMinimumSize(240, 160);
        setMouseTracking(true);

        m_audio.setMuted(muted);
        m_player.setAudioOutput(&m_audio);
        m_player.setVideoSink(&m_sink);
        m_player.setLoops(QMediaPlayer::Infinite);  // Короткие ролики — по кругу, как в карточке
        connect(&m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &f) {
            m_frame = f.toImage();
            if (!m_sized && !m_frame.isNull()) { m_sized = true; fitToVideo(m_frame.size()); }
            update();
        });
        connect(&m_player, &QMediaPlayer::positionChanged, this, [this]() { update(); });
        connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this]() { update(); });
        m_player.setSource(QUrl::fromLocalFile(path));
        m_player.play();

        m_pin = new WindowPinButton(this);
        m_pin->setToolTip(QApplication::translate("PhotoWindow", "Поверх всех окон"));
        QObject::connect(m_pin, &QAbstractButton::clicked, this, [this]() { setOnTop(!m_onTop); });

        // Полоса перемотки прячется, когда мышь ушла
        m_hideBar.setSingleShot(true);
        m_hideBar.setInterval(1500);
        connect(&m_hideBar, &QTimer::timeout, this, [this]() { m_barShown = false; update(); });

        fitToVideo(QSize(1280, 720));  // Пока кадра нет — 16:9; первый кадр поправит
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/,
                              &dark, sizeof(dark));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(17, 17, 24));
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);
        if (!m_frame.isNull()) {
            const QSizeF s = QSizeF(m_frame.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio);
            p.drawImage(QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height()),
                        m_frame, QRectF(m_frame.rect()));
        }
        const bool paused = m_player.playbackState() != QMediaPlayer::PlayingState;
        if (paused) {  // Пауза — большой ▶ посередине
            const QRectF c(width() / 2.0 - 34, height() / 2.0 - 34, 68, 68);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 140));
            p.drawEllipse(c);
            QPainterPath tri;
            tri.moveTo(c.center() + QPointF(-9, -15));
            tri.lineTo(c.center() + QPointF(17, 0));
            tri.lineTo(c.center() + QPointF(-9, 15));
            tri.closeSubpath();
            p.fillPath(tri, QColor(255, 255, 255, 230));
        }
        if (m_barShown || paused) {
            // Полоса снизу: пройдено / всего и время
            const QRect bar = barRect();
            QLinearGradient g(0, height() - 60, 0, height());
            g.setColorAt(0, QColor(0, 0, 0, 0));
            g.setColorAt(1, QColor(0, 0, 0, 170));
            p.fillRect(QRect(0, height() - 60, width(), 60), g);
            const qreal k = m_player.duration() > 0 ? qreal(m_player.position()) / m_player.duration() : 0;
            p.setBrush(QColor(255, 255, 255, 60));
            p.drawRoundedRect(QRectF(bar.left(), bar.center().y() - 2, bar.width(), 4), 2, 2);
            p.setBrush(QColor(120, 170, 255));
            p.drawRoundedRect(QRectF(bar.left(), bar.center().y() - 2, bar.width() * k, 4), 2, 2);
            p.drawEllipse(QPointF(bar.left() + bar.width() * k, bar.center().y()), 6, 6);
            auto t = [](qint64 ms) { const qint64 s = ms / 1000; return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0')); };
            QFont f = font();
            f.setPixelSize(12);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 210));
            p.drawText(QRect(bar.left(), bar.bottom() + 2, bar.width(), 16), Qt::AlignRight | Qt::AlignVCenter,
                       t(m_player.position()) + " / " + t(m_player.duration()));
        }
    }

    void resizeEvent(QResizeEvent *) override { m_pin->move(width() - m_pin->width() - 10, 10); }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) return;
        if (barRect().adjusted(0, -10, 0, 10).contains(e->position().toPoint()) && (m_barShown || paused())) {
            m_seeking = true;
            seekTo(e->position().x());
            return;
        }
        togglePlay();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_seeking) { seekTo(e->position().x()); return; }
        m_barShown = true;
        m_hideBar.start();
        update();
    }
    void mouseReleaseEvent(QMouseEvent *) override { m_seeking = false; }
    void leaveEvent(QEvent *) override { m_hideBar.start(300); }

    void keyPressEvent(QKeyEvent *e) override
    {
        switch (e->key()) {
        case Qt::Key_Escape: close(); return;
        case Qt::Key_Space:  togglePlay(); return;
        case Qt::Key_Left:   m_player.setPosition(qMax<qint64>(0, m_player.position() - 5000)); return;
        case Qt::Key_Right:  m_player.setPosition(qMin(m_player.duration(), m_player.position() + 5000)); return;
        default: QWidget::keyPressEvent(e);
        }
    }
    void closeEvent(QCloseEvent *e) override
    {
        m_player.stop();
        QWidget::closeEvent(e);
    }

private:
    bool paused() const { return m_player.playbackState() != QMediaPlayer::PlayingState; }
    void togglePlay() { if (paused()) m_player.play(); else m_player.pause(); }
    QRect barRect() const { return QRect(16, height() - 34, width() - 32, 12); }
    void seekTo(qreal x)
    {
        const QRect bar = barRect();
        const qreal k = qBound(0.0, (x - bar.left()) / qMax(1, bar.width()), 1.0);
        m_player.setPosition(qint64(k * m_player.duration()));
    }
    // Размер окна — по видео, но не больше 60% экрана (на том экране, где мышь)
    void fitToVideo(const QSize &video)
    {
        QScreen *scr = QGuiApplication::screenAt(QCursor::pos());
        if (!scr) scr = QGuiApplication::primaryScreen();
        const QRect av = scr->availableGeometry();
        QSize sz = video.scaled(video.boundedTo(QSize(av.width() * 6 / 10, av.height() * 6 / 10)), Qt::KeepAspectRatio)
                       .expandedTo(QSize(320, 180));
        const QPoint c = isVisible() ? geometry().center() : av.center();
        resize(sz);
        move(c - QPoint(sz.width() / 2, sz.height() / 2));
    }
    void setOnTop(bool on)
    {
        m_onTop = on;
        m_pin->on = on;
        m_pin->setToolTip(on ? QApplication::translate("PhotoWindow", "Не поверх других окон")
                             : QApplication::translate("PhotoWindow", "Поверх всех окон"));
        m_pin->update();
        SetWindowPos(reinterpret_cast<HWND>(winId()), on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    QMediaPlayer m_player;
    QAudioOutput m_audio;
    QVideoSink   m_sink;
    QImage       m_frame;
    bool         m_sized = false;     // Под размер видео уже подогнали
    bool         m_barShown = false;  // Полоса перемотки видна (мышь над окном)
    bool         m_seeking = false;   // Тянем по полосе
    QTimer       m_hideBar;
    WindowPinButton *m_pin = nullptr;
    bool         m_onTop = false;
};
