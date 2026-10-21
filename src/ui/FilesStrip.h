#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  FilesStrip.h — полоска свежих файлов внизу по центру, над поиском           ║
// ║                                                                              ║
// ║  Свёрнута — тонкая плашка с маленькими значками. Навели мышь — плавно        ║
// ║  раскрывается вверх: плитки или список. Клик — вставить файл (как у видео),  ║
// ║  можно перетащить мышкой в Telegram/Discord, правый клик — меню.             ║
// ║  Папки, срок, количество, вид и типы файлов — Настройки → Файлы.             ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QCursor>
#include <QElapsedTimer>
#include <QEasingCurve>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QEnterEvent>
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QLocale>
#include <algorithm>
#include <functional>

#include "../core/AppSettings.h"
#include "../core/Thumbs.h"
#include "Anim.h"
#include "SmartButton.h"   // SmartButton::iconFontFamily() — системный шрифт значков

namespace Files {

// Тип файла по расширению — для фильтра «Какие файлы показывать»
inline QString category(const QString &path)
{
    static const QHash<QString, QString> map = []() {
        QHash<QString, QString> m;
        auto add = [&m](const char *cat, std::initializer_list<const char *> exts) {
            for (const char *e : exts) m.insert(QLatin1String(e), QLatin1String(cat));
        };
        add("image",    {"png", "jpg", "jpeg", "gif", "webp", "bmp", "tif", "tiff", "heic", "svg", "ico"});
        add("video",    {"mp4", "mkv", "mov", "avi", "webm", "wmv", "m4v", "flv"});
        add("audio",    {"mp3", "wav", "ogg", "flac", "m4a", "aac", "opus", "wma"});
        add("docs",     {"pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "rtf",
                         "odt", "ods", "odp", "csv", "md", "epub", "json", "xml"});
        add("archives", {"zip", "rar", "7z", "tar", "gz", "bz2", "xz", "iso"});
        add("programs", {"exe", "msi", "bat", "cmd", "apk", "appx", "msix", "msixbundle"});
        return m;
    }();
    return map.value(QFileInfo(path).suffix().toLower(), QStringLiteral("other"));
}

inline QStringList allCategories()
{
    return {"image", "video", "audio", "docs", "archives", "programs", "other"};
}

// Стандартные папки — «Загрузки»
inline QStringList defaultFolders()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return d.isEmpty() ? QStringList() : QStringList{d};
}

// Папки из настроек; ещё не выбирали — стандартные
inline QStringList configuredFolders()
{
    return AppSettings::get().hasFilesFolders() ? AppSettings::get().filesFolders() : defaultFolders();
}

// Недокачанные и служебные файлы не показываем
inline bool skipFile(const QFileInfo &fi)
{
    static const QStringList junk = {"crdownload", "part", "partial", "tmp", "download",
                                     "opdownload", "lnk", "url", "ini"};
    return fi.fileName().startsWith(QLatin1String("~$")) || junk.contains(fi.suffix().toLower());
}

struct Entry {
    QString   path;
    QString   name;
    qint64    size = 0;
    QDateTime mtime;
};

// Свежие файлы: за days дней, не больше max, новые первыми (в фоновом потоке)
inline QList<Entry> scan(const QStringList &folders, int days, int max, const QStringList &cats)
{
    QList<Entry> out;
    const QDateTime since = QDateTime::currentDateTime().addDays(-days);
    for (const QString &folder : folders) {
        QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            // Скопированный файл сохраняет старую дату изменения — берём и дату
            // появления в папке, что новее
            const QDateTime t = qMax(fi.lastModified(), fi.birthTime().isValid() ? fi.birthTime() : QDateTime());
            if (skipFile(fi) || t < since) continue;
            if (!cats.contains(category(fi.filePath()))) continue;
            out.append({fi.filePath(), fi.fileName(), fi.size(), t});
        }
    }
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.mtime > b.mtime; });
    if (out.size() > max) out.resize(max);
    return out;
}

