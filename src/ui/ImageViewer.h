#pragma once

#include <QWidget>
#include <QPixmap>

// Полноэкранный просмотрщик изображений поверх SmartClip
// Колёсико — зум относительно курсора
// Зажал левую кнопку + тащи — перемещение
// Двойной клик — закрыть, вернуться в SmartClip
class ImageViewer : public QWidget
{
    Q_OBJECT

public:
    explicit ImageViewer(QWidget *parent = nullptr);

    // Загрузить и показать картинку
    void showImage(const QString &filepath);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void resetView(); // вернуть масштаб 1:1, центрировать

    QPixmap m_pixmap;
    qreal   m_scale       = 1.0;
    QPoint  m_offset;       // текущее смещение изображения на экране
    QPoint  m_dragStart;    // точка начала перетаскивания (в px)
    QPoint  m_offsetStart;  // смещение в момент начала перетаскивания
    bool    m_dragging    = false;
};
