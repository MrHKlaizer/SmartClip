#pragma once

#include <QLabel>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

// QLabel + clicked() + hover + тень без артефактов на скруглённых углах.
//
// В режиме редактирования (setEditMode):
//   - левый клик коротко → emit clicked() (toggle выделения)
//   - долгое нажатие (250 мс) + движение → drag-to-reorder сигналы
class ClickableCard : public QLabel
{
    Q_OBJECT

public:
    explicit ClickableCard(QWidget *parent = nullptr) : QLabel(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAutoFillBackground(false);

        m_pressTimer.setSingleShot(true);
        m_pressTimer.setInterval(250);
        connect(&m_pressTimer, &QTimer::timeout, this, [this]() {
            if (m_pressing && m_editMode) {
                m_dragReady = true;
                setCursor(Qt::SizeVerCursor);
            }
        });
    }

    void setCardStyle(const QColor &bg, int radius = 10)
    {
        m_bgColor = bg;
        m_radius  = radius;
        update();
    }

    void setEditMode(bool on)
    {
        m_editMode  = on;
        m_dragReady = false;
        m_dragging  = false;
        m_pressing  = false;
        if (!on) setCursor(Qt::PointingHandCursor);
        update();
    }

    void setSelected(bool sel) { m_selected = sel; update(); }
    bool isSelected() const    { return m_selected; }

signals:
    void clicked();
    void dragStarted(QPoint globalPos);
    void dragMoved(QPoint globalPos);
    void dragFinished();

protected:
    void paintEvent(QPaintEvent *event) override
    {
        {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);
            QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
            QPainterPath path;
            path.addRoundedRect(r, m_radius, m_radius);

            p.fillPath(path, m_bgColor);

            if (m_editMode && m_selected)
                p.fillPath(path, QColor(255, 200, 50, 45));

            // Рамка: золотая при выделении, яркая при перетаскивании, hover/обычная иначе
            QColor border;
            if (m_dragging)                border = QColor(120, 200, 255, 220);
            else if (m_editMode && m_selected) border = QColor(255, 215, 70, 210);
            else if (m_hovered)            border = QColor(255, 255, 255, 90);
            else                           border = QColor(255, 255, 255, 55);
            p.setPen(QPen(border, 2.0));
            p.drawPath(path);
        }

        QLabel::paintEvent(event);

        // Индикатор выделения в режиме редактирования
        if (m_editMode) {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            QRectF cr(width() - 23, 5, 16, 16);
            if (m_selected) {
                p.setBrush(QColor(255, 200, 50, 225));
                p.setPen(QPen(QColor(255, 220, 100, 240), 1.5));
                p.drawEllipse(cr);
                p.setPen(QPen(QColor(20, 10, 0, 230), 2,
                              Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                QPainterPath check;
                check.moveTo(cr.x() + 3.5, cr.center().y() + 0.5);
                check.lineTo(cr.center().x() - 0.5, cr.bottom() - 3.5);
                check.lineTo(cr.right() - 2.5, cr.top() + 3.5);
                p.drawPath(check);
            } else {
                p.setBrush(QColor(255, 255, 255, 25));
                p.setPen(QPen(QColor(255, 255, 255, 100), 1.5));
                p.drawEllipse(cr);
            }
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (!m_editMode) {
                emit clicked();
            } else {
                m_pressing  = true;
                m_dragReady = false;
                m_dragging  = false;
                m_pressPos  = event->pos();
                m_pressTimer.start();
            }
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_pressing && m_editMode) {
            if (!m_dragging && m_dragReady &&
                (event->pos() - m_pressPos).manhattanLength() > 6)
            {
                m_dragging = true;
                update();
                emit dragStarted(mapToGlobal(event->pos()));
            }
            if (m_dragging)
                emit dragMoved(mapToGlobal(event->pos()));
        }
        QLabel::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_editMode) {
            m_pressTimer.stop();
            if (m_dragging) {
                m_dragging  = false;
                m_dragReady = false;
                m_pressing  = false;
                setCursor(Qt::PointingHandCursor);
                update();
                emit dragFinished();
            } else if (m_pressing) {
                m_pressing  = false;
                m_dragReady = false;
                emit clicked();
            }
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        update();
        QLabel::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        update();
        QLabel::leaveEvent(event);
    }

private:
    QColor m_bgColor  { 20, 20, 32, 245 };
    int    m_radius   = 10;
    bool   m_hovered  = false;
    bool   m_editMode = false;
    bool   m_selected = false;

    // Drag-to-reorder state
    bool   m_pressing  = false;
    bool   m_dragReady = false;
    bool   m_dragging  = false;
    QPoint m_pressPos;
    QTimer m_pressTimer;
};