inline QString sizeText(qint64 b)
{
    return QLocale().formattedDataSize(b, 1, QLocale::DataSizeTraditionalFormat);
}

// «14:05» — сегодня, «20.10» — раньше
inline QString timeText(const QDateTime &t)
{
    return t.date() == QDate::currentDate() ? t.toString("HH:mm") : t.toString("dd.MM");
}

} // namespace Files

class FilesStrip : public QWidget
{
public:
    // thumbnailer — превью файла как в Проводнике (вызывается в фоновом потоке)
    FilesStrip(QWidget *parent, int scalePct, std::function<QImage(const QString &, QSize)> thumbnailer)
        : QWidget(parent), m_scale(scalePct), m_thumbnailer(std::move(thumbnailer))
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
        // Свой точный таймер (~165 кадров/с), как у просмотра видео: обычные
        // анимации Qt в прозрачном окне SmartClip шли рывками
        m_tick.setTimerType(Qt::PreciseTimer);
        m_tick.setInterval(6);
        connect(&m_tick, &QTimer::timeout, this, [this]() { tick(); });
        m_leaveTimer.setSingleShot(true);
        m_leaveTimer.setInterval(60);  // Увели мышь — почти сразу сворачиваемся
        connect(&m_leaveTimer, &QTimer::timeout, this, [this]() { expand(false); });
        // Подстраховка: Windows не всегда сразу сообщает, что мышь ушла
        // (над полоской окно прозрачное) — проверяем курсор сами
        m_hoverPoll.setInterval(40);
        connect(&m_hoverPoll, &QTimer::timeout, this, [this]() {
            if (m_target <= 0) { m_hoverPoll.stop(); return; }
            if (m_dragging || QApplication::activePopupWidget()) return;  // Тянем файл или открыто меню
            const QPoint p = mapFromGlobal(QCursor::pos());
            if (!rect().contains(p) || !hitVisible(p)) expand(false);
        });
        hide();
    }

    std::function<void(const QString &)> onPick;                  // Клик — вставить файл
    std::function<void(const QString &, const QPoint &)> onMenu;  // Правый клик
    std::function<void()> onDropped;                              // Файл перетащили в другую программу

    // Где стоять: прямоугольник прозрачной середины окна (координаты родителя)
    void place(const QRect &center)
    {
        m_center = center;
        relayout();
    }

    // Перечитать папки (при каждом открытии SmartClip и после настроек)
    void reload()
    {
        const AppSettings &s = AppSettings::get();
        m_enabled = s.filesStrip();
        m_view = s.filesView();
        if (!m_enabled) { m_all.clear(); applyFilter(); return; }
        const QStringList folders = Files::configuredFolders();
        const int days = s.filesDays(), max = s.filesMax();
        const QStringList cats = s.filesTypes();
        const int gen = ++m_gen;
        QPointer<FilesStrip> self = this;
        Thumbs::runAsync(this, [folders, days, max, cats]() {
            QVariantList l;
            for (const Files::Entry &e : Files::scan(folders, days, max, cats))
                l << QVariant(QVariantList{e.path, e.name, e.size, e.mtime});
            return QVariant(l);
        }, [self, gen](const QVariant &v) {
            if (!self || gen != self->m_gen) return;
            self->m_all.clear();
            for (const QVariant &x : v.toList()) {
                const QVariantList r = x.toList();
                self->m_all.append({r[0].toString(), r[1].toString(), r[2].toLongLong(), r[3].toDateTime()});
            }
            self->applyFilter();
        });
    }

    // Поиск внизу фильтрует и файлы — по имени
    void setFilter(const QString &q)
    {
        m_query = q.trimmed();
        applyFilter();
    }

    // Свернуть сразу, без анимации (окно спряталось)
    void collapseNow()
    {
        m_tick.stop();
        m_leaveTimer.stop();
        m_hoverPoll.stop();
        m_t = m_target = 0;
        m_scroll = 0;
        m_hover = -1;
        relayout();
    }

    // Видимая часть (виджет размером с раскрытую полоску, над видимой частью —
    // прозрачно, клики там проходят насквозь, см. MainWindow::isInteractiveArea)
    bool hitVisible(const QPoint &p) const { return p.y() >= top(); }

