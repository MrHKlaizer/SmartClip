// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  DateRuler.h — Шкала дат сбоку колонки при прокрутке (SmartClip 2.0)        ║
// ║                                                                              ║
// ║  Пока колонку листают, у её края проявляется линейка: засечка на каждую     ║
// ║  карточку, у первой карточки каждого дня — дата («21.10», а за прошлые      ║
// ║  годы — «21.10.25»). Голубая линия посередине — дата карточки в центре      ║
// ║  колонки. Остановились — линейка плавно гаснет.                             ║
// ║                                                                              ║
// ║  Быстрая перемотка: зажать у линии шкалы и вести вверх-вниз — колонка       ║
// ║  перематывается по всей длине (верх шкалы — начало, низ — конец), у        ║
// ║  курсора крупно видна дата. Как в галерее телефона.                         ║
// ║                                                                              ║
// ║  Настройки → Оформление → «Шкала дат»: у края экрана / со стороны центра /  ║
// ║  выключена. Даты есть только у истории (у закрепов — нет, там шкалы нет).   ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QTimer>
#include <QVariantAnimation>
#include <QEvent>
#include <QTimeZone>
#include <QFontMetrics>
#include <climits>
#include <QMouseEvent>
#include <QScrollBar>
#include <functional>
#include "Anim.h"
#include "core/AppSettings.h"

class DateRuler;

// Невидимая полоса у линии шкалы: за неё «хватают» для быстрой перемотки.
// Отдельный виджет, а не вся шкала, — чтобы шкала не мешала кликам по краю карточек.
class DateRulerGrip : public QWidget
{
public:
    DateRulerGrip(QWidget *parent, DateRuler *ruler) : QWidget(parent), m_ruler(ruler)
    {
        setCursor(Qt::SizeVerCursor);
        hide();
    }
protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
private:
    DateRuler *m_ruler;
};

class DateRuler : public QWidget
{
public:
    // leftColumn — колонка у левого края экрана (тексты); у правой (фото) всё зеркально.
    // jump — мгновенно прокрутить колонку на позицию (для быстрой перемотки).
    DateRuler(QScrollArea *area, QLayout *layout, bool leftColumn, int width,
              std::function<void(int)> jump)
        : QWidget(area->viewport()), m_area(area), m_layout(layout), m_leftColumn(leftColumn),
          m_jump(std::move(jump))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);  // Клики — сквозь шкалу к карточкам
        setFixedWidth(width);
        hide();
        m_grip = new DateRulerGrip(area->viewport(), this);
        area->widget()->installEventFilter(this);   // Прокрутка двигает содержимое
        area->viewport()->installEventFilter(this); // Размер колонки
        m_hideTimer.setSingleShot(true);
        m_hideTimer.setInterval(900);
        connect(&m_hideTimer, &QTimer::timeout, this, [this]() { if (!m_scrubbing) fadeTo(0.0); });
        m_fade.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_opacity = v.toReal();
            update();
        });
        connect(&m_fade, &QVariantAnimation::finished, this, [this]() { if (m_opacity <= 0.0) hideAll(); });
        place();
    }

    // Настройку поменяли — переставить (или спрятать)
    void refresh() { place(); if (side() == Off) { m_fade.stop(); m_opacity = 0; hideAll(); } }

    // ── Быстрая перемотка (вызывает DateRulerGrip) ──────────────────────────
    void scrubStart(int y) { m_scrubbing = true; m_hideTimer.stop(); fadeTo(1.0); scrubTo(y); }
    void scrubTo(int y)
    {
        // Позиция по высоте шкалы → позиция во всём списке
        QScrollBar *sb = m_area->verticalScrollBar();
        const int pad = 14;
        const qreal frac = qBound(0.0, qreal(y - pad) / qMax(1, height() - 2 * pad), 1.0);
        m_scrubY = qBound(pad, y, height() - pad);
        if (m_jump) m_jump(qRound(frac * sb->maximum()));
        update();
    }
    void scrubEnd() { m_scrubbing = false; m_hideTimer.start(); update(); }

    // Дата для карточки (из created_at истории — оно в UTC)
    static void setCardDate(QWidget *card, const QString &createdAtUtc)
    {
        QDateTime dt = QDateTime::fromString(createdAtUtc, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!dt.isValid()) return;
        dt.setTimeZone(QTimeZone::UTC);
        card->setProperty("cardDate", dt.toLocalTime().date());
    }

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == m_area->widget() && e->type() == QEvent::Move && side() != Off) {
            // Колонку листают — показываем шкалу, после остановки она погаснет
            place();
            if (hasDates()) {
                raise();
                show();
                m_grip->show();
                m_grip->raise();
                fadeTo(1.0);
                if (!m_scrubbing) m_hideTimer.start();
            }
            update();
        } else if (obj == m_area->viewport() && e->type() == QEvent::Resize) {
            place();
        }
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        if (m_opacity <= 0.0) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(m_opacity);

        // Без подложки: только засечки и даты в своих «таблетках» —
        // карточки под шкалой ничем не затемняются
        const bool atLeft = (x() == 0);
        const int W = width(), H = height();

        // Засечки у «своего» края, подписи — рядом с засечками
        const int edgeX = atLeft ? 2 : W - 2;
        const int dir   = atLeft ? 1 : -1;
        QFont f = font();
        f.setPixelSize(10);
        f.setBold(false);
        p.setFont(f);

        QWidget *content = m_area->widget();
        const int offY = content->y();
        QDate prev;
        int lastLabelY = -1000;
        QDate centerDate;
        int bestDist = INT_MAX;
        for (int i = 0; i < m_layout->count(); ++i) {
            QWidget *w = m_layout->itemAt(i)->widget();
            if (!w || w->isHidden()) continue;
            const QDate d = w->property("cardDate").toDate();
            if (!d.isValid()) continue;
            const int cy = offY + w->y() + w->height() / 2;
            const int dist = qAbs(cy - H / 2);
            if (dist < bestDist) { bestDist = dist; centerDate = d; }
            const bool newDay = (d != prev);
            prev = d;
            if (cy < -20 || cy > H + 20) continue;

            // Новый день — длинная засечка и дата; остальные карточки — короткая
            const int len = newDay ? 10 : 5;
            p.setPen(QPen(QColor(255, 255, 255, newDay ? 150 : 70), 1.2));
            p.drawLine(edgeX, cy, edgeX + dir * len, cy);
            if (newDay && cy - lastLabelY >= 18) {
                // Дата — в тёмной «таблетке»: читается поверх края карточек
                const QString t = label(d);
                const int tw = QFontMetrics(f).horizontalAdvance(t) + 8;
                const QRectF pill(atLeft ? edgeX + 12 : W - 12 - tw - 2, cy - 7, tw, 14);
                // Непрозрачная «таблетка» с рамкой — читается поверх любой карточки
                QPainterPath lp; lp.addRoundedRect(pill, 5, 5);
                p.fillPath(lp, QColor(28, 31, 46));
                p.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
                p.drawPath(lp);
                p.setPen(QColor(235, 238, 248));
                p.drawText(pill, Qt::AlignCenter, t);
                lastLabelY = cy;
            }
        }

        // Голубая линия посередине — какой день сейчас в центре колонки
        if (centerDate.isValid()) {
            const int cy = H / 2;
            p.setPen(QPen(QColor(120, 190, 255, 235), 2.0));
            p.drawLine(edgeX, cy, edgeX + dir * 12, cy);  // Короткая — не перечёркивает карточку
            QFont fb = f;
            fb.setBold(true);
            p.setFont(fb);
            const QString t = label(centerDate);
            const int tw = QFontMetrics(fb).horizontalAdvance(t) + 10;
            const QRectF pill(atLeft ? edgeX + 12 : W - 12 - tw - 2, cy - 7, tw, 14);
            QPainterPath pp; pp.addRoundedRect(pill, 7, 7);
            p.fillPath(pp, QColor(36, 74, 122));   // Непрозрачная — поверх карточки
            p.setPen(QColor(225, 240, 255));
            p.drawText(pill, Qt::AlignCenter, t);

            // Быстрая перемотка: у курсора — крупная дата
            if (m_scrubbing) {
                QFont fs = f;
                fs.setPixelSize(14);
                fs.setBold(true);
                p.setFont(fs);
                const int bw = QFontMetrics(fs).horizontalAdvance(t) + 18;
                const QRectF bubble(atLeft ? edgeX + 14 : W - 14 - bw - 2, m_scrubY - 13, bw, 26);
                QPainterPath bp; bp.addRoundedRect(bubble, 13, 13);
                p.fillPath(bp, QColor(120, 190, 255, 245));
                p.setPen(QColor(10, 22, 40));
                p.drawText(bubble, Qt::AlignCenter, t);
            }
        }
    }

