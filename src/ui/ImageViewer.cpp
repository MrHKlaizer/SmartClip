#include "ImageViewer.h"

#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>

ImageViewer::ImageViewer(QWidget *parent)
    : QWidget(parent)
{
    // Полупрозрачный чёрный фон — затемняет SmartClip под собой
    setStyleSheet("background-color: rgba(0, 0, 0, 210);");
    setCursor(Qt::OpenHandCursor);
    hide();
}

void ImageViewer::showImage(const QString &filepath)
{
    m_pixmap = QPixmap(filepath);
    // Растягиваемся на весь родительский виджет (MainWindow)
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    resetView();
    show();
    raise();
}

void ImageViewer::resetView()
{
    m_scale = 1.0;

    // Центрируем изображение при первом показе
    if (!m_pixmap.isNull()) {
        m_offset = QPoint(
            (width()  - m_pixmap.width())  / 2,
            (height() - m_pixmap.height()) / 2
        );
    }
    update();
}

void ImageViewer::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    // Затемняющий фон
    p.fillRect(rect(), QColor(0, 0, 0, 210));

    if (m_pixmap.isNull()) return;

    p.setRenderHint(QPainter::SmoothPixmapTransform);

    int w = static_cast<int>(m_pixmap.width()  * m_scale);
    int h = static_cast<int>(m_pixmap.height() * m_scale);

    p.drawPixmap(m_offset.x(), m_offset.y(), w, h, m_pixmap);

    // Подсказка в углу
    p.setPen(QColor(180, 180, 180, 160));
    p.setFont(QFont("Segoe UI", 11));
    p.drawText(rect().adjusted(0, 0, -16, -16),
               Qt::AlignBottom | Qt::AlignRight,
               tr("двойной клик — закрыть"));
}

void ImageViewer::wheelEvent(QWheelEvent *event)
{
    // Позиция курсора — точка, вокруг которой зумируем
    QPointF mouse = event->position();
    qreal oldScale = m_scale;

    if (event->angleDelta().y() > 0)
        m_scale *= 1.15;   // приближение
    else
        m_scale /= 1.15;   // удаление

    // Ограничиваем диапазон масштаба
    m_scale = qBound(0.05, m_scale, 20.0);

    // Корректируем смещение так, чтобы точка под курсором не смещалась
    qreal ratio = m_scale / oldScale;
    m_offset.setX(static_cast<int>(mouse.x() - ratio * (mouse.x() - m_offset.x())));
    m_offset.setY(static_cast<int>(mouse.y() - ratio * (mouse.y() - m_offset.y())));

    update();
    event->accept();
}

void ImageViewer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging    = true;
        m_dragStart   = event->pos();
        m_offsetStart = m_offset;
        setCursor(Qt::ClosedHandCursor);
    }
}

void ImageViewer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        QPoint delta = event->pos() - m_dragStart;
        m_offset = m_offsetStart + delta;
        update();
    }
}

void ImageViewer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent *)
{
    // Скрываемся — SmartClip остаётся открытым
    hide();
}
