// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  CardArc.h — Прокрутка «Дуга» (SmartClip 2.0, этап 4)                       ║
// ║                                                                              ║
// ║  Колонка ровная, как раньше. Изгибаются только крайние карточки — те, что   ║
// ║  уезжают за верхний/нижний край или выезжают из-за него: они уходят по      ║
// ║  дуге НАРУЖУ (в левой колонке — влево, в правой — вправо), немного          ║
// ║  уменьшаясь и тая.                                                           ║
// ║                                                                              ║
// ║  Наведение мышью: карточка плавно выдвигается вбок (к центру экрана) и     ║
// ║  чуть увеличивается, а карточки под ней раздвигаются — она никого не        ║
// ║  накрывает, промежутки между карточками остаются те же.                     ║
// ║                                                                              ║
// ║  Положение карточек меняется по-настоящему (move) — клик попадает туда,     ║
// ║  где карточку видно. Масштаб и прозрачность рисует тень-эффект карточки    ║
// ║  (CardShadowEffect).                                                         ║
// ║                                                                              ║
// ║  Включается в окне 🖌: Прокрутка → «Дуга». «Плавная» — без этих эффектов,  ║
// ║  «Классика» — обычная прокрутка Qt.                                         ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QVBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QEvent>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QHash>
#include <QCursor>
#include <QtMath>
#include <functional>
#include "Anim.h"
#include "CardShadow.h"

// Лейаут колонки: после каждой раскладки сообщает, что карточки встали на
// «прямые» места, — эффекты сразу пересчитываются от них
class ArcColumnLayout : public QVBoxLayout
{
public:
    using QVBoxLayout::QVBoxLayout;
    std::function<void()> afterLayout;

    void setGeometry(const QRect &r) override
    {
        QVBoxLayout::setGeometry(r);
        if (afterLayout) afterLayout();
    }
};

class CardArc : public QObject
{
public:
    // toCenter = +1 — центр экрана справа от колонки (левая колонка), -1 — слева.
    // edge     — насколько уходит наружу карточка у самого края, px.
    // slide    — насколько выдвигается к центру карточка под мышью, px.
    // byCrossing — для колонки с карточками РАЗНОЙ высоты (тексты): уход зависит
    //   от того, какая часть карточки уже за краем. Иначе (фото одного размера) —
    //   от того, насколько близко к краю центр карточки: получается ровная дуга.
    CardArc(QScrollArea *area, ArcColumnLayout *layout, int toCenter, int edge, int slide,
            bool byCrossing)
        : QObject(area), m_area(area), m_layout(layout), m_toCenter(toCenter),
          m_edge(edge), m_slide(slide), m_byCrossing(byCrossing)
    {
        layout->afterLayout = [this]() { rememberBase(); apply(); };
        // Содержимое колонки двигается при прокрутке (и при «перетяжке» за край)
        area->widget()->installEventFilter(this);
        area->widget()->setMouseTracking(true);  // Курсор между карточками — тоже знаем
        area->viewport()->installEventFilter(this);
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.setInterval(8);
        connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
    }

    // Пересчитать (например, после смены стиля прокрутки в окне 🖌)
    void refresh() { if (!enabled()) setHovered(nullptr); apply(); }

    static bool enabled()
    {
        return Anim::enabled(Anim::Kind::Scroll) && Anim::scrollStyle() == QLatin1String("arc");
    }

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        QWidget *content = m_area->widget();
        const QEvent::Type t = e->type();
        if ((obj == content && t == QEvent::Move) || (obj == m_area->viewport() && t == QEvent::Resize)) {
            apply();
        } else if (obj == m_area->viewport() && (t == QEvent::Leave || t == QEvent::Hide)) {
            setHovered(nullptr);
        } else if (obj != content && obj != m_area->viewport()) {
            // Карточка: мышь зашла — выдвигаем; вышла — проверяем, не осталась ли
            // она над увеличенной частью карточки (иначе карточка дрожала бы)
            auto *w = static_cast<QWidget *>(obj);
            if (t == QEvent::Enter && enabled()) setHovered(w);
            else if (t == QEvent::Leave) checkHover();
        } else if (obj == content && t == QEvent::MouseMove) {
            checkHover();
        }
        return false;
    }