private:
    enum Side { Off, Outer, Inner };
    static Side side()
    {
        const QString s = AppSettings::get().dateRuler();
        return s == QLatin1String("off") ? Off : s == QLatin1String("inner") ? Inner : Outer;
    }

    // «21.10» в этом году, «21.10.25» — в прошлые годы
    static QString label(const QDate &d)
    {
        return d.year() == QDate::currentDate().year() ? d.toString(QStringLiteral("dd.MM"))
                                                       : d.toString(QStringLiteral("dd.MM.yy"));
    }

    bool hasDates() const
    {
        for (int i = 0; i < m_layout->count(); ++i)
            if (QWidget *w = m_layout->itemAt(i)->widget())
                if (w->property("cardDate").isValid()) return true;
        return false;
    }

    // У края экрана: левая колонка — слева, правая — справа; «со стороны центра» — наоборот
    void place()
    {
        QWidget *vp = m_area->viewport();
        const bool atLeft = (side() == Inner) ? !m_leftColumn : m_leftColumn;
        setGeometry(atLeft ? 0 : vp->width() - width(), 0, width(), vp->height());
        // Полоса для перемотки — у линии шкалы (там, где засечки)
        const int gw = 22;
        m_grip->setGeometry(atLeft ? 0 : vp->width() - gw, 0, gw, vp->height());
    }

    void hideAll() { hide(); m_grip->hide(); }

    void fadeTo(qreal to)
    {
        if (qFuzzyCompare(m_opacity + 1.0, to + 1.0)) return;
        m_fade.stop();
        if (!Anim::enabled(Anim::Kind::Scroll)) {
            m_opacity = to;
            if (to <= 0.0) hideAll();
            update();
            return;
        }
        m_fade.setDuration(to > 0.5 ? 140 : 320);
        m_fade.setStartValue(m_opacity);
        m_fade.setEndValue(to);
        m_fade.start();
    }

    QScrollArea      *m_area;
    QLayout          *m_layout;
    bool              m_leftColumn;
    qreal             m_opacity = 0.0;
    QTimer            m_hideTimer;
    QVariantAnimation m_fade;
    std::function<void(int)> m_jump;
    DateRulerGrip    *m_grip = nullptr;
    bool              m_scrubbing = false;  // Идёт быстрая перемотка
    int               m_scrubY = 0;         // Где курсор (для крупной даты)
};

inline void DateRulerGrip::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) m_ruler->scrubStart(qRound(e->position().y()));
}
inline void DateRulerGrip::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton) m_ruler->scrubTo(qRound(e->position().y()));
}
inline void DateRulerGrip::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) m_ruler->scrubEnd();
}