protected:
    void enterEvent(QEnterEvent *e) override
    {
        if (!hitVisible(e->position().toPoint())) return;
        m_leaveTimer.stop();
        expand(true);
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
        if (!m_dragging) m_leaveTimer.start();
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!hitVisible(e->position().toPoint())) {  // Над полоской — прозрачно
            if (m_hover != -1) { m_hover = -1; update(); }
            if (!m_dragging && m_target > 0 && !m_leaveTimer.isActive()) m_leaveTimer.start();
            return;
        }
        m_leaveTimer.stop();
        if (m_target < 1) expand(true);
        const int h = itemAt(e->position().toPoint());
        if (h != m_hover) { m_hover = h; update(); }
        setCursor(h >= 0 || viewBtnAt(e->position().toPoint()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        // Тянем файл мышкой — перетаскивание в другую программу
        if ((e->buttons() & Qt::LeftButton) && m_press >= 0 && m_press < m_items.size()
            && (e->position().toPoint() - m_pressPos).manhattanLength() > QApplication::startDragDistance()) {
            const QString path = m_items[m_press].path;
            m_press = -1;
            startDrag(path);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        m_press = -1;
        if (m_t < 0.9) return;  // Ещё раскрывается
        const QPoint p = e->position().toPoint();
        if (!hitVisible(p)) return;
        const int btn = viewBtnAt(p);
        if (btn >= 0 && e->button() == Qt::LeftButton) {
            m_view = btn == 0 ? QStringLiteral("tiles") : QStringLiteral("list");
            AppSettings::get().setFilesView(m_view);
            m_scroll = 0;
            relayout();
            return;
        }
        const int i = itemAt(p);
        if (i < 0) return;
        if (e->button() == Qt::LeftButton) { m_press = i; m_pressPos = p; }
        else if (e->button() == Qt::RightButton && onMenu) onMenu(m_items[i].path, e->globalPosition().toPoint());
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_press >= 0 && m_press < m_items.size()
            && itemAt(e->position().toPoint()) == m_press) {
            const QString path = m_items[m_press].path;
            m_press = -1;
            if (onPick) onPick(path);
            return;
        }
        m_press = -1;
    }

    void wheelEvent(QWheelEvent *e) override
    {
        m_scroll = qBound(0, m_scroll - e->angleDelta().y() / 2, maxScroll());
        m_hover = itemAt(e->position().toPoint());
        update();
        e->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.translate(0, top());  // Дальше — координаты видимой части
        const QRectF r = QRectF(0, 0, width(), curH()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath bg;
        bg.addRoundedRect(r, s(12), s(12));
        p.fillPath(bg, QColor(18, 18, 30, 245));
        p.setPen(QPen(QColor(255, 255, 255, 28), 1));
        p.drawPath(bg);
        p.setClipPath(bg);

        // Заголовок: значок папки, «Файлы», сколько; справа — плитки/список
        QFont icon(SmartButton::iconFontFamily());
        icon.setPixelSize(s(14));
        QFont f = font();
        f.setPixelSize(s(12));
        const QRect head(s(12), 0, width() - s(24), s(HeadH));
        p.setFont(icon);
        p.setPen(QColor(160, 200, 255));
        p.drawText(head, Qt::AlignLeft | Qt::AlignVCenter, QString(QChar(0xE8B7)));
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 190));
        p.drawText(head.adjusted(s(24), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QApplication::translate("FilesStrip", "Свежие файлы · %1").arg(m_items.size()));

        if (m_t < 0.4) {
            // Свёрнута — маленькие значки в ряд справа от заголовка (тают при раскрытии)
            const int sz = s(22), gap = s(5);
            int x = width() - s(12) - sz;
            const int minX = s(150);
            p.setOpacity(1.0 - m_t / 0.4);
            for (int i = 0; i < m_items.size() && x >= minX; ++i, x -= sz + gap)
                drawThumb(p, m_items[i], QRect(x, (s(HeadH) - sz) / 2, sz, sz), s(4));
            p.setOpacity(1.0);
            if (m_t <= 0.0) return;
        }

        // Содержимое проявляется вместе с ростом и чуть «въезжает» снизу
        const qreal fade = qBound(0.0, (m_t - 0.25) / 0.75, 1.0);
        p.setOpacity(fade);
        p.translate(0, (1.0 - fade) * s(10));
        p.setFont(icon);
        for (int b = 0; b < 2; ++b) {
            const QRect br = viewBtnRect(b);
            const bool on = (b == 0) == (m_view != QLatin1String("list"));
            if (on) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 30));
                p.drawRoundedRect(br, s(5), s(5));
            }
            p.setPen(QColor(255, 255, 255, on ? 230 : 120));
            p.drawText(br, Qt::AlignCenter, QString(QChar(b == 0 ? 0xE80A : 0xE8FD)));  // Плитки / список
        }

        const QRect body = bodyRect();
        p.setClipRect(body, Qt::IntersectClip);
        if (m_items.isEmpty()) {
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 110));
            p.drawText(body, Qt::AlignCenter, QApplication::translate("FilesStrip", "Ничего не найдено"));
            return;
        }
        for (int i = 0; i < m_items.size(); ++i) {
            const QRect ir = itemRect(i);
            if (!ir.intersects(body)) continue;
            if (i == m_hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 22));
                p.drawRoundedRect(ir, s(8), s(8));
            }
            const Files::Entry &e = m_items[i];
            if (m_view == QLatin1String("list")) {
                const int isz = s(24);
                drawThumb(p, e, QRect(ir.left() + s(6), ir.center().y() - isz / 2 + 1, isz, isz), s(4));
                p.setFont(f);
                const QString meta = Files::sizeText(e.size) + "   " + Files::timeText(e.mtime);
                const int metaW = QFontMetrics(f).horizontalAdvance(meta) + s(10);
                p.setPen(QColor(255, 255, 255, 110));
                p.drawText(ir.adjusted(0, 0, -s(8), 0), Qt::AlignRight | Qt::AlignVCenter, meta);
                p.setPen(QColor(255, 255, 255, 220));
                const QRect nr = ir.adjusted(s(38), 0, -metaW - s(8), 0);
                p.drawText(nr, Qt::AlignLeft | Qt::AlignVCenter,
                           QFontMetrics(f).elidedText(e.name, Qt::ElideMiddle, nr.width()));
            } else {
                const int tsz = s(56);
                drawThumb(p, e, QRect(ir.center().x() - tsz / 2, ir.top() + s(6), tsz, tsz), s(6));
                QFont small = f;
                small.setPixelSize(s(10));
                p.setFont(small);
                p.setPen(QColor(255, 255, 255, 210));
                const QRect nr(ir.left() + s(4), ir.top() + s(66), ir.width() - s(8), s(28));
                p.drawText(nr, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWrapAnywhere,
                           QFontMetrics(small).elidedText(e.name, Qt::ElideMiddle, nr.width() * 2 - s(6)));
            }
        }
    }

