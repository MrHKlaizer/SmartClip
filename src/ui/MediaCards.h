// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  MediaCards.h — Аудио и видео прямо в карточках (SmartClip 2.1)            ║
// ║                                                                              ║
// ║  • Аудио: круглая кнопка ▶ слева, внизу — полоска с временем (по ней       ║
// ║    можно перемотать). Клик по самой карточке, как и раньше, вставляет файл. ║
// ║  • Видео: кадр-превью; ▶ — играет прямо в карточке БЕЗ звука (значок 🔇 —   ║
// ║    включить звук), ⤢ — большой просмотр (VideoViewer.h).                   ║
// ║  • Одновременно играет что-то одно: запустили другое — прежнее встало.      ║
// ║  • Закрыли SmartClip — всё останавливается.                                 ║
// ║                                                                              ║
// ║  Играет FFmpeg (открытая библиотека, LGPL; см. main.cpp) — почти любые     ║
// ║  форматы, не зависит от кодеков Windows.                                    ║
// ║  Кадры видео рисуем сами (VideoSurface): окно SmartClip полупрозрачное, и   ║
// ║  стандартный QVideoWidget с отдельным системным окном в нём не виден.       ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QWidget>
#include <QAbstractButton>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QHash>
#include <QEvent>
#include <QMouseEvent>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QVideoFrame>
#include <QUrl>
#include <QCoreApplication>
#include <functional>
#include "SmartButton.h"   // SmartButton::iconFontFamily() — системный шрифт значков

namespace Media {

// «0:42», «12:05», «1:02:33»
inline QString timeText(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 s = ms / 1000;
    if (s >= 3600) return QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// ─── Один проигрыватель на всё ──────────────────────────────────────────────
class Playback : public QObject
{
public:
    static Playback &get()
    {
        static Playback *p = new Playback(qApp);
        return *p;
    }

    QMediaPlayer *player() const { return m_player; }
    QAudioOutput *output() const { return m_out; }
    bool isOwner(const QObject *o) const { return o && m_owner == o; }

    // Файл не играет (редкий формат) — что делать (задаёт MainWindow)
    std::function<void(const QString &file)> onUnsupported;

    // Играть файл для «владельца» (карточки). sink — куда рисовать кадры (для видео)
    void start(const QString &file, QObject *owner, QVideoSink *sink, bool muted)
    {
        m_errorReported = false;
        m_player->stop();
        m_owner = owner;
        m_file = file;
        m_player->setVideoSink(sink);
        m_out->setMuted(muted);
        m_player->setSource(QUrl::fromLocalFile(file));
        m_player->play();
        notify();  // Прежняя карточка вернёт свою кнопку ▶
    }
    void toggle()
    {
        if (m_player->playbackState() == QMediaPlayer::PlayingState) m_player->pause();
        else m_player->play();
    }
    void stop()
    {
        if (!m_owner) return;
        m_player->stop();
        m_player->setSource(QUrl());
        m_player->setVideoSink(nullptr);
        m_owner = nullptr;
        notify();
    }
    void setSink(QVideoSink *sink) { m_player->setVideoSink(sink); }

    // Сменился «владелец» или звук — карточки перерисовывают свои кнопки.
    // ctx — карточка: удалили её — подписка снимается сама.
    // (Раньше удалённые подписки оставались пустыми строками в списке, и он
    //  рос с каждой перестройкой карточек.)
    void listen(QObject *ctx, std::function<void()> f)
    {
        const quint64 id = ++m_nextListener;
        m_listeners.insert(id, std::move(f));
        QObject::connect(ctx, &QObject::destroyed, this, [this, id]() { m_listeners.remove(id); });
    }
    void notify()
    {
        const auto all = m_listeners;  // Копия: обработчик может удалить карточку
        for (const auto &f : all) if (f) f();
    }

private:
    explicit Playback(QObject *parent) : QObject(parent)
    {
        m_player = new QMediaPlayer(this);
        m_out = new QAudioOutput(this);
        m_player->setAudioOutput(m_out);
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error e, const QString &) {
            // Ошибка приходит несколько раз подряд — предлагаем внешний плеер один раз
            if (e == QMediaPlayer::NoError || !m_owner || m_errorReported) return;
            m_errorReported = true;
            const QString file = m_file;
            stop();
            if (onUnsupported) onUnsupported(file);
        });
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
            if (st == QMediaPlayer::EndOfMedia) { m_player->setPosition(0); m_player->pause(); }
        });
    }
    QHash<quint64, std::function<void()>> m_listeners;
    quint64 m_nextListener = 0;
    QMediaPlayer     *m_player = nullptr;
    QAudioOutput     *m_out = nullptr;
    QPointer<QObject> m_owner;
    QString           m_file;
    bool              m_errorReported = false;  // Об ошибке этого файла уже сообщили
};

