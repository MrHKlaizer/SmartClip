// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  ImageViewer.cpp — Реализация полноэкранного просмотрщика изображений      ║
// ║                                                                              ║
// ║  Этот файл содержит реальный код методов, объявленных в ImageViewer.h.     ║
// ║  Здесь: рисование картинки, зум, перетаскивание.                            ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "ImageViewer.h"

// Классы для рисования и обработки событий мыши:
#include <QPainter>      // Рисование: картинки, текст, фигуры
#include <QWheelEvent>   // Событие прокрутки колёсика мыши
#include <QMouseEvent>   // Событие нажатия / движения / отпускания мыши
#include <QEasingCurve>
#include <QtMath>
#include <windows.h>
#include "Anim.h"         // Настройки анимаций (Настройки → Оформление)

// ─── Конструктор ─────────────────────────────────────────────────────────────
// Просмотрщик — ОТДЕЛЬНОЕ прозрачное окно поверх SmartClip, а не часть его
// окна. Раньше каждый кадр полёта и зума заставлял перерисовывать всё окно
// SmartClip (все карточки с тенями на весь экран) — анимация шла рывками.
ImageViewer::ImageViewer(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                      | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);   // Фон рисуем сами, полупрозрачным
    setAttribute(Qt::WA_ShowWithoutActivating);   // Не отбираем фокус (см. MainWindow.h)

    // Курсор в форме «раскрытой руки» — намекает что можно тащить картинку.
    setCursor(Qt::OpenHandCursor);

    m_tick.setTimerType(Qt::PreciseTimer);
    m_tick.setInterval(6);
    connect(&m_tick, &QTimer::timeout, this, [this]() { tick(); });

    // Скрываемся при создании — показываем только когда вызывают showImage().
    hide();
}

