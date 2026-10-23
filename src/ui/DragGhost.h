#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  DragGhost.h — «живой» файл (или стопка файлов) в руке при перетаскивании     ║
// ║                                                                              ║
// ║  Не застывшая картинка, а карточка, которая висит под курсором: догоняет     ║
// ║  его с лёгкой инерцией, при быстром движении наклоняется в сторону движения ║
// ║  (как подвешенная за верх), а в покое чуть покачивается. Несколько файлов —   ║
// ║  стопка со счётчиком.                                                        ║
// ║                                                                              ║
// ║  Отдельное прозрачное окно поверх всего, сквозь которое проходит мышь: файл  ║
// ║  бросают в программу ПОД ним (Windows его «не видит»).                       ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>
#include <QCursor>
#include <QtMath>
#include <functional>

#include "SmartButton.h"

class DragGhost : public QWidget
{
public:
    struct Item { QPixmap thumb; QString name; };

    DragGhost(QWidget *ref, int scalePct)
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                           Qt::WindowStaysOnTopHint)
        , m_scale(scalePct)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlag(Qt::WindowTransparentForInput);  // Файл бросают сквозь окно
        if (ref && ref->window()->testAttribute(Qt::WA_DontShowOnScreen))
            setAttribute(Qt::WA_DontShowOnScreen);       // Проверки — не на экране
        resize(s(200), s(200));
        m_tick.setTimerType(Qt::PreciseTimer);
        m_tick.setInterval(6);
        QObject::connect(&m_tick, &QTimer::timeout, this, [this]() { step(); });
    }

    void setItems(const QList<Item> &items) { m_items = items; update(); }

    // Взяли файл: карточка «приподнимается» в точке курсора
    void start()
    {
        m_pos = QCursor::pos();
        m_prev = m_pos;
        m_clock.start();
        m_last = 0;
        place();
        show();
        m_tick.setInterval(Anim::frameMs(this));  // По частоте монитора
        m_tick.start();
    }

    // Отпустили: dropped — бросили в программу (карточка «уходит» в неё),
    // иначе просто тает
    void finish(bool dropped)
    {
        if (m_leaving) return;
        m_leaving = true;
        m_dropped = dropped;
        m_leaveAt = m_clock.elapsed();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_items.isEmpty()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setOpacity(m_opacity);
        // Точка подвеса — верх карточки под курсором
        p.translate(width() / 2.0, s(46));
        p.rotate(m_angle);
        p.scale(m_scaleK, m_scaleK);

        const int cw = s(84), ch = s(96);
        // Стопка: задние карточки чуть повёрнуты и сдвинуты
        const int n = qMin(3, int(m_items.size()));
        for (int i = n - 1; i >= 0; --i) {
            p.save();
            if (i > 0) {
                p.translate(i * s(6), i * s(4));
                p.rotate(i % 2 ? 7 : -6);
            }
            drawCard(p, m_items[i], QRectF(-cw / 2.0, -s(8), cw, ch), i == 0);
            p.restore();
        }
        if (m_items.size() > 1) {  // Счётчик файлов
            const QRectF badge(cw / 2.0 - s(14), -s(16), s(24), s(24));
            p.setPen(QPen(QColor(18, 18, 30), 2));
            p.setBrush(QColor(110, 170, 255));
            p.drawEllipse(badge);
            QFont f = font();
            f.setPixelSize(s(12));
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(badge, Qt::AlignCenter, QString::number(m_items.size()));
        }
    }

private:
    int s(int v) const { return v * m_scale / 100; }

    void drawCard(QPainter &p, const Item &it, const QRectF &r, bool front)
    {
        QPainterPath path;
        path.addRoundedRect(r, s(10), s(10));
        // Мягкая «тень» под карточкой — она приподнята над экраном
        if (front) {
            QPainterPath sh;
            sh.addRoundedRect(r.translated(s(3), s(6)), s(10), s(10));
            p.fillPath(sh, QColor(0, 0, 0, 70));
        }
        p.fillPath(path, QColor(22, 22, 36, 245));
        p.setPen(QPen(QColor(140, 190, 255, front ? 170 : 90), 1.5));
        p.drawPath(path);
        if (!it.thumb.isNull()) {
            const QRectF area(r.left() + s(12), r.top() + s(8), r.width() - s(24), s(58));
            const QSizeF sz = QSizeF(it.thumb.size() / it.thumb.devicePixelRatio()).scaled(area.size(), Qt::KeepAspectRatio);
            p.drawPixmap(QRectF(area.center().x() - sz.width() / 2, area.center().y() - sz.height() / 2,
                                sz.width(), sz.height()), it.thumb, QRectF(it.thumb.rect()));
        }
        if (front) {
            QFont f = font();
            f.setPixelSize(s(10));
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 220));
            const QRectF nr(r.left() + s(6), r.top() + s(70), r.width() - s(12), s(18));
            p.drawText(nr, Qt::AlignCenter, QFontMetrics(f).elidedText(it.name, Qt::ElideMiddle, int(nr.width())));
        }
    }

    void place()
    {
        move(QPoint(qRound(m_pos.x()) - width() / 2, qRound(m_pos.y()) - s(30)));
    }

    void step()
    {
        const qint64 now = m_clock.elapsed();
        const qreal dt = qBound(0.001, (now - m_last) / 1000.0, 0.05);
        m_last = now;

        // Догоняет курсор с лёгкой инерцией
        const QPointF target = QPointF(QCursor::pos());
        m_pos += (target - m_pos) * (1.0 - qExp(-dt / 0.035));
        const QPointF vel = (m_pos - m_prev) / dt;  // px/с
        m_prev = m_pos;

        // Наклон в сторону движения (подвешена за верх — низ отстаёт), в покое —
        // лёгкое покачивание
        const qreal tilt = qBound(-24.0, vel.x() * 0.018, 24.0);
        const qreal calm = 1.0 - qMin(1.0, qAbs(vel.x()) / 500.0);
        const qreal sway = qSin(now / 1000.0 * 2.4) * 2.2 * calm;
        m_angle += (tilt + sway - m_angle) * (1.0 - qExp(-dt / 0.09));

        // Появление: чуть «подпрыгивает» до 1.06 и садится в 1.0
        const qreal t = now / 1000.0;
        m_scaleK = t < 0.18 ? 0.85 + 0.21 * (t / 0.18) : 1.06 - 0.06 * qMin(1.0, (t - 0.18) / 0.15);
        m_opacity = qMin(1.0, t / 0.08);

        if (m_leaving) {
            const qreal k = qMin(1.0, (now - m_leaveAt) / 200.0);
            m_opacity = 1.0 - k;
            m_scaleK *= m_dropped ? (1.0 - 0.5 * k) : (1.0 - 0.15 * k);
            if (k >= 1.0) { m_tick.stop(); close(); return; }
        }
        place();
        update();
    }

    int           m_scale = 100;
    QList<Item>   m_items;
    QTimer        m_tick;
    QElapsedTimer m_clock;
    qint64        m_last = 0, m_leaveAt = 0;
    QPointF       m_pos, m_prev;
    qreal         m_angle = 0, m_scaleK = 1, m_opacity = 0;
    bool          m_leaving = false, m_dropped = false;
};