// ─── Поверхность для кадров видео ───────────────────────────────────────────
class VideoSurface : public QWidget
{
public:
    explicit VideoSurface(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);  // Клики — карточке/окну под ней
        connect(&m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &f) {
            m_frame = f.toImage();
            update();
        });
    }
    QVideoSink *sink() { return &m_sink; }
    void setPoster(const QPixmap &p) { m_poster = p; update(); }
    void clearFrame() { m_frame = QImage(); update(); }
    QSize frameSize() const { return !m_frame.isNull() ? m_frame.size() : m_poster.size(); }
    // Радиус скругления углов (в карточке — как у карточки)
    void setRadius(qreal r) { m_radius = r; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_radius > 0) {
            QPainterPath clip; clip.addRoundedRect(QRectF(rect()), m_radius, m_radius);
            p.setClipPath(clip);
        }
        // Плёнка: тёмные края с перфорацией — сразу видно, что это видео
        QRectF box(rect());
        if (m_film) {
            p.fillRect(rect(), QColor(8, 8, 12));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(70, 74, 92));
            const int n = qMax(3, height() / 12);
            const qreal step = qreal(height()) / n;
            for (int i = 0; i < n; ++i) {
                const qreal y = i * step + (step - 5) / 2;
                p.drawRoundedRect(QRectF(4, y, 6, 5), 1.5, 1.5);
                p.drawRoundedRect(QRectF(width() - 10, y, 6, 5), 1.5, 1.5);
            }
            box.adjust(FilmEdge, 0, -FilmEdge, 0);
        }
        auto fit = [box](QSize s) {
            const QSizeF sz = QSizeF(s).scaled(box.size(), Qt::KeepAspectRatio);
            return QRectF(QPointF(box.left() + (box.width() - sz.width()) / 2,
                                  box.top() + (box.height() - sz.height()) / 2), sz);
        };
        if (!m_frame.isNull())        p.drawImage(fit(m_frame.size()), m_frame);
        else if (!m_poster.isNull())  p.drawPixmap(fit(m_poster.size()), m_poster, QRectF(m_poster.rect()));
    }

private:
    QVideoSink m_sink;
    QImage     m_frame;
    QPixmap    m_poster;
    qreal      m_radius = 0;
    bool       m_film = false;

public:
    static constexpr int FilmEdge = 14;  // Ширина края плёнки
    void setFilm(bool on) { m_film = on; update(); }
};

// ─── Круглая кнопка со значком ──────────────────────────────────────────────
class RoundButton : public QAbstractButton
{
public:
    RoundButton(QChar glyph, int size, QWidget *parent) : QAbstractButton(parent), m_glyph(glyph)
    {
        setFixedSize(size, size);
        setCursor(Qt::PointingHandCursor);
    }
    void setGlyph(QChar g) { if (g != m_glyph) { m_glyph = g; update(); } }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool hot = underMouse();
        p.setPen(QPen(QColor(255, 255, 255, hot ? 120 : 60), 1.0));
        p.setBrush(QColor(hot ? 60 : 30, hot ? 60 : 30, hot ? 90 : 50, 225));
        p.drawEllipse(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
        QFont f(SmartButton::iconFontFamily());
        f.setPixelSize(qMax(9, width() * 5 / 12));
        p.setFont(f);
        p.setPen(QColor(240, 244, 255));
        p.drawText(rect(), Qt::AlignCenter, QString(m_glyph));
    }
    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e) override { update(); QAbstractButton::leaveEvent(e); }

private:
    QChar m_glyph;
};

// Значки системного шрифта Windows (Segoe Fluent / MDL2)
constexpr ushort GlyphPlay = 0xE768, GlyphPause = 0xE769, GlyphMute = 0xE74F,
                 GlyphVolume = 0xE767, GlyphExpand = 0xE740;

