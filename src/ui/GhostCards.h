#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  GhostCards.h — «фантомные» карточки в пустом месте колонки                   ║
// ║                                                                              ║
// ║  Когда карточек мало (полупустые Закрепы, поиск нашёл пару штук), пустое      ║
// ║  место ниже последней карточки заполняют бледные контуры карточек — без       ║
// ║  текста и картинок. Ровно до низа видимой части колонки: прокрутку они не     ║
// ║  удлиняют, листать «бесконечно» нельзя. Мышь и клики их не замечают —         ║
// ║  это просто рисунок на фоне колонки.                                         ║
// ║                                                                              ║
// ║  Перерисовывается ТОЛЬКО пустое место и только когда оно поменялось: иначе    ║
// ║  выезд карточек под мышью заставлял бы перерисовывать всю колонку каждый      ║
// ║  кадр, и наведение шло рывками.                                              ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QObject>
#include <QScrollArea>
#include <QBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QEvent>
#include <QTimer>
#include <climits>

class GhostCards : public QObject
{
public:
    // images = true — колонка картинок (одинаковые высокие карточки),
    // false — колонка текстов (карточки разной высоты)
    GhostCards(QScrollArea *area, QBoxLayout *layout, bool images, int scalePct)
        : QObject(area), m_area(area), m_layout(layout), m_images(images), m_scale(scalePct)
    {
        // Следим только за фоном колонки (viewport): карточки там прозрачные,
        // и любое их изменение перерисовывает и фон под ними. За самой
        // колонкой не следим — при выходе она удаляется раньше нас.
        area->viewport()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        QWidget *viewport = m_area->viewport();
        if (obj != viewport) return false;
        if (e->type() == QEvent::Paint) {
            const Geo g = geometry();
            if (g.top != m_last.top || g.x != m_last.x || g.width != m_last.width) {
                // Пустое место сдвинулось — дорисуем его целиком на следующем шаге
                const int from = qMax(0, qMin(g.top, m_last.top));
                m_last = g;
                scheduleUpdate(from);
            }
            if (g.top < viewport->height() && static_cast<QPaintEvent *>(e)->rect().bottom() >= g.top)
                paint(viewport, g);
        } else if (e->type() == QEvent::Resize) {
            scheduleUpdate(0);
        }
        return false;
    }

private:
    struct Geo { int top = INT_MAX; int x = 0; int width = 0; };

    int s(int v) const { return v * m_scale / 100; }

    // Где начинается пустое место (координаты viewport) и какой ширины фантомы
    Geo geometry() const
    {
        Geo g;
        QWidget *content = m_area->widget();
        if (!content) return g;
        int bottom = -1, left = -1, width = -1;
        for (int i = 0; i < m_layout->count(); ++i) {
            QWidget *w = m_layout->itemAt(i)->widget();
            if (!w || w->isHidden()) continue;
            const QPoint base = w->property("arcBase").isValid() ? w->property("arcBase").toPoint() : w->pos();
            bottom = qMax(bottom, base.y() + w->height());
            if (w->width() > width) { left = base.x(); width = w->width(); }
        }
        const QMargins mg = m_layout->contentsMargins();
        if (width <= 0) {  // Колонка пустая — фантомы с самого верха
            left = mg.left();
            width = content->width() - mg.left() - mg.right();
            bottom = mg.top() - m_layout->spacing();
        }
        if (width <= 0) return g;
        QWidget *viewport = m_area->viewport();
        g.top = content->mapTo(viewport, QPoint(0, bottom + m_layout->spacing())).y();
        g.x = content->mapTo(viewport, QPoint(left, 0)).x();
        g.width = width;
        return g;
    }

    void scheduleUpdate(int from)
    {
        QTimer::singleShot(0, this, [this, from]() {
            QWidget *viewport = m_area->viewport();
            if (viewport && from < viewport->height())
                viewport->update(QRect(0, from, viewport->width(), viewport->height() - from));
        });
    }

    void paint(QWidget *viewport, const Geo &g)
    {
        // Высоты: картинки — одинаковые, тексты — разные (как настоящие)
        static const int textH[] = {46, 64, 46, 82, 54, 46, 70};
        const int gap = m_layout->spacing();
        const int vh = viewport->height();
        const int x = g.x, width = g.width;
        int y = g.top;
        QPainter p(viewport);
        p.setRenderHint(QPainter::Antialiasing);
        // Почти невидимая подложка под фантомами: окно SmartClip прозрачное, и
        // Windows пропускает клик сквозь пиксели без цвета — между фантомами
        // клик проваливался в программу под окном. С подложкой — как у карточек.
        p.fillRect(QRect(0, y, viewport->width(), vh - y), QColor(0, 0, 0, 1));
        for (int i = 0; y < vh; ++i) {
            const int h = m_images ? s(120) : s(textH[i % 7]);
            const qreal fade = qMax(0.55, 1.0 - i * 0.08);  // Чем ниже, тем бледнее
            QPainterPath path;
            path.addRoundedRect(QRectF(x + 0.5, y + 0.5, width - 1, h - 1), s(10), s(10));
            p.fillPath(path, QColor(16, 16, 26, int(150 * fade)));
            p.setPen(QPen(QColor(255, 255, 255, int(60 * fade)), 1));
            p.drawPath(path);
            if (!m_images) {
                // Пара «строк текста» — бледные полоски
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, int(36 * fade)));
                p.drawRoundedRect(QRectF(x + s(12), y + s(12), width * 0.55, s(7)), s(3), s(3));
                if (h > s(50))
                    p.drawRoundedRect(QRectF(x + s(12), y + s(26), width * 0.75, s(7)), s(3), s(3));
            } else {
                // Значок «картинка» по центру
                p.setPen(QPen(QColor(255, 255, 255, int(64 * fade)), 1.4));
                p.setBrush(Qt::NoBrush);
                const QRectF ic(x + width / 2.0 - s(16), y + h / 2.0 - s(12), s(32), s(24));
                p.drawRoundedRect(ic, s(3), s(3));
                p.drawLine(QPointF(ic.left() + s(4), ic.bottom() - s(4)), QPointF(ic.center().x() - s(2), ic.center().y()));
                p.drawLine(QPointF(ic.center().x() - s(2), ic.center().y()), QPointF(ic.right() - s(4), ic.bottom() - s(4)));
                p.drawEllipse(QPointF(ic.right() - s(8), ic.top() + s(7)), s(2), s(2));
            }
            y += h + gap;
        }
    }

    QScrollArea *m_area;
    QBoxLayout  *m_layout;
    bool         m_images;
    int          m_scale;
    Geo          m_last;
};
