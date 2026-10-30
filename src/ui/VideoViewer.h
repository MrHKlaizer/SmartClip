// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  VideoViewer.h — Большой просмотр видео (SmartClip 2.1)                     ║
// ║                                                                              ║
// ║  Как просмотр картинок: видео вылетает из своей карточки и встаёт по        ║
// ║  центру экрана, фон затемняется. Внизу — ▶/⏸, перемотка, время, звук.       ║
// ║  Клик по видео — пауза, двойной клик / Esc — закрыть (видео возвращается    ║
// ║  в карточку и продолжает играть там).                                       ║
// ║  Отдельное окно поверх SmartClip, фокус не отбирает (как ImageViewer).      ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QEasingCurve>
#include <QPointer>
#include <QMouseEvent>
#include <windows.h>
#include "MediaCards.h"
#include "Anim.h"

class VideoViewer : public QWidget
{
public:
    explicit VideoViewer(QWidget *parent)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::NoDropShadowWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        m_surface = new Media::VideoSurface(this);
        m_surface->setRadius(10);

        m_play = new Media::RoundButton(QChar(Media::GlyphPause), 38, this);
        m_mute = new Media::RoundButton(QChar(Media::GlyphMute), 32, this);
        m_bar  = new Media::ProgressLine(this);
        m_bar->setFixedHeight(18);
        connect(m_play, &QAbstractButton::clicked, this, []() { Media::Playback::get().toggle(); });
        connect(m_mute, &QAbstractButton::clicked, this, []() {
            auto *out = Media::Playback::get().output();
            out->setMuted(!out->isMuted());
        });
        m_bar->onSeek = [](qint64 ms) { Media::Playback::get().player()->setPosition(ms); };

        auto &pb = Media::Playback::get();
        auto refresh = [this]() { refreshControls(); };
        connect(pb.player(), &QMediaPlayer::positionChanged, this, refresh);
        connect(pb.player(), &QMediaPlayer::durationChanged, this, refresh);
        connect(pb.player(), &QMediaPlayer::playbackStateChanged, this, refresh);
        connect(pb.output(), &QAudioOutput::mutedChanged, this, refresh);

        m_tick.setTimerType(Qt::PreciseTimer);
        m_tick.setInterval(6);
        connect(&m_tick, &QTimer::timeout, this, [this]() { tick(); });
        hide();
    }

    // Открыть видео из карточки card (его поверхность — cardSurface)
    void open(QLabel *card, Media::VideoSurface *cardSurface, const QString &file)
    {
        m_card = card;
        m_cardSurface = cardSurface;
        if (parentWidget()) setGeometry(parentWidget()->geometry());
        auto &pb = Media::Playback::get();
        if (pb.isOwner(card)) pb.setSink(m_surface->sink());                   // Уже играет в карточке
        else                  pb.start(file, card, m_surface->sink(), true);   // Как в карточке — без звука
        if (pb.player()->playbackState() != QMediaPlayer::PlayingState) pb.player()->play();

        m_closing = false;
        m_from = 0; m_to = 1; m_t = 0;
        m_duration = Anim::enabled(Anim::Kind::Viewer) ? Anim::ms(Anim::Kind::Viewer, 420) : 1;
        m_curve = QEasingCurve(QEasingCurve::OutCubic);
        show();
        HWND h = reinterpret_cast<HWND>(winId());
        SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        m_clock.start();
        m_tick.setInterval(Anim::frameMs(this));  // По частоте монитора
        m_tick.start();
        tick();
        refreshControls();
    }

    // Открыть видеофайл без карточки (Ctrl+клик в полоске файлов или в папке
    // с полки). Закрыли — видео останавливается: возвращаться ему некуда
    void openFile(const QString &file)
    {
        m_card = nullptr;
        m_cardSurface = nullptr;
        if (parentWidget()) setGeometry(parentWidget()->geometry());
        auto &pb = Media::Playback::get();
        pb.start(file, this, m_surface->sink(), true);  // Как из карточки — без звука, включить кнопкой
        pb.player()->play();
        m_closing = false;
        m_from = 0; m_to = 1; m_t = 0;
        m_duration = Anim::enabled(Anim::Kind::Viewer) ? Anim::ms(Anim::Kind::Viewer, 420) : 1;
        m_curve = QEasingCurve(QEasingCurve::OutCubic);
        show();
        HWND h = reinterpret_cast<HWND>(winId());
        SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        m_clock.start();
        m_tick.setInterval(Anim::frameMs(this));
        m_tick.start();
        tick();
        refreshControls();
    }

    // Закрыть: видео улетает обратно в карточку и продолжает играть там
    void closeAnimated()
    {
        if (!isVisible() || m_closing) return;
        m_closing = true;
        m_from = m_t; m_to = 0;
        m_duration = Anim::enabled(Anim::Kind::Viewer) ? qMax(1, int(Anim::ms(Anim::Kind::Viewer, 220) * m_t)) : 1;
        m_curve = QEasingCurve(QEasingCurve::InOutCubic);
        m_clock.start();
        m_tick.setInterval(Anim::frameMs(this));  // По частоте монитора
        m_tick.start();
    }

    // Спрятать сразу (SmartClip закрывается — проигрывание всё равно остановится)
    void hideNow()
    {
        m_tick.stop();
        m_closing = false;
        hide();
    }

    bool isClosing() const { return m_closing; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, int(215 * qBound(0.0, m_t, 1.0))));
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        // Клик по видео — пауза / продолжить; мимо — ничего
        if (e->button() == Qt::LeftButton && m_surface->geometry().contains(e->pos()) && !m_closing)
            Media::Playback::get().toggle();
    }
    void mouseDoubleClickEvent(QMouseEvent *) override { closeAnimated(); }