// ─── Полоска прогресса (клик/перетаскивание — перемотка) ────────────────────
class ProgressLine : public QWidget
{
public:
    explicit ProgressLine(QWidget *parent) : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(14);
    }
    void setState(qint64 pos, qint64 dur) { m_pos = pos; m_dur = dur; update(); }
    std::function<void(qint64)> onSeek;
    bool showTime = true;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        const QString t = m_dur > 0 ? timeText(m_pos) + " / " + timeText(m_dur) : QString("—:—");
        const int tw = showTime ? QFontMetrics(f).horizontalAdvance(t) + 8 : 0;
        const QRectF track(0, height() / 2.0 - 1.5, width() - tw, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 45));
        p.drawRoundedRect(track, 1.5, 1.5);
        if (m_dur > 0) {
            QRectF done = track;
            done.setWidth(track.width() * qBound(0.0, double(m_pos) / m_dur, 1.0));
            p.setBrush(QColor(120, 190, 255, 235));
            p.drawRoundedRect(done, 1.5, 1.5);
        }
        if (showTime) {
            p.setPen(QColor(255, 255, 255, 170));
            p.drawText(QRectF(width() - tw, 0, tw, height()), Qt::AlignRight | Qt::AlignVCenter, t);
        }
    }
    void mousePressEvent(QMouseEvent *e) override { seekAt(e->position().x()); }
    void mouseMoveEvent(QMouseEvent *e) override { if (e->buttons() & Qt::LeftButton) seekAt(e->position().x()); }

private:
    void seekAt(qreal x)
    {
        if (m_dur <= 0 || !onSeek) return;
        QFont f = font(); f.setPixelSize(10);
        const QString t = timeText(m_pos) + " / " + timeText(m_dur);
        const int tw = showTime ? QFontMetrics(f).horizontalAdvance(t) + 8 : 0;
        const qreal w = qMax(1, width() - tw);
        onSeek(qint64(qBound(0.0, x / w, 1.0) * m_dur));
    }
    qint64 m_pos = 0, m_dur = 0;
};

// ─── Аудио в карточке ────────────────────────────────────────────────────────
// Кнопка ▶ слева, полоска с временем внизу. Текст карточки сдвигается вправо.
inline void attachAudio(QLabel *card, const QString &file)
{
    card->setContentsMargins(34, 0, 0, 14);   // Место под кнопку и полоску
    auto *btn = new RoundButton(QChar(GlyphPlay), 26, card);
    auto *bar = new ProgressLine(card);
    auto &pb = Playback::get();

    auto layoutControls = [card, btn, bar]() {
        if (card->text().isEmpty()) {  // Без названия — кнопка и полоска в одну строку
            btn->move(9, (card->height() - btn->height()) / 2);
            bar->setGeometry(42, (card->height() - 14) / 2, card->width() - 54, 14);
            return;
        }
        btn->move(9, qMax(4, (card->height() - 14 - btn->height()) / 2));
        bar->setGeometry(42, card->height() - 18, card->width() - 54, 14);
    };
    struct ResizeWatch : QObject {
        std::function<void()> f;
        bool eventFilter(QObject *, QEvent *e) override { if (e->type() == QEvent::Resize) f(); return false; }
    };
    auto *watch = new ResizeWatch;
    watch->setParent(card);
    watch->f = layoutControls;
    card->installEventFilter(watch);
    layoutControls();

    auto refresh = [card, btn, bar]() {
        auto &pb = Playback::get();
        const bool mine = pb.isOwner(card);
        const bool playing = mine && pb.player()->playbackState() == QMediaPlayer::PlayingState;
        btn->setGlyph(QChar(playing ? GlyphPause : GlyphPlay));
        bar->setState(mine ? pb.player()->position() : 0, mine ? pb.player()->duration() : 0);
    };
    QObject::connect(btn, &QAbstractButton::clicked, card, [card, file]() {
        auto &pb = Playback::get();
        if (pb.isOwner(card)) pb.toggle();
        else pb.start(file, card, nullptr, false);
    });
    bar->onSeek = [card](qint64 ms) {
        auto &pb = Playback::get();
        if (pb.isOwner(card)) pb.player()->setPosition(ms);
    };
    QObject::connect(pb.player(), &QMediaPlayer::positionChanged, card, refresh);
    QObject::connect(pb.player(), &QMediaPlayer::durationChanged, card, refresh);
    QObject::connect(pb.player(), &QMediaPlayer::playbackStateChanged, card, refresh);
    QObject::connect(pb.player(), &QMediaPlayer::sourceChanged, card, refresh);
    pb.listen(card, refresh);
    refresh();
}