private:
    static constexpr qreal GROW = 0.05;   // Увеличение карточки под мышью (+5%)

    static CardShadowEffect *fxOf(QWidget *w)
    {
        return dynamic_cast<CardShadowEffect *>(w->graphicsEffect());
    }

    // Где карточки стоят без эффектов — их ставит лейаут
    void rememberBase()
    {
        for (int i = 0; i < m_layout->count(); ++i) {
            QWidget *w = m_layout->itemAt(i)->widget();
            if (!w) continue;
            w->setProperty("arcBase", w->pos());
            if (!w->property("arcHooked").toBool() && fxOf(w)) {
                w->setProperty("arcHooked", true);
                w->installEventFilter(this);
                connect(w, &QObject::destroyed, this, [this, w]() { m_hover.remove(w); });
            }
        }
    }

    void setHovered(QWidget *w)
    {
        if (m_hovered == w) return;
        m_hovered = w;
        startTimer();
    }

    // Курсор всё ещё над карточкой с учётом её увеличения?
    void checkHover()
    {
        if (!m_hovered) return;
        const qreal s = 1.0 + GROW * m_hover.value(m_hovered.data(), 0.0);
        const QRectF g(m_hovered->geometry());
        const QRectF vis(g.center().x() - g.width() * s / 2, g.top(), g.width() * s, g.height() * s);
        const QPointF c = m_area->widget()->mapFromGlobal(QCursor::pos());
        if (!vis.contains(c)) setHovered(nullptr);
    }

    void startTimer()
    {
        if (!m_timer.isActive()) { m_clock.start(); m_last = 0; m_timer.start(); }
    }

    // Плавное выдвижение/возврат карточек под мышью
    void tick()
    {
        const qint64 now = m_clock.elapsed();
        const qreal dt = qBound<qreal>(0.001, (now - m_last) / 1000.0, 0.05);
        m_last = now;
        const qreal tau = Anim::ms(Anim::Kind::Scroll, 70) / 1000.0;
        const qreal k = 1.0 - qExp(-dt / tau);

        bool moving = false;
        if (m_hovered && !m_hover.contains(m_hovered.data())) m_hover.insert(m_hovered.data(), 0.0);
        for (auto it = m_hover.begin(); it != m_hover.end(); ) {
            const qreal target = (it.key() == m_hovered.data()) ? 1.0 : 0.0;
            qreal v = it.value() + (target - it.value()) * k;
            if (qAbs(v - target) < 0.004) v = target;
            if (v != target) moving = true;
            if (v == 0.0 && target == 0.0) { it = m_hover.erase(it); continue; }
            it.value() = v;
            ++it;
        }
        apply();
        if (!moving) m_timer.stop();
    }

    void apply()
    {
        QWidget *content = m_area->widget();
        if (!content) return;
        const bool on = enabled();
        const qreal vh = m_area->viewport()->height();
        if (vh <= 0) return;
        const qreal zone = qMin(vh * 0.22, 260.0);  // Где у края начинается уход по дуге

        // В самом верху списка уходить некуда — выше карточек больше нет; так же
        // в самом низу. Поэтому изгиб у края включается плавно, по мере того
        // как список отъезжает от начала (или от конца)
        // (высоту берём у содержимого: полоса прокрутки узнаёт новую длину позже)
        const qreal scrolled  = -content->y();                              // От начала, px
        const qreal remaining = content->height() - vh - scrolled;          // До конца, px
        const qreal ramp = m_byCrossing ? 90.0 : zone;
        const qreal kTop    = qBound(0.0, scrolled  / ramp, 1.0);
        const qreal kBottom = qBound(0.0, remaining / ramp, 1.0);

        qreal push = 0;  // Насколько раздвинуты карточки выше (увеличенными под мышью)
        for (int i = 0; i < m_layout->count(); ++i) {
            QWidget *w = m_layout->itemAt(i)->widget();
            if (!w || w->isHidden()) continue;
            const QVariant baseV = w->property("arcBase");
            if (!baseV.isValid()) continue;
            const QPoint base = baseV.toPoint();
            CardShadowEffect *fx = fxOf(w);

            if (!on) {
                if (w->pos() != base) w->move(base);
                if (fx) fx->setLook(1.0, 1.0);
                continue;
            }

            const qreal h = m_hover.value(w, 0.0);  // 0..1 — насколько выдвинута мышью
            const qreal y = base.y() + push;

            // Уход по дуге у верхнего/нижнего края (0 — карточка стоит ровно)
            qreal e = 0;
            const qreal top = content->y() + y, hh = w->height();
            if (m_byCrossing) {
                // Какая доля карточки уже ушла за линию у края. Пока карточка
                // видна целиком — стоит ровно; высокая уходит так же плавно,
                // как низкая, а не прыгает вбок целым блоком
                const qreal line = m_cross;
                qreal p = 0;
                if (top < line)                p = kTop * (line - top) / hh;
                else if (top + hh > vh - line) p = kBottom * (top + hh - (vh - line)) / hh;
                p = qBound(0.0, p, 1.0);
                e = p * p * 1.25;
            } else {
                // u = 0 внутри колонки, 1 — центр карточки на самом краю
                const qreal cy = top + hh / 2.0;
                qreal u = 0;
                if (cy < zone)            u = kTop * (zone - cy) / zone;
                else if (cy > vh - zone)  u = kBottom * (cy - (vh - zone)) / zone;
                u = qBound(0.0, u, 1.4);
                e = u * u;  // Плавно: у края — быстрее
            }

            const qreal dx = -m_toCenter * m_edge * e + m_toCenter * m_slide * h;
            const QPoint pos(qRound(base.x() + dx), qRound(y));
            if (w->pos() != pos) w->move(pos);
            if (fx) fx->setLook((1.0 + GROW * h) * (1.0 - 0.12 * qMin(e, 1.0)),
                                qMax(0.25, 1.0 - 0.6 * qMin(e, 1.0)));

            // Увеличенная карточка раздвигает те, что ниже
            push += GROW * h * w->height();
            Q_UNUSED(zone)
        }
    }

    QScrollArea      *m_area;
    ArcColumnLayout  *m_layout;
    int               m_toCenter;
    int               m_edge;
    int               m_slide;
    bool              m_byCrossing;
    qreal             m_cross = 30;   // Линия у края для byCrossing: за ней карточка «уходит», px
    QPointer<QWidget> m_hovered;
    QHash<QWidget *, qreal> m_hover;  // Карточки, которые выдвинуты или возвращаются
    QTimer            m_timer;
    QElapsedTimer     m_clock;
    qint64            m_last = 0;
};