private:
    // Где видео на экране: вписано в 85% окна по своим пропорциям
    QRectF targetRect() const
    {
        QSize fs = m_surface->frameSize();
        if (fs.isEmpty() && m_cardSurface) fs = m_cardSurface->frameSize();
        if (fs.isEmpty()) fs = QSize(16, 9);
        const QSizeF sz = QSizeF(fs).scaled(QSizeF(width() * 0.85, (height() - 90) * 0.85), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - sz.width()) / 2, (height() - 90 - sz.height()) / 2 + 10), sz);
    }
    QRectF sourceRect() const
    {
        if (!m_cardSurface || !m_cardSurface->isVisible()) {
            const QRectF t = targetRect();
            return QRectF(t.center() - QPointF(t.width() * 0.4, t.height() * 0.4), t.size() * 0.8);
        }
        return QRectF(mapFromGlobal(m_cardSurface->mapToGlobal(QPoint(0, 0))), QSizeF(m_cardSurface->size()));
    }

    void tick()
    {
        const qreal pr = qMin(1.0, m_clock.nsecsElapsed() / 1e6 / qMax(1, m_duration));
        m_t = m_from + (m_to - m_from) * m_curve.valueForProgress(pr);
        const QRectF a = sourceRect(), b = targetRect();
        auto lerp = [this](qreal x, qreal y) { return x + (y - x) * m_t; };
        m_surface->setGeometry(QRectF(QPointF(lerp(a.left(), b.left()), lerp(a.top(), b.top())),
                                      QPointF(lerp(a.right(), b.right()), lerp(a.bottom(), b.bottom()))).toRect());
        placeControls(b);
        update();
        if (pr >= 1.0) {
            m_tick.stop();
            if (m_closing) {
                m_closing = false;
                hide();
                // Видео — обратно в карточку (играет там дальше)
                auto &pb = Media::Playback::get();
                if (pb.isOwner(m_card) && m_cardSurface) pb.setSink(m_cardSurface->sink());
                else if (pb.isOwner(this)) pb.stop();  // Открывали файл без карточки
            }
        }
    }

    void placeControls(const QRectF &video)
    {
        const bool show = m_t > 0.98 && !m_closing;
        for (QWidget *w : std::initializer_list<QWidget *>{m_play, m_mute, m_bar}) w->setVisible(show);
        const int y = int(video.bottom()) + 22;
        const int x0 = int(video.left()), w = int(video.width());
        m_play->move(x0, y - 10);
        m_mute->move(x0 + w - m_mute->width(), y - 7);
        m_bar->setGeometry(x0 + 50, y, w - 50 - m_mute->width() - 12, 18);
    }

    void refreshControls()
    {
        auto &pb = Media::Playback::get();
        m_play->setGlyph(QChar(pb.player()->playbackState() == QMediaPlayer::PlayingState
                                   ? Media::GlyphPause : Media::GlyphPlay));
        m_mute->setGlyph(QChar(pb.output()->isMuted() ? Media::GlyphMute : Media::GlyphVolume));
        m_bar->setState(pb.player()->position(), pb.player()->duration());
    }

    Media::VideoSurface          *m_surface = nullptr;
    QPointer<QLabel>              m_card;
    QPointer<Media::VideoSurface> m_cardSurface;
    Media::RoundButton           *m_play = nullptr, *m_mute = nullptr;
    Media::ProgressLine          *m_bar = nullptr;
    QTimer        m_tick;
    QElapsedTimer m_clock;
    QEasingCurve  m_curve;
    qreal         m_from = 0, m_to = 1, m_t = 0;
    int           m_duration = 1;
    bool          m_closing = false;
};