// ─── Видео в карточке ────────────────────────────────────────────────────────
// area — где в карточке показывается кадр (под строкой с названием).
// onExpand — открыть большой просмотр (MainWindow → VideoViewer).
inline VideoSurface *attachVideo(QLabel *card, const QString &file, const QRect &area,
                                 std::function<void(VideoSurface *)> onExpand)
{
    auto *surface = new VideoSurface(card);
    surface->setGeometry(area);
    surface->setRadius(8);
    surface->setFilm(true);  // Края плёнки — отличать видео от картинок
    surface->lower();  // Под кнопками
    const int e = VideoSurface::FilmEdge;  // Кнопки — внутри кадра, не на плёнке

    auto *play   = new RoundButton(QChar(GlyphPlay), 34, card);
    auto *mute   = new RoundButton(QChar(GlyphMute), 24, card);
    auto *expand = new RoundButton(QChar(GlyphExpand), 24, card);
    auto *bar    = new ProgressLine(card);
    bar->showTime = false;
    auto *time   = new QLabel(card);
    time->setObjectName("mediaTime");  // Подпись длительности (setVideoDuration)
    time->setAttribute(Qt::WA_TransparentForMouseEvents);
    time->setStyleSheet("color:#f0f4ff; background:rgba(10,10,18,170); border-radius:5px; padding:1px 5px; font-size:10px;");
    time->hide();
    mute->move(area.right() - e - 26, area.bottom() - 28);
    expand->move(area.right() - e - 26, area.top() + 4);
    bar->setGeometry(area.left() + e + 32, area.bottom() - 21, area.width() - 2 * e - 64, 14);

    auto refresh = [card, surface, play, mute, bar, time, e]() {
        auto &pb = Playback::get();
        const bool mine = pb.isOwner(card);
        const bool playing = mine && pb.player()->playbackState() == QMediaPlayer::PlayingState;
        play->setGlyph(QChar(playing ? GlyphPause : GlyphPlay));
        const QRect a = surface->geometry();
        if (mine) {
            // Видео в карточке — маленькая ▶/⏸ слева внизу, чтобы не закрывать кадр
            play->setFixedSize(24, 24);
            play->move(a.left() + e + 2, a.bottom() - 28);
        } else {
            play->setFixedSize(34, 34);
            play->move(a.center().x() - 17, a.center().y() - 17);
        }
        mute->setVisible(mine);
        mute->setGlyph(QChar(pb.output()->isMuted() ? GlyphMute : GlyphVolume));
        bar->setVisible(mine);
        bar->setState(mine ? pb.player()->position() : 0, mine ? pb.player()->duration() : 0);
        if (mine) time->setText(timeText(pb.player()->position()) + " / " + timeText(pb.player()->duration()));
        time->setVisible(mine || !time->property("duration").toString().isEmpty());
        if (!mine) {
            surface->clearFrame();
            time->setText(time->property("duration").toString());
        }
        time->adjustSize();
        time->move(surface->geometry().left() + e + 4, surface->geometry().top() + 6);
    };
    QObject::connect(play, &QAbstractButton::clicked, card, [card, file, surface]() {
        auto &pb = Playback::get();
        if (pb.isOwner(card)) pb.toggle();
        else pb.start(file, card, surface->sink(), /*muted=*/true);  // В карточке — без звука
    });
    QObject::connect(mute, &QAbstractButton::clicked, card, [card]() {
        auto &pb = Playback::get();
        if (pb.isOwner(card)) pb.output()->setMuted(!pb.output()->isMuted());
        pb.notify();
    });
    QObject::connect(expand, &QAbstractButton::clicked, card, [surface, onExpand]() { if (onExpand) onExpand(surface); });
    bar->onSeek = [card](qint64 ms) { auto &pb = Playback::get(); if (pb.isOwner(card)) pb.player()->setPosition(ms); };
    auto &pb = Playback::get();
    QObject::connect(pb.player(), &QMediaPlayer::positionChanged, card, refresh);
    QObject::connect(pb.player(), &QMediaPlayer::durationChanged, card, refresh);
    QObject::connect(pb.player(), &QMediaPlayer::playbackStateChanged, card, refresh);
    QObject::connect(pb.output(), &QAudioOutput::mutedChanged, card, refresh);
    pb.listen(card, refresh);
    refresh();
    return surface;
}

// Длительность для подписи на карточке, пока видео не играет
inline void setVideoDuration(QLabel *card, qint64 ms)
{
    for (QLabel *l : card->findChildren<QLabel *>(Qt::FindDirectChildrenOnly)) {
        if (l->objectName() == QLatin1String("mediaTime")) {
            l->setProperty("duration", ms > 0 ? timeText(ms) : QString());
            if (!Playback::get().isOwner(card)) {
                l->setText(l->property("duration").toString());
                l->setVisible(ms > 0);
                l->adjustSize();
            }
        }
    }
}

} // namespace Media