private:
    static constexpr int HeadH = 38;    // Высота свёрнутой плашки и заголовка
    static constexpr int MaxBody = 460; // Высота раскрытой части (не выше 60% середины окна)

    int s(int v) const { return v * m_scale / 100; }

    void expand(bool on)
    {
        if (m_items.isEmpty() && on && m_query.isEmpty()) return;
        const qreal target = on ? 1.0 : 0.0;
        if (target == m_target && (m_tick.isActive() || m_t == target)) return;
        m_target = target;
        m_from = m_t;
        m_duration = qMax(1, int(Anim::ms(Anim::Kind::Menu, on ? 340 : 190) * qAbs(target - m_t)));
        if (on) m_hoverPoll.start();
        else m_hover = -1;
        if (!Anim::enabled(Anim::Kind::Menu)) { m_t = target; finish(); return; }
        m_clock.restart();
        m_tick.start();
    }

    void tick()
    {
        const qreal k = qMin(1.0, m_clock.elapsed() / qreal(m_duration));
        // Раскрытие — мягкое торможение в конце, сворачивание — сразу и быстро
        const QEasingCurve curve(m_target > m_from ? QEasingCurve::OutQuart : QEasingCurve::OutCubic);
        m_t = m_from + (m_target - m_from) * curve.valueForProgress(k);
        update();
        if (k >= 1.0) { m_t = m_target; finish(); }
    }

    void finish()
    {
        m_tick.stop();
        if (m_t <= 0.0) m_scroll = 0;
        update();
    }

    void applyFilter()
    {
        m_items.clear();
        for (const Files::Entry &e : m_all)
            if (m_query.isEmpty() || e.name.contains(m_query, Qt::CaseInsensitive)) m_items.append(e);
        m_scroll = qBound(0, m_scroll, maxScroll());
        m_hover = -1;
        requestThumbs();
        relayout();
    }

    // Превью — по одному разу на файл (ключ — путь и время изменения)
    void requestThumbs()
    {
        const QSize px = QSize(s(56), s(56)) * devicePixelRatioF();
        for (const Files::Entry &e : m_items) {
            const QString key = e.path + '|' + QString::number(e.mtime.toMSecsSinceEpoch());
            if (m_thumbs.contains(key) || m_waiting.contains(key)) continue;
            m_waiting.insert(key);
            const QString path = e.path;
            auto thumb = m_thumbnailer;
            QPointer<FilesStrip> self = this;
            Thumbs::runAsync(this, [thumb, path, px]() { return QVariant(thumb ? thumb(path, px) : QImage()); },
                             [self, key](const QVariant &v) {
                if (!self) return;
                self->m_waiting.remove(key);
                QPixmap pm = QPixmap::fromImage(v.value<QImage>());
                pm.setDevicePixelRatio(self->devicePixelRatioF());
                self->m_thumbs.insert(key, pm);
                self->update();
            });
        }
    }

    void drawThumb(QPainter &p, const Files::Entry &e, const QRect &r, int radius) const
    {
        const QPixmap pm = m_thumbs.value(e.path + '|' + QString::number(e.mtime.toMSecsSinceEpoch()));
        if (pm.isNull()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 18));
            p.drawRoundedRect(r, radius, radius);
            return;
        }
        const QSizeF sz = QSizeF(pm.size() / pm.devicePixelRatio()).scaled(QSizeF(r.size()), Qt::KeepAspectRatio);
        const QRectF tr(r.left() + (r.width() - sz.width()) / 2, r.top() + (r.height() - sz.height()) / 2,
                        sz.width(), sz.height());
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(tr, radius, radius);
        p.setClipPath(clip, Qt::IntersectClip);
        p.drawPixmap(tr, pm, QRectF(pm.rect()));
        p.restore();
    }

    // ── Раскладка ────────────────────────────────────────────────────────────
    int fullBodyHeight() const
    {
        if (m_items.isEmpty()) return s(60);
        if (m_view == QLatin1String("list")) return m_items.size() * s(34) + s(8);
        const int cols = qMax(1, (width() - s(16)) / s(92));
        return int((m_items.size() + cols - 1) / cols) * s(100) + s(8);
    }
    int maxBody() const { return qMax(s(120), qMin(s(MaxBody), m_center.height() * 6 / 10)); }
    int bodyHeight() const { return qMin(fullBodyHeight(), maxBody()); }
    int maxScroll() const { return qMax(0, fullBodyHeight() - maxBody()); }
    // Видимая высота сейчас и где начинается видимая часть
    int curH() const { return s(HeadH) + qRound(bodyHeight() * m_t); }
    int top() const { return height() - curH(); }
    // Дальше всё — в координатах видимой части (её верх = 0)
    QRect bodyRect() const { return QRect(0, s(HeadH), width(), bodyHeight()); }
    QPoint local(const QPoint &p) const { return p - QPoint(0, top()); }

    QRect itemRect(int i) const
    {
        const QRect b = bodyRect();
        if (m_view == QLatin1String("list"))
            return QRect(s(8), b.top() + s(2) + i * s(34) - m_scroll, width() - s(16), s(32));
        const int cols = qMax(1, (width() - s(16)) / s(92));
        const int cw = (width() - s(16)) / cols;
        return QRect(s(8) + (i % cols) * cw, b.top() + s(2) + (i / cols) * s(100) - m_scroll, cw - s(4), s(96));
    }

    int itemAt(const QPoint &widgetPos) const
    {
        const QPoint p = local(widgetPos);
        if (m_t < 0.9 || !bodyRect().contains(p)) return -1;
        for (int i = 0; i < m_items.size(); ++i)
            if (itemRect(i).contains(p)) return i;
        return -1;
    }

    QRect viewBtnRect(int b) const
    {
        const int sz = s(26);
        return QRect(width() - s(10) - (2 - b) * (sz + s(4)), (s(HeadH) - sz) / 2, sz, sz);
    }
    int viewBtnAt(const QPoint &widgetPos) const
    {
        const QPoint p = local(widgetPos);
        if (m_t < 0.9) return -1;
        for (int b = 0; b < 2; ++b) if (viewBtnRect(b).contains(p)) return b;
        return -1;
    }

    void relayout()
    {
        const bool show = m_enabled && m_center.width() > s(260) && (!m_all.isEmpty());
        if (!show) { hide(); return; }
        const int w = qMin(m_center.width() - s(40), s(720));
        const int h = s(HeadH) + bodyHeight();  // Размер не меняется во время анимации
        setGeometry(m_center.center().x() - w / 2, m_center.bottom() - s(6) - h, w, h);
        if (!isVisible()) { QWidget::show(); raise(); }
        update();
    }

    void startDrag(const QString &path)
    {
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(path)});
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        const QPixmap pm = [&]() {
            for (const Files::Entry &e : m_items)
                if (e.path == path) return m_thumbs.value(e.path + '|' + QString::number(e.mtime.toMSecsSinceEpoch()));
            return QPixmap();
        }();
        if (!pm.isNull()) drag->setPixmap(pm.scaled(QSize(s(48), s(48)) * pm.devicePixelRatio(),
                                                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_dragging = true;
        const Qt::DropAction act = drag->exec(Qt::CopyAction, Qt::CopyAction);
        m_dragging = false;
        if (act != Qt::IgnoreAction && onDropped) onDropped();
        else if (!underMouse()) m_leaveTimer.start();
    }

    int m_scale = 100;
    std::function<QImage(const QString &, QSize)> m_thumbnailer;
    QList<Files::Entry> m_all;     // Всё найденное
    QList<Files::Entry> m_items;   // После поиска
    QHash<QString, QPixmap> m_thumbs;
    QSet<QString> m_waiting;
    QString m_view = QStringLiteral("tiles");
    QString m_query;
    QRect m_center;
    bool m_enabled = true;
    bool m_dragging = false;
    int m_gen = 0;
    int m_scroll = 0;
    int m_hover = -1;
    int m_press = -1;
    QPoint m_pressPos;
    qreal m_t = 0;                 // 0 — свёрнута, 1 — раскрыта
    qreal m_from = 0, m_target = 0;
    int m_duration = 1;
    QElapsedTimer m_clock;
    QTimer m_tick;
    QTimer m_leaveTimer;
    QTimer m_hoverPoll;
};
