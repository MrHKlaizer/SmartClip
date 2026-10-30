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
// ║  Настройки → Оформление: прокрутка «Дуга» / «Плавная» (без ухода по дуге), ║
// ║  выезд карточки под мышью — отдельной галочкой.                             ║
// ║  Анимации выключены в Windows — обычная прокрутка без эффектов.            ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QVBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QEvent>
#include <QTimer>
#include <QApplication>
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
    std::function<void()> beforeLayout;
    std::function<void()> afterLayout;

    void setGeometry(const QRect &r) override
    {
        if (beforeLayout) beforeLayout();
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
        // Плавная перестройка: лейаут сразу ставит карточки на новые места —
        // запоминаем, где они стояли, и возвращаем туда; дальше tick() плавно
        // довозит их (раньше соседние карточки при перетаскивании перескакивали)
        layout->beforeLayout = [this]() {
            m_prevPos.clear();
            if (m_reflow)
                if (QWidget *content = m_area->widget())
                    for (QObject *o : content->children())
                        if (auto *w = qobject_cast<QWidget *>(o)) m_prevPos.insert(w, w->pos());
            // Перед раскладкой — все карточки на «родные» места (без дуги и
            // выезда). Qt иногда раскладывает вхолостую (размер тот же) и
            // карточки не трогает — тогда rememberBase запоминал сдвинутое дугой
            // место как «родное», и карточки навсегда оставались съехавшими
            // посреди колонки (тот самый редкий баг). Сразу после раскладки
            // apply() возвращает эффекты — до отрисовки, так что не мигает.
            for (int i = 0; i < m_layout->count(); ++i) {
                QWidget *w = m_layout->itemAt(i)->widget();
                if (!w) continue;
                const QVariant base = w->property("arcBase");
                if (base.isValid() && w->pos() != base.toPoint()) w->move(base.toPoint());
            }
        };
        layout->afterLayout = [this]() {
            rememberBase();
            for (auto it = m_prevPos.cbegin(); it != m_prevPos.cend(); ++it)
                if (it.key()->pos() != it.value()) it.key()->move(it.value());
            m_prevPos.clear();
            apply();
        };
        // Содержимое колонки двигается при прокрутке (и при «перетяжке» за край)
        area->widget()->installEventFilter(this);
        area->widget()->setMouseTracking(true);  // Курсор между карточками — тоже знаем
        area->viewport()->installEventFilter(this);
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.setInterval(8);
        connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
        // Страховка наведения: Windows иногда не присылает «мышь ушла/пришла»
        // (быстрое движение, прозрачная середина окна) — карточка «залипала»
        // выдвинутой, а другие больше не выдвигались. Пока мышь над колонкой,
        // сами сверяем, над какой карточкой курсор.
        m_hoverCheck.setInterval(50);
        connect(&m_hoverCheck, &QTimer::timeout, this, [this]() {
            syncHover();
            // Страховка изгиба: иногда карточки оставались сдвинутыми вбок
            // посреди колонки (изгиб посчитан по устаревшим размерам) —
            // раз в четверть секунды ставим их заново (на месте — не двигаются)
            if (++m_checks % 5 == 0 && !m_reflow && QApplication::mouseButtons() == Qt::NoButton) apply();
        });
    }

    // Пересчитать (например, после смены оформления в настройках)
    void refresh() { if (!hoverOn()) setHovered(nullptr); apply(); }

    // Пока окно SmartClip открыто — следим за курсором сами, всё время, а не
    // только после сообщения «мышь зашла на карточку». Когда Windows нагружена
    // или мышь держит другая программа, эти сообщения теряются, и выезд
    // карточек «залипал» на одной.
    void watchHover(bool on)
    {
        m_watching = on;
        // Страховка изгиба (см. m_hoverCheck) нужна и без выезда под мышью
        if (on) { m_hoverCheck.start(); syncHover(); }
        else { m_hoverCheck.stop(); setHovered(nullptr); }
    }

    // Плавная перестройка: карточки не прыгают на новые места, а доезжают до
    // них (перетаскивание закрепа — соседние аккуратно расступаются)
    void setReflowAnimated(bool on)
    {
        m_reflow = on;
        if (on) startTimer();
        else    { m_targets.clear(); apply(); }  // Выключили — сразу на места
    }

    // Уход крайних карточек по дуге — прокрутка «Дуга»
    static bool arcOn()
    {
        return Anim::enabled(Anim::Kind::Scroll) && Anim::scrollStyle() == QLatin1String("arc");
    }
    // Выезд карточки под мышью — своя галочка (Настройки → Оформление)
    static bool hoverOn()
    {
        return Anim::enabled(Anim::Kind::Scroll) && AppSettings::get().cardHover();
    }
    static bool enabled() { return arcOn() || hoverOn(); }

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        QWidget *content = m_area->widget();
        const QEvent::Type t = e->type();
        if ((obj == content && t == QEvent::Move) || (obj == m_area->viewport() && t == QEvent::Resize)) {
            apply();
        } else if (obj == m_area->viewport() && t == QEvent::Enter) {
            if (hoverOn()) m_hoverCheck.start();
        } else if (obj == m_area->viewport() && (t == QEvent::Leave || t == QEvent::Hide)) {
            setHovered(nullptr);
        } else if (obj != content && obj != m_area->viewport()) {
            // Карточка: мышь зашла — выдвигаем; вышла — проверяем, не осталась ли
            // она над увеличенной частью карточки (иначе карточка дрожала бы)
            auto *w = static_cast<QWidget *>(obj);
            if (t == QEvent::Enter && hoverOn()) { setHovered(w); m_hoverCheck.start(); }
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
                connect(w, &QObject::destroyed, this, [this, w]() { m_hover.remove(w); m_targets.remove(w); });
            }
        }
    }

    void setHovered(QWidget *w)
    {
        if (m_hovered == w) return;
        m_hovered = w;
        startTimer();
    }

    // Где видна выдвинутая карточка с учётом её увеличения (координаты колонки)
    QRectF hoveredRect() const
    {
        const qreal s = 1.0 + GROW * m_hover.value(m_hovered.data(), 0.0);
        const QRectF g(m_hovered->geometry());
        return QRectF(g.center().x() - g.width() * s / 2, g.top(), g.width() * s, g.height() * s);
    }

    // Курсор всё ещё над карточкой с учётом её увеличения?
    void checkHover()
    {
        if (!m_hovered) return;
        const QPointF c = m_area->widget()->mapFromGlobal(QCursor::pos());
        if (!hoveredRect().contains(c)) setHovered(nullptr);
    }

    // Сверить выдвинутую карточку с тем, что сейчас под курсором
    void syncHover()
    {
        QWidget *content = m_area->widget();
        if (!content || !m_area->isVisible() || !hoverOn()) {
            if (!m_watching || !content || !m_area->isVisible()) m_hoverCheck.stop();
            if (m_hovered) setHovered(nullptr);
            return;
        }
        if (QApplication::mouseButtons() != Qt::NoButton) return;  // Тащат карточку — не мешаем
        const QPoint gp = QCursor::pos();
        const QPointF c = content->mapFromGlobal(gp);
        if (m_hovered && hoveredRect().contains(c)) return;           // Всё верно
        const bool inColumn = m_area->viewport()->rect().contains(m_area->viewport()->mapFromGlobal(gp));
        QWidget *under = nullptr;
        if (inColumn) {
            for (int i = 0; i < m_layout->count(); ++i) {
                QWidget *w = m_layout->itemAt(i)->widget();
                if (w && !w->isHidden() && fxOf(w) && w->geometry().contains(c.toPoint())) { under = w; break; }
            }
        }
        setHovered(under);
        // Мышь ушла из колонки — проверять нечего (если не следим всё время)
        if (!inColumn && !m_hovered && !m_watching) m_hoverCheck.stop();
    }

    void startTimer()
    {
        if (!m_timer.isActive()) {
            m_clock.start();
            m_last = 0;
            m_timer.setInterval(Anim::frameMs(m_area));  // По частоте монитора
            m_timer.start();
        }
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

        // Перестройка: каждая карточка доезжает до своего нового места
        if (m_reflow) {
            const qreal kr = 1.0 - qExp(-dt / (Anim::ms(Anim::Kind::Scroll, 60) / 1000.0));
            for (auto it = m_targets.cbegin(); it != m_targets.cend(); ++it) {
                QWidget *w = it.key();
                const QPointF cur = w->pos(), target = it.value();
                if (cur == target) continue;
                QPointF next = cur + (target - cur) * kr;
                if (qAbs(next.x() - target.x()) < 1 && qAbs(next.y() - target.y()) < 1) next = target;
                w->move(next.toPoint());
                moving = true;
            }
            if (!Anim::enabled(Anim::Kind::Scroll)) {  // Анимации выключены в Windows — сразу
                for (auto it = m_targets.cbegin(); it != m_targets.cend(); ++it) it.key()->move(it.value());
            }
            moving = true;  // Пока идёт перетаскивание — следим постоянно
        }
        if (!moving) m_timer.stop();
    }

    void apply()
    {
        QWidget *content = m_area->widget();
        if (!content) return;
        const bool on = enabled();
        const bool arc = arcOn();
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

        // Карточки далеко за краем видимой части не трогаем: их никто не видит,
        // а двигать и перерисовывать их — работа впустую. Встанут на место,
        // когда до них долистают (прокрутка тоже вызывает apply).
        // При перестройке (перетаскивание) — двигаем все, чтобы ничего не потерялось.
        const qreal bandTop = scrolled - 120, bandBottom = scrolled + vh + 120;

        qreal push = 0;  // Насколько раздвинуты карточки выше (увеличенными под мышью)
        for (int i = 0; i < m_layout->count(); ++i) {
            QWidget *w = m_layout->itemAt(i)->widget();
            if (!w || w->isHidden()) continue;
            const QVariant baseV = w->property("arcBase");
            if (!baseV.isValid()) continue;
            const QPoint base = baseV.toPoint();
            if (!m_reflow && (base.y() + push > bandBottom || base.y() + push + w->height() < bandTop)) {
                // ...но и не оставляем её сдвинутой дугой: за краем она сразу
                // встаёт ровно на своё место (её не видно — это ничего не стоит).
                // Иначе карточка, ушедшая за край в «дуговом» положении, могла
                // вернуться в середину колонки сдвинутой вбок.
                const QPoint flat(base.x(), qRound(base.y() + push));
                if (w->pos() != flat) w->move(flat);
                if (CardShadowEffect *fx = fxOf(w)) fx->setLook(1.0, 1.0);
                push += GROW * m_hover.value(w, 0.0) * w->height();
                continue;
            }
            CardShadowEffect *fx = fxOf(w);

            // «Приподнятая» карточка (её тащат или только что отпустили):
            // полупрозрачная и чуть меньше (lift: 0 — обычная, 1 — поднята)
            const qreal lift = w->property("lift").toReal();
            if (!on) {
                place(w, base);
                if (fx) fx->setLook(1.0 - 0.05 * lift, 1.0 - 0.35 * lift);
                continue;
            }

            const qreal h = m_hover.value(w, 0.0);  // 0..1 — насколько выдвинута мышью
            const qreal y = base.y() + push;

            // Уход по дуге у верхнего/нижнего края (0 — карточка стоит ровно)
            qreal e = 0;
            const qreal top = content->y() + y, hh = w->height();
            if (!arc) {
                // «Плавная» прокрутка — крайние карточки стоят ровно
            } else if (m_byCrossing) {
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
            place(w, pos);
            if (fx) fx->setLook((1.0 + GROW * h) * (1.0 - 0.12 * qMin(e, 1.0)) * (1.0 - 0.05 * lift),
                                qMax(0.25, 1.0 - 0.6 * qMin(e, 1.0)) * (1.0 - 0.35 * lift));

            // Увеличенная карточка раздвигает те, что ниже
            push += GROW * h * w->height();
            Q_UNUSED(zone)
        }
    }

    // Поставить карточку на место: сразу или (при перестройке) через tick()
    void place(QWidget *w, const QPoint &pos)
    {
        if (m_reflow) {
            // Виджет удалят (например, пустое место после перетаскивания) —
            // забываем его. Раньше tick() ещё полсекунды двигал удалённое
            // место, и SmartClip падал.
            if (!w->property("arcForget").toBool()) {
                w->setProperty("arcForget", true);
                connect(w, &QObject::destroyed, this, [this, w]() { m_targets.remove(w); m_hover.remove(w); });
            }
            m_targets.insert(w, pos);
        }
        else if (w->pos() != pos) w->move(pos);
    }

    bool              m_reflow = false;  // Плавная перестройка включена
    QHash<QWidget *, QPoint> m_targets;   // Куда доезжает каждая карточка
    QHash<QWidget *, QPoint> m_prevPos;   // Где стояли до перестройки лейаута

    QScrollArea      *m_area;
    ArcColumnLayout  *m_layout;
    int               m_toCenter;
    int               m_edge;
    int               m_slide;
    bool              m_byCrossing;
    qreal             m_cross = 30;   // Линия у края для byCrossing: за ней карточка «уходит», px
    QPointer<QWidget> m_hovered;
    QTimer            m_hoverCheck;  // Страховка наведения (см. syncHover)
    int               m_checks = 0;
    bool              m_watching = false;  // Окно открыто — опрашиваем всегда (watchHover)
    QHash<QWidget *, qreal> m_hover;  // Карточки, которые выдвинуты или возвращаются
    QTimer            m_timer;
    QElapsedTimer     m_clock;
    qint64            m_last = 0;
};