// ─── Показать изображение ─────────────────────────────────────────────────────
// Загружает файл, накрывает окно SmartClip и показывает картинку.
void ImageViewer::showImage(const QString &filepath, QWidget *source)
{
    // QPixmap(filepath) — загружает картинку с диска в видеопамять.
    // Если файл не найден или повреждён — m_pixmap будет «нулевым» (isNull() = true).
    m_pixmap = QPixmap(filepath);
    m_source = source;

    // Накрываем окно SmartClip (оно на весь экран)
    if (parentWidget())
        setGeometry(parentWidget()->geometry());

    resetView();  // Вписываем картинку в экран и ставим по центру
    m_closing = false;
    m_dragging = false;

    // Копия под размер на экране (с учётом масштаба Windows) — один раз
    m_display = QPixmap();
    if (!m_pixmap.isNull()) {
        const QSize sz = (targetRect().size() * devicePixelRatioF()).toSize();
        if (sz.width() < m_pixmap.width())
            m_display = m_pixmap.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (Anim::enabled(Anim::Kind::Viewer) && !m_pixmap.isNull()) {
        // Картинка вырастает из карточки, фон проявляется. Плавный разгон и
        // долгое мягкое торможение (кривая Material «emphasized»)
        m_t = 0.0;
        QEasingCurve soft(QEasingCurve::BezierSpline);
        soft.addCubicBezierSegment(QPointF(0.2, 0.0), QPointF(0.0, 1.0), QPointF(1.0, 1.0));
        startFlight(1.0, Anim::ms(Anim::Kind::Viewer, 420), soft);
    } else {
        m_t = 1.0;
        m_flying = false;
    }
    show();
    // Поверх SmartClip, но без активации: клик по картинке не должен
    // отбирать фокус у программы, куда потом вставляем
    HWND h = reinterpret_cast<HWND>(winId());
    SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ─── Закрытие с анимацией ─────────────────────────────────────────────────────
// То же, что появление, задом наперёд и в 2 раза быстрее: картинка улетает
// обратно в свою карточку, фон тает. Летит с того места и масштаба, где
// она сейчас (её могли приблизить и сдвинуть).
void ImageViewer::closeAnimated()
{
    if (!isVisible() || m_closing) return;
    if (!Anim::enabled(Anim::Kind::Viewer)) { m_tick.stop(); hide(); return; }
    m_closing = true;
    m_dragging = false;
    m_scaleTarget = m_scale;  // Недоигранный зум останавливаем на месте
    startFlight(0.0, qMax(1, int(Anim::ms(Anim::Kind::Viewer, 200) * m_t)), QEasingCurve::InOutCubic);
}

void ImageViewer::startFlight(qreal to, int durationMs, QEasingCurve curve)
{
    m_from = m_t;
    m_to = to;
    m_duration = qMax(1, durationMs);
    m_curve = curve;
    m_flying = true;
    m_clockStarted = false;  // Отсчёт — с первого нарисованного кадра (см. paintEvent)
    if (!m_tick.isActive()) { m_frameClock.start(); m_lastFrame = 0; m_tick.setInterval(Anim::frameMs(this)); m_tick.start(); }
    update();
}

void ImageViewer::tick()
{
    const qint64 now = m_frameClock.elapsed();
    const qreal dt = qBound<qreal>(0.001, (now - m_lastFrame) / 1000.0, 0.05);
    m_lastFrame = now;

    // Полёт между карточкой и местом на экране
    if (m_flying && m_clockStarted) {
        const qreal p = qMin(1.0, m_clock.nsecsElapsed() / 1e6 / m_duration);
        m_t = m_from + (m_to - m_from) * m_curve.valueForProgress(p);
        if (p >= 1.0) {
            m_t = m_to;
            m_flying = false;
            if (m_closing) { m_closing = false; m_tick.stop(); hide(); return; }
        }
    }

    // Плавный зум: масштаб догоняет цель (как прокрутка колонок)
    bool zooming = false;
    if (!m_flying && !qFuzzyCompare(m_scale, m_scaleTarget)) {
        const qreal tau = Anim::ms(Anim::Kind::Viewer, 75) / 1000.0;
        const qreal k = 1.0 - qExp(-dt / tau);
        qreal s = m_scale + (m_scaleTarget - m_scale) * k;
        if (qAbs(s - m_scaleTarget) / m_scaleTarget < 0.002) s = m_scaleTarget;
        applyZoom(s, m_zoomAnchor);
        zooming = !qFuzzyCompare(m_scale, m_scaleTarget);
    }

    update();
    if (!m_flying && !zooming) m_tick.stop();
}

// Масштаб вокруг точки: точка картинки под курсором остаётся под курсором
void ImageViewer::applyZoom(qreal newScale, QPointF anchor)
{
    const qreal ratio = newScale / m_scale;
    m_offset = anchor - (anchor - m_offset) * ratio;
    m_scale = newScale;
}

// Где картинка сейчас лежит в своей карточке (вписана с сохранением пропорций).
// Карточку прокрутили за край или удалили — пустой прямоугольник.
QRectF ImageViewer::sourceRect() const
{
    if (!m_source || !m_source->isVisible() || m_pixmap.isNull())
        return {};
    const QRect card(mapFromGlobal(m_source->mapToGlobal(QPoint(0, 0))), m_source->size());
    // Карточка видна в своей колонке? (иначе картинка улетела бы «в никуда»)
    if (QWidget *vp = m_source->parentWidget() ? m_source->parentWidget()->parentWidget() : nullptr) {
        const QRect view(mapFromGlobal(vp->mapToGlobal(QPoint(0, 0))), vp->size());
        if (!view.intersects(card)) return {};
    }
    QSizeF sz = QSizeF(m_pixmap.size()).scaled(QSizeF(card.size()) * 0.92, Qt::KeepAspectRatio);
    return QRectF(QPointF(card.center()) - QPointF(sz.width() / 2, sz.height() / 2), sz);
}

QRectF ImageViewer::targetRect() const
{
    return QRectF(m_offset, QSizeF(m_pixmap.width() * m_scale, m_pixmap.height() * m_scale));
}

// ─── Сброс вида ───────────────────────────────────────────────────────────────
// Картинка по центру экрана: 1:1, а большая — вписана в экран.
void ImageViewer::resetView()
{
    m_scale = 1.0;  // Масштаб 1.0 = оригинальный размер
    if (!m_pixmap.isNull()) {
        // Большую картинку (скриншот 4K на экране Full HD) сразу вписываем в экран —
        // раньше она открывалась в 100% и не помещалась
        const qreal fit = qMin(width()  * 0.95 / m_pixmap.width(),
                               height() * 0.95 / m_pixmap.height());
        if (fit < 1.0) m_scale = fit;
        const qreal w = m_pixmap.width()  * m_scale;
        const qreal h = m_pixmap.height() * m_scale;
        m_offset = QPointF((width() - w) / 2, (height() - h) / 2);
    }
    m_scaleTarget = m_scale;
    update();  // Просим Qt перерисовать виджет (вызовет paintEvent)
}

// ─── Рисование ────────────────────────────────────────────────────────────────
// Qt вызывает этот метод каждый раз когда нужно обновить изображение на экране.
void ImageViewer::paintEvent(QPaintEvent *)
{
    // Полёт считает время с момента, когда первый кадр реально нарисован:
    // иначе показ окна и подготовка картинки «съедали» начало полёта
    if (m_flying && !m_clockStarted) { m_clock.start(); m_clockStarted = true; }

    QPainter p(this);  // «Кисть» привязанная к нашему виджету

    // Полупрозрачный чёрный фон — затемняет SmartClip под собой.
    // Во время появления/ухода проявляется/тает вместе с анимацией.
    p.fillRect(rect(), QColor(0, 0, 0, int(210 * qBound(0.0, m_t, 1.0))));

    if (m_pixmap.isNull()) return;  // Если картинка не загружена — рисовать нечего

    // SmoothPixmapTransform — плавное масштабирование с интерполяцией пикселей.
    // Без этой настройки при зуме картинка выглядит «пиксельно».
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Где рисовать картинку: на своём месте (масштаб + смещение), а во время
    // полёта — по пути между карточкой и этим местом
    const QRectF to = targetRect();
    QRectF r = to;
    if (m_t < 1.0) {
        QRectF from = sourceRect();
        if (from.isEmpty()) {
            // Карточки не видно — картинка просто растёт из центра и проявляется
            from = QRectF(to.center() - QPointF(to.width() * 0.4, to.height() * 0.4),
                          to.size() * 0.8);
            p.setOpacity(qBound(0.0, m_t, 1.0));
        }
        auto lerp = [this](qreal a, qreal b) { return a + (b - a) * m_t; };
        r = QRectF(QPointF(lerp(from.left(), to.left()), lerp(from.top(), to.top())),
                   QPointF(lerp(from.right(), to.right()), lerp(from.bottom(), to.bottom())));
    }
    // Уменьшенная копия, пока её хватает по размеру; приблизили сильнее — оригинал
    const bool useDisplay = !m_display.isNull()
        && r.width() * devicePixelRatioF() <= m_display.width() * 1.02;
    const QPixmap &pm = useDisplay ? m_display : m_pixmap;
    p.drawPixmap(r, pm, QRectF(pm.rect()));
    p.setOpacity(1.0);
    if (m_t < 1.0) return;  // Подсказку показываем, когда картинка на месте

    // ── Подсказка в правом нижнем углу ────────────────────────────────────────
    p.setPen(QColor(180, 180, 180, 160));      // Светло-серый полупрозрачный текст
    p.setFont(QFont("Segoe UI", 11));
    // adjusted(0, 0, -16, -16) — отступ 16px от правого и нижнего края
    p.drawText(rect().adjusted(0, 0, -16, -16),
               Qt::AlignBottom | Qt::AlignRight,
               tr("двойной клик — закрыть"));  // tr() — поддержка перевода
}

// ─── Зум колёсиком мыши ───────────────────────────────────────────────────────
// Масштабирует картинку вокруг точки где находится курсор (не центра экрана!).
// В новом стиле масштаб плавно доезжает до цели, быстрые щелчки складываются.
void ImageViewer::wheelEvent(QWheelEvent *event)
{
    event->accept();
    if (m_flying) return;  // Картинка ещё летит
    const int dy = event->angleDelta().y();
    if (dy == 0) return;

    // Один щелчок колёсика (120) = ±15%; мелкие шаги тачпада — пропорционально.
    // qBound — масштаб от 5% до 2000% оригинала.
    const qreal factor = qPow(1.15, dy / 120.0);
    m_scaleTarget = qBound(0.05, m_scaleTarget * factor, 20.0);
    m_zoomAnchor = event->position();

    if (!Anim::enabled(Anim::Kind::Viewer)) {
        applyZoom(m_scaleTarget, m_zoomAnchor);  // Анимации выключены в Windows — сразу
        update();
        return;
    }
    if (!m_tick.isActive()) { m_frameClock.start(); m_lastFrame = 0; m_tick.setInterval(Anim::frameMs(this)); m_tick.start(); }
}

// ─── Нажатие кнопки мыши — начало перетаскивания ─────────────────────────────
void ImageViewer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_flying) {
        m_dragging    = true;           // Начинаем режим перетаскивания
        m_dragStart   = event->pos();   // Запоминаем где нажали
        m_offsetStart = m_offset;       // Запоминаем текущее положение картинки
        setCursor(Qt::ClosedHandCursor); // Курсор «сжатая рука» — тянем объект
    }
}

// ─── Движение мыши — обновление положения картинки ───────────────────────────
void ImageViewer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        // Новое положение = исходное положение + насколько сдвинулся курсор
        m_offset = m_offsetStart + QPointF(event->pos() - m_dragStart);
        update();  // Перерисовать с новым смещением
    }
}

// ─── Отпускание кнопки мыши — завершение перетаскивания ──────────────────────
void ImageViewer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);  // Возвращаем курсор «раскрытая рука»
    }
}

// ─── Двойной клик — закрыть просмотрщик ─────────────────────────────────────
void ImageViewer::mouseDoubleClickEvent(QMouseEvent *)
{
    // Скрываем просмотрщик (с анимацией), но НЕ удаляем его.
    // SmartClip остаётся открытым, пользователь видит историю.
    closeAnimated();
}
