// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  CardShadow.h — Быстрая тень под карточками (SmartClip 2.0, этап 4)         ║
// ║                                                                              ║
// ║  QGraphicsDropShadowEffect заново размывает тень КАЖДЫЙ кадр, когда         ║
// ║  карточку надо перерисовать. При плавной прокрутке и «Дуге» это съедало     ║
// ║  больше половины времени кадра — прокрутка шла рывками.                     ║
// ║                                                                              ║
// ║  Здесь размытая тень скруглённого прямоугольника считается ОДИН раз, а     ║
// ║  потом растягивается под любую карточку «по девяти кускам» (углы как есть,  ║
// ║  края и середина — растянуты). Выглядит так же, рисуется в разы быстрее.    ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QGraphicsEffect>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QPixmap>
#include <QHash>
#include <QtMath>
#include <vector>

class CardShadowEffect : public QGraphicsEffect
{
public:
    explicit CardShadowEffect(QObject *parent = nullptr) : QGraphicsEffect(parent) {}

    // Как нарисовать карточку (эффекты «Дуги», см. CardArc.h): масштаб вокруг
    // середины верхнего края и прозрачность. Положение двигает CardArc.
    void setLook(qreal scale, qreal opacity)
    {
        if (qFuzzyCompare(scale, m_scale) && qFuzzyCompare(opacity, m_opacity)) return;
        const bool grow = scale > m_scale;
        m_scale = scale;
        m_opacity = opacity;
        if (grow) updateBoundingRect();  // Больше карточка — больше область рисования
        update();
    }
    qreal lookScale() const { return m_scale; }

    QRectF boundingRectFor(const QRectF &r) const override
    {
        const QRectF v = scaled(r);
        return r.united(v.adjusted(-EXTENT, -EXTENT + OFFSET_Y, EXTENT, EXTENT + OFFSET_Y));
    }

protected:
    void draw(QPainter *painter) override
    {
        const QRectF real = sourceBoundingRect(Qt::LogicalCoordinates);
        const QRectF src = scaled(real);  // Где карточка видна (с учётом масштаба)
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap &sh = shadowPixmap(dpr);

        // Куда рисовать тень: карточка, расширенная на размытие и сдвинутая вниз
        const QRectF t = src.adjusted(-EXTENT, -EXTENT + OFFSET_Y, EXTENT, EXTENT + OFFSET_Y);
        const qreal sz = sh.width();                  // Сторона картинки тени
        // Углы — как есть (в маленькой карточке — сколько влезает)
        const qreal cw = qMin<qreal>(CORNER, t.width() / 2), ch = qMin<qreal>(CORNER, t.height() / 2);
        const qreal sx = cw * dpr, sy = ch * dpr;     // ...в пикселях картинки

        const QRectF dst[9] = {
            {t.left(),          t.top(),           cw, ch},                          // ↖
            {t.left() + cw,     t.top(),           t.width() - 2 * cw, ch},          // ↑
            {t.right() - cw,    t.top(),           cw, ch},                          // ↗
            {t.left(),          t.top() + ch,      cw, t.height() - 2 * ch},         // ←
            {t.left() + cw,     t.top() + ch,      t.width() - 2 * cw, t.height() - 2 * ch}, // центр
            {t.right() - cw,    t.top() + ch,      cw, t.height() - 2 * ch},         // →
            {t.left(),          t.bottom() - ch,   cw, ch},                          // ↙
            {t.left() + cw,     t.bottom() - ch,   t.width() - 2 * cw, ch},          // ↓
            {t.right() - cw,    t.bottom() - ch,   cw, ch},                          // ↘
        };
        const qreal mx = sz - 2 * sx, my = sz - 2 * sy;  // Середина картинки (растягивается)
        const QRectF srcR[9] = {
            {0, 0, sx, sy},            {sx, 0, mx, sy},            {sz - sx, 0, sx, sy},
            {0, sy, sx, my},           {sx, sy, mx, my},           {sz - sx, sy, sx, my},
            {0, sz - sy, sx, sy},      {sx, sz - sy, mx, sy},      {sz - sx, sz - sy, sx, sy},
        };
        painter->save();                                       // Вернём прозрачность в конце
        painter->setOpacity(painter->opacity() * m_opacity);  // Тень и карточка тают вместе
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        for (int i = 0; i < 9; ++i)
            if (dst[i].width() > 0 && dst[i].height() > 0)
                painter->drawPixmap(dst[i], sh, srcR[i]);
        painter->restore();

        // Сама карточка — поверх тени, с тем же масштабом и прозрачностью
        const bool plain = qFuzzyCompare(m_scale, 1.0) && qFuzzyCompare(m_opacity, 1.0);
        if (plain) { drawSource(painter); painter->restore(); return; }
        painter->save();
        const QPointF o(real.center().x(), real.top());
        painter->translate(o);
        painter->scale(m_scale, m_scale);
        painter->translate(-o);
        drawSource(painter);
        painter->restore();
        painter->restore();
    }

private:
    QRectF scaled(const QRectF &r) const
    {
        if (qFuzzyCompare(m_scale, 1.0)) return r;
        const qreal w = r.width() * m_scale, h = r.height() * m_scale;
        return QRectF(r.center().x() - w / 2, r.top(), w, h);
    }

    qreal m_scale   = 1.0;
    qreal m_opacity = 1.0;

    // Как у прежней тени: blurRadius 40, сдвиг вниз на 8px, почти чёрная
    static constexpr int EXTENT   = 40;   // Насколько тень выходит за карточку
    static constexpr int OFFSET_Y = 8;
    static constexpr int RADIUS   = 10;   // Скругление карточки (ClickableCard)
    // Угол «девяти кусков»: снаружи — размытие, внутри — запас, чтобы
    // середина тени была уже полной (как под настоящей длинной карточкой)
    static constexpr int CORNER   = EXTENT + 44;

    // Готовая размытая тень (одна на масштаб экрана)
    static const QPixmap &shadowPixmap(qreal dpr)
    {
        static QHash<int, QPixmap> cache;
        const int key = qRound(dpr * 100);
        auto it = cache.find(key);
        if (it != cache.end()) return *it;

        const int side = qCeil((2 * CORNER + 4) * dpr);  // Середина — 4px, её и растягиваем
        QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            const qreal in = EXTENT * dpr;
            path.addRoundedRect(QRectF(in, in, side - 2 * in, side - 2 * in), RADIUS * dpr, RADIUS * dpr);
            p.fillPath(path, QColor(0, 0, 0, 240));
        }
        // Размытие: три прохода «коробочного» размытия ≈ размытие по Гауссу
        blurAlpha(img, qMax(1, qRound(13 * dpr)));
        QPixmap pm = QPixmap::fromImage(img);
        return *cache.insert(key, pm);
    }

    // Чёрная тень — размываем только прозрачность (по строкам, потом по столбцам)
    static void blurAlpha(QImage &img, int r)
    {
        const int w = img.width(), h = img.height();
        std::vector<int> a(w * h), tmp(w * h);
        for (int y = 0; y < h; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < w; ++x) a[y * w + x] = qAlpha(line[x]);
        }
        auto pass = [&](std::vector<int> &from, std::vector<int> &to, bool horizontal) {
            const int n = horizontal ? w : h, lines = horizontal ? h : w;
            for (int l = 0; l < lines; ++l) {
                auto at = [&](int i) -> int & {
                    return horizontal ? from[l * w + i] : from[i * w + l];
                };
                int sum = 0;
                for (int i = -r; i <= r; ++i) sum += at(qBound(0, i, n - 1));
                for (int i = 0; i < n; ++i) {
                    (horizontal ? to[l * w + i] : to[i * w + l]) = sum / (2 * r + 1);
                    sum += at(qMin(n - 1, i + r + 1)) - at(qMax(0, i - r));
                }
            }
        };
        for (int k = 0; k < 3; ++k) { pass(a, tmp, true); pass(tmp, a, false); }
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) line[x] = qRgba(0, 0, 0, a[y * w + x]);
        }
    }
};
