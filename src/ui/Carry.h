#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  Carry.h — «рука»: фото и файлы, которые несёшь, чтобы бросить в программу   ║
// ║                                                                              ║
// ║  Зажал левой фото (в колонке) или файл (в полоске) — он «вытягивается» и     ║
// ║  летит под курсор. Пока левая зажата, правым кликом (или проведя с зажатой   ║
// ║  правой) добавляешь ещё фото и файлы. Вынес за SmartClip — бросаешь всё      ║
// ║  разом в любую программу. Отпустил над SmartClip — всё кладётся обратно.     ║
// ║                                                                              ║
// ║  Колёсико: клик — экран темнеет, и в окошке видно всё, что в руке: можно     ║
// ║  убрать лишнее (минусики) и поменять порядок вставки (перетащить). Зажал     ║
// ║  левую вне окошка — всё снова летит в руку. Колёсико зажато — под курсором   ║
// ║  маленькая полупрозрачная копия окошка: просто посмотреть, что несёшь.       ║
// ║                                                                              ║
// ║  За кнопками мыши следим сами (GetAsyncKeyState): пока кнопка зажата, Qt     ║
// ║  шлёт события тому виджету, где её нажали, а нести можно через всё окно.     ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>
#include <QEasingCurve>
#include <QPointer>
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileInfo>
#include <QSet>
#include <QtMath>
#include <functional>
#include <atomic>
#include <QDir>
#include <windows.h>
#include <ole2.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
// Из shlobj.h (его целиком не подключаем: там своя struct FolderItem)
extern "C" DECLSPEC_IMPORT HRESULT WINAPI SHCreateStdEnumFmtEtc(UINT, const FORMATETC *, IEnumFORMATETC **);

#include "Anim.h"
#include "DragGhost.h"

// Что в руке: файл, его превью и подпись
struct CarryItem {
    QString path;
    QPixmap thumb;
    QString name;
};

namespace CarryUi {

inline bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
// С учётом «левши» (кнопки поменяны местами)
inline bool leftHeld()   { return down(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON); }
inline bool rightHeld()  { return down(GetSystemMetrics(SM_SWAPBUTTON) ? VK_LBUTTON : VK_RBUTTON); }
inline bool middleHeld() { return down(VK_MBUTTON); }

// Картинка целиком внутри area, по центру, без искажения
inline QRectF fit(const QPixmap &pm, const QRectF &area)
{
    if (pm.isNull()) return area;
    const QSizeF sz = QSizeF(pm.size() / pm.devicePixelRatio()).scaled(area.size(), Qt::KeepAspectRatio);
    return QRectF(area.center().x() - sz.width() / 2, area.center().y() - sz.height() / 2, sz.width(), sz.height());
}

// Окно поверх всего; input = false — мышь проходит сквозь него
inline void makeOverlay(QWidget *w, QWidget *ref, bool input)
{
    w->setAttribute(Qt::WA_TranslucentBackground);
    w->setAttribute(Qt::WA_ShowWithoutActivating);
    w->setAttribute(Qt::WA_DeleteOnClose);
    if (!input) {
        w->setAttribute(Qt::WA_TransparentForMouseEvents);
        w->setWindowFlag(Qt::WindowTransparentForInput);
    }
    if (ref && ref->window()->testAttribute(Qt::WA_DontShowOnScreen))
        w->setAttribute(Qt::WA_DontShowOnScreen);  // Проверки — не на экране
}

// Клик по окну не делает его активным: фокус остаётся у программы пользователя
inline void noActivate(QWidget *w)
{
    HWND h = reinterpret_cast<HWND>(w->winId());
    SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
}

inline QRectF scaled(const QRectF &r, qreal k)
{
    QRectF o(0, 0, r.width() * k, r.height() * k);
    o.moveCenter(r.center());
    return o;
}

// Чуть «шевельнуть» мышь (туда и обратно): Windows проверяет, не отменён ли
// перенос, только когда что-то происходит с мышью или клавишами
inline void nudgeMouse()
{
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = in[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    in[0].mi.dx = 1;
    in[1].mi.dx = -1;
    SendInput(2, in, sizeof(INPUT));
}

// Перенос в программу — напрямую через Windows (DoDragDrop), со своим
// «источником»: он сам решает, бросить, продолжать или отменить. Через Qt
// отменить уже начатый перенос (колёсико, Esc) не получалось — файлы всё
// равно вставлялись при отпускании.
class DropSource : public IDropSource
{
public:
    DropSource(const bool *abort, std::function<void()> tick) : m_abort(abort), m_tick(std::move(tick)) {}
    virtual ~DropSource() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDropSource)) {
            *ppv = static_cast<IDropSource *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --m_ref;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override
    {
        if (m_tick) m_tick();
        if (escape || (m_abort && *m_abort)) return DRAGDROP_S_CANCEL;
        // Отпустили — бросаем. Кнопку проверяем и напрямую: пока колёсико
        // «съедено» (Carry::midProc), состояние от Windows могло запаздывать
        if (!(keys & MK_LBUTTON) || !leftHeld()) return DRAGDROP_S_DROP;
        return S_OK;                                      // Колёсико/правая — не бросок
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
    {
        if (m_tick) m_tick();
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    const bool *m_abort = nullptr;
    std::function<void()> m_tick;  // Карточка под курсором (DragGhost::pump)
    ULONG m_ref = 1;
};

// Список файлов для переноса (CF_HDROP — его понимает любая программа).
// Нарочно простой: программа под курсором опрашивает данные при каждом
// движении мыши, и «тяжёлые» данные Проводника давали рывки.
class FilesData : public IDataObject
{
    // То же, что DROPFILES из shlobj.h (его не подключаем, см. выше)
    struct DropFilesHead { DWORD pFiles; POINT pt; BOOL fNC; BOOL fWide; };

public:
    explicit FilesData(const QStringList &paths)
    {
        m_effectFormat = CLIPFORMAT(RegisterClipboardFormatW(L"Preferred DropEffect"));
        for (const QString &p : paths) {
            m_list += QDir::toNativeSeparators(p).toStdWString();
            m_list.push_back(L'\0');
        }
        m_list.push_back(L'\0');  // Список кончается двумя нулями
    }
    virtual ~FilesData() = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataObject)) {
            *ppv = static_cast<IDataObject *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --m_ref;
        if (!r) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *fe, STGMEDIUM *sm) override
    {
        if (!fe || !sm) return E_INVALIDARG;
        if (!(fe->tymed & TYMED_HGLOBAL) || fe->dwAspect != DVASPECT_CONTENT) return DV_E_FORMATETC;
        HGLOBAL g = nullptr;
        if (fe->cfFormat == CF_HDROP) {
            const SIZE_T bytes = sizeof(DropFilesHead) + m_list.size() * sizeof(wchar_t);
            g = GlobalAlloc(GHND, bytes);
            if (!g) return E_OUTOFMEMORY;
            auto *df = static_cast<DropFilesHead *>(GlobalLock(g));
            df->pFiles = sizeof(DropFilesHead);
            df->fWide = TRUE;
            memcpy(reinterpret_cast<char *>(df) + sizeof(DropFilesHead), m_list.data(), m_list.size() * sizeof(wchar_t));
            GlobalUnlock(g);
        } else if (fe->cfFormat == m_effectFormat) {
            g = GlobalAlloc(GHND, sizeof(DWORD));
            if (!g) return E_OUTOFMEMORY;
            *static_cast<DWORD *>(GlobalLock(g)) = DROPEFFECT_COPY;
            GlobalUnlock(g);
        } else return DV_E_FORMATETC;
        sm->tymed = TYMED_HGLOBAL;
        sm->hGlobal = g;
        sm->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *fe) override
    {
        if (!fe) return E_INVALIDARG;
        const bool known = fe->cfFormat == CF_HDROP || fe->cfFormat == m_effectFormat;
        return known && (fe->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out) override
    {
        if (out) out->ptd = nullptr;
        return E_NOTIMPL;
    }
    // Служебное от программ под курсором (подсказки и т.п.) — не храним
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC **out) override
    {
        if (dir != DATADIR_GET) return E_NOTIMPL;
        const FORMATETC f[2] = {{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
                                {m_effectFormat, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}};
        return SHCreateStdEnumFmtEtc(2, f, out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    std::wstring m_list;
    CLIPFORMAT m_effectFormat = 0;
    ULONG m_ref = 1;
};

inline IDataObject *filesDataObject(const QStringList &paths)
{
    return paths.isEmpty() ? nullptr : new FilesData(paths);
}

} // namespace CarryUi

// ─── Полёт ───────────────────────────────────────────────────────────────────
// Фото/файл летит туда, где его держат (из карточки — в руку, из окошка —
// обратно в руку). lift — сначала чуть «приподнимается» на месте, будто его
// вытянули, а потом уменьшается до размера стопки в руке.
class CarryFlyer : public QWidget
{
public:
    CarryFlyer(QWidget *ref, int scalePct, const QPixmap &pm, const QRectF &from,
               std::function<QRectF()> to, bool lift, int delayMs, std::function<void()> landed)
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                               Qt::WindowStaysOnTopHint)
        , m_scale(scalePct), m_pm(pm), m_from(from), m_rect(from), m_to(std::move(to))
        , m_lift(lift), m_delay(delayMs), m_landed(std::move(landed))
    {
        CarryUi::makeOverlay(this, ref, false);
        m_tick.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_tick, &QTimer::timeout, this, [this]() { step(); });
        place();
        show();
        m_clock.start();
        m_tick.setInterval(Anim::frameMs(this));
        m_tick.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF img = CarryUi::fit(m_pm, m_rect.translated(-QPointF(m_origin)));
        if (m_shadow > 0.01) {  // Приподнят над экраном — мягкая тень
            QPainterPath sh;
            sh.addRoundedRect(img.translated(0, s(6) * m_shadow), s(8), s(8));
            p.fillPath(sh, QColor(0, 0, 0, int(90 * m_shadow)));
        }
        QPainterPath clip;
        clip.addRoundedRect(img, s(6), s(6));
        if (m_pm.isNull()) { p.fillPath(clip, QColor(40, 40, 62)); return; }
        p.setClipPath(clip);
        p.drawPixmap(img, m_pm, QRectF(m_pm.rect()));
    }

private:
    int s(int v) const { return v * m_scale / 100; }

    void place()
    {
        const int pad = s(16);
        const QRect r = m_rect.toAlignedRect().adjusted(-pad, -pad, pad, pad + s(6));
        m_origin = r.topLeft();
        setGeometry(r);
    }

    void step()
    {
        const qreal liftMs = m_lift ? 120.0 : 0.0, flyMs = m_lift ? 260.0 : 300.0;
        const qreal t = m_clock.elapsed() - m_delay;
        if (t < 0) return;  // Ждёт своей очереди на месте
        const QRectF lifted = m_lift ? CarryUi::scaled(m_from, 1.07) : m_from;
        if (t < liftMs) {
            const qreal k = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(t / liftMs);
            m_rect = CarryUi::scaled(m_from, 1.0 + 0.07 * k);
            m_shadow = k;
        } else {
            const qreal k = qMin(1.0, (t - liftMs) / flyMs);
            const qreal e = QEasingCurve(QEasingCurve::InOutCubic).valueForProgress(k);
            const QRectF b = m_to ? m_to() : lifted;
            m_rect = QRectF(lifted.topLeft() + (b.topLeft() - lifted.topLeft()) * e,
                            lifted.size() + (b.size() - lifted.size()) * e);
            m_shadow = (m_lift ? 1.0 : 0.6) * (1.0 - 0.7 * e);
            if (k >= 1.0) {
                m_tick.stop();
                if (m_landed) m_landed();
                close();
                return;
            }
        }
        place();
        update();
    }

    int m_scale = 100;
    QPixmap m_pm;
    QRectF m_from, m_rect;
    std::function<QRectF()> m_to;
    bool m_lift = true;
    int m_delay = 0;
    std::function<void()> m_landed;
    qreal m_shadow = 0;
    QPoint m_origin;
    QTimer m_tick;
    QElapsedTimer m_clock;
};

// ─── Плитки «что в руке» ─────────────────────────────────────────────────────
// Общее для окошка (клик колёсиком) и его маленькой копии (колёсико зажато):
// плитки по порядку вставки, с номером и минусиком; покачиваются, как на iOS.
// Координаты — в окне; плитки плавно едут к своему месту.
class CarryBoard
{
public:
    struct Tile {
        CarryItem item;
        QPointF pos;              // Левый верхний угол сейчас
        qreal phase = 0;          // Своё покачивание у каждой
        qreal gone = 0;           // Убирается: 0 → 1
        bool removing = false;
    };

    int scale = 100;
    int maxH = 600;               // Окошко не выше
    int scroll = 0;
    QList<Tile> tiles;
    QPointF origin;               // Левый верхний угол окошка (в окне)
    QSizeF bg;                    // Размер окошка сейчас (плавно уменьшается)

    int s(int v) const { return v * scale / 100; }
    int tileW() const { return s(104); }
    int tileH() const { return s(112); }
    int gap() const { return s(12); }
    int padX() const { return s(20); }
    int headH() const { return s(46); }
    int footH() const { return s(46); }
    int count() const { return int(tiles.size()); }
    int cols() const { return qBound(1, count(), 5); }
    int rows() const { return qMax(1, (count() + cols() - 1) / cols()); }
    int gridH() const { return rows() * tileH() + (rows() - 1) * gap(); }
    int viewH() const { return qMin(gridH(), maxH - headH() - footH()); }
    int maxScroll() const { return qMax(0, gridH() - viewH()); }
    QSize size() const
    {
        return QSize(padX() * 2 + cols() * tileW() + (cols() - 1) * gap(), headH() + viewH() + footH());
    }
    QRectF view() const { return QRectF(origin.x(), origin.y() + headH(), bg.width(), viewH()); }
    QPointF slot(int i) const
    {
        return origin + QPointF(padX() + (i % cols()) * (tileW() + gap()),
                                headH() + (i / cols()) * (tileH() + gap()) - scroll);
    }
    QRectF tileRect(int i) const { return QRectF(tiles[i].pos, QSizeF(tileW(), tileH())); }
    QRectF thumbArea(const QRectF &t) const { return QRectF(t.left() + s(10), t.top() + s(10), t.width() - s(20), s(66)); }
    QRectF badge(const QRectF &t) const { return QRectF(t.right() - s(15), t.top() - s(7), s(22), s(22)); }

    void setItems(const QList<CarryItem> &items)
    {
        tiles.clear();
        for (int i = 0; i < items.size(); ++i) {
            Tile t;
            t.item = items[i];
            t.phase = i * 1.9;
            tiles.append(t);
        }
        bg = size();
    }
    QList<CarryItem> items() const
    {
        QList<CarryItem> out;
        for (const Tile &t : tiles) if (!t.removing) out.append(t.item);
        return out;
    }
    // Окошко по центру окна: center — центр окна
    void layout(const QPointF &center, bool snap)
    {
        origin = center - QPointF(bg.width() / 2, bg.height() / 2);
        scroll = qBound(0, scroll, maxScroll());
        if (snap) for (int i = 0; i < count(); ++i) tiles[i].pos = slot(i);
    }
    // Шаг анимации; true — какая-то плитка убрана совсем
    bool step(qreal dt, const QPointF &center, int drag)
    {
        const qreal k = 1.0 - qExp(-dt / 0.07);
        const QSizeF target = size();
        bg += (target - bg) * k;
        layout(center, false);
        bool removed = false;
        for (int i = count() - 1; i >= 0; --i) {
            Tile &t = tiles[i];
            if (t.removing) {
                t.gone += dt / 0.17;
                if (t.gone >= 1.0) { tiles.removeAt(i); removed = true; }
                continue;
            }
            if (i != drag) t.pos += (slot(i) - t.pos) * k;
        }
        return removed;
    }
    int tileAt(const QPointF &p) const
    {
        if (!view().contains(p)) return -1;
        for (int i = count() - 1; i >= 0; --i)
            if (!tiles[i].removing && tileRect(i).contains(p)) return i;
        return -1;
    }
    int badgeAt(const QPointF &p) const
    {
        for (int i = count() - 1; i >= 0; --i)
            if (!tiles[i].removing && badge(tileRect(i)).adjusted(-s(4), -s(4), s(4), s(4)).contains(p)
                && view().adjusted(0, -s(12), 0, 0).contains(p))
                return i;
        return -1;
    }
    // Тащим плитку: она встаёт на ближайшее место, остальные расступаются
    int reorder(int drag)
    {
        const QPointF c = tileRect(drag).center();
        int best = drag;
        qreal bestD = 1e18;
        for (int i = 0; i < count(); ++i) {
            const QPointF d = slot(i) + QPointF(tileW() / 2.0, tileH() / 2.0) - c;
            const qreal dd = d.x() * d.x() + d.y() * d.y();
            if (dd < bestD) { bestD = dd; best = i; }
        }
        if (best != drag) tiles.move(drag, best);
        return best;
    }

    // time — секунды (покачивание); appear — 0..1 появление окошка
    void paint(QPainter &p, qreal time, int drag, qreal appear, bool showTiles) const
    {
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF box(origin, bg);
        QPainterPath path;
        path.addRoundedRect(box, s(18), s(18));
        p.fillPath(path.translated(0, s(6)), QColor(0, 0, 0, 70));  // Тень
        p.fillPath(path, QColor(22, 22, 36, 248));
        p.setPen(QPen(QColor(255, 255, 255, 40), 1.2));
        p.drawPath(path);

        QFont f = QApplication::font();
        f.setPixelSize(s(15));
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 235));
        const int n = int(items().size());
        p.drawText(QRectF(box.left() + padX(), box.top(), box.width() - padX() * 2, headH()),
                   Qt::AlignLeft | Qt::AlignVCenter, QApplication::translate("Carry", "В руке: %1").arg(n));
        QFont small = QApplication::font();
        small.setPixelSize(s(11));
        p.setFont(small);
        p.setPen(QColor(255, 255, 255, 120));
        const QRectF foot(box.left() + s(12), box.bottom() - footH(), box.width() - s(24), footH());
        p.drawText(foot, Qt::AlignCenter | Qt::TextWordWrap,
                   QApplication::translate("Carry", "Перетащи — порядок вставки  ·  «−» — убрать  ·  "
                                                    "зажми мышь вне окна — снова в руку  ·  Esc — отмена"));
        if (!showTiles) return;

        p.save();
        p.setClipRect(view().adjusted(-padX(), -s(12), padX(), s(4)));
        for (int pass = 0; pass < 2; ++pass)  // Перетаскиваемая — поверх остальных
            for (int i = 0; i < count(); ++i) {
                if ((i == drag) != (pass == 1)) continue;
                const Tile &t = tiles[i];
                // Появление: плитки «выпрыгивают» по очереди
                const qreal a = qBound(0.0, appear * 1.6 - i * 0.06, 1.0);
                const qreal pop = QEasingCurve(QEasingCurve::OutBack).valueForProgress(a);
                qreal k = pop * (t.removing ? 1.0 - 0.6 * t.gone : 1.0) * (i == drag ? 1.08 : 1.0);
                const qreal op = qMin(1.0, a * 2) * (t.removing ? 1.0 - t.gone : 1.0);
                if (op <= 0.01 || k <= 0.01) continue;
                const QRectF r = tileRect(i);
                const qreal angle = i == drag ? 0.0 : 1.4 * qSin(time * 2 * M_PI * 2.3 + t.phase);
                p.save();
                p.setOpacity(op);
                p.translate(r.center());
                p.rotate(angle);
                p.scale(k, k);
                p.translate(-r.center());
                drawTile(p, t, r, i + 1, i == drag);
                p.restore();
            }
        p.restore();
    }

private:
    void drawTile(QPainter &p, const Tile &t, const QRectF &r, int number, bool lifted) const
    {
        QPainterPath path;
        path.addRoundedRect(r, s(12), s(12));
        if (lifted) p.fillPath(path.translated(0, s(8)), QColor(0, 0, 0, 90));
        p.fillPath(path, QColor(34, 34, 54));
        p.setPen(QPen(QColor(255, 255, 255, lifted ? 90 : 32), 1.2));
        p.drawPath(path);

        const QRectF area = thumbArea(r);
        if (!t.item.thumb.isNull()) {
            const QRectF img = CarryUi::fit(t.item.thumb, area);
            QPainterPath clip;
            clip.addRoundedRect(img, s(5), s(5));
            p.save();
            p.setClipPath(clip, Qt::IntersectClip);
            p.drawPixmap(img, t.item.thumb, QRectF(t.item.thumb.rect()));
            p.restore();
        } else {  // Превью нет — расширение файла
            QFont f = QApplication::font();
            f.setPixelSize(s(16));
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 150));
            p.drawText(area, Qt::AlignCenter, QFileInfo(t.item.path).suffix().toUpper().left(5));
        }
        QFont nf = QApplication::font();
        nf.setPixelSize(s(10));
        p.setFont(nf);
        p.setPen(QColor(255, 255, 255, 215));
        const QRectF nr(r.left() + s(6), r.top() + s(82), r.width() - s(12), s(22));
        p.drawText(nr, Qt::AlignCenter, QFontMetrics(nf).elidedText(t.item.name, Qt::ElideMiddle, int(nr.width())));

        // Номер — порядок вставки
        const QRectF num(r.left() + s(6), r.top() + s(6), s(18), s(18));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(110, 170, 255, 230));
        p.drawEllipse(num);
        QFont bf = QApplication::font();
        bf.setPixelSize(s(10));
        bf.setBold(true);
        p.setFont(bf);
        p.setPen(Qt::white);
        p.drawText(num, Qt::AlignCenter, QString::number(number));

        // Минусик — убрать из руки
        const QRectF b = badge(r);
        p.setPen(QPen(QColor(18, 18, 30), 2));
        p.setBrush(QColor(92, 92, 110));
        p.drawEllipse(b);
        p.setPen(QPen(Qt::white, s(2) > 1 ? s(2) : 2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(b.left() + s(6), b.center().y()), QPointF(b.right() - s(6), b.center().y()));
        p.setBrush(Qt::NoBrush);
    }
};

// ─── Окошко «что в руке» (клик колёсиком) ────────────────────────────────────
// Экран темнеет, по месту курсора — окошко с плитками. Минусик — убрать,
// перетащить плитку — поменять порядок. Зажал левую вне окошка — всё летит
// обратно в руку. Esc — всё кладётся на место.
class CarryPanel : public QWidget
{
public:
    std::function<void(const QList<CarryItem> &)> onChanged;
    std::function<void(const QList<CarryItem> &, const QList<QRectF> &)> onRegrab;
    std::function<void()> onCancel;

    CarryPanel(QWidget *ref, int scalePct, const QList<CarryItem> &items, const QPoint &at)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                               Qt::WindowStaysOnTopHint)
    {
        CarryUi::makeOverlay(this, ref, true);
        setMouseTracking(true);
        QScreen *scr = QGuiApplication::screenAt(at);
        if (!scr) scr = QGuiApplication::primaryScreen();
        const QRect ag = scr->availableGeometry();

        // Затемнение — отдельное простое окно: прозрачность всего окна сразу
        // (дёшево, перерисовывать весь экран каждый кадр не нужно)
        m_dim = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                                         Qt::WindowStaysOnTopHint);
        m_dim->setAttribute(Qt::WA_ShowWithoutActivating);
        if (testAttribute(Qt::WA_DontShowOnScreen)) m_dim->setAttribute(Qt::WA_DontShowOnScreen);
        m_dim->setAutoFillBackground(true);
        QPalette pal = m_dim->palette();
        pal.setColor(QPalette::Window, Qt::black);
        m_dim->setPalette(pal);
        m_dim->setGeometry(scr->geometry());
        m_dim->setWindowOpacity(0.0);
        m_dim->installEventFilter(this);
        m_dim->show();
        CarryUi::noActivate(m_dim);

        m_board.scale = scalePct;
        m_board.maxH = int(ag.height() * 0.8);
        m_board.setItems(items);
        const int m = m_board.s(28);
        QRect r(QPoint(0, 0), m_board.size() + QSize(m * 2, m * 2));
        r.moveCenter(at);
        r.moveLeft(qBound(ag.left(), r.left(), qMax(ag.left(), ag.right() - r.width())));
        r.moveTop(qBound(ag.top(), r.top(), qMax(ag.top(), ag.bottom() - r.height())));
        setGeometry(r);
        m_board.layout(QRectF(rect()).center(), true);

        m_tick.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_tick, &QTimer::timeout, this, [this]() { step(); });
        show();
        raise();
        CarryUi::noActivate(this);
        m_escWas = CarryUi::down(VK_ESCAPE);
        m_clock.start();
        m_tick.setInterval(Anim::frameMs(this));
        m_tick.start();
    }

    ~CarryPanel() override
    {
        if (m_dim) m_dim->deleteLater();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const qreal t = m_clock.elapsed() / 1000.0;
        p.setOpacity(m_closing ? 1.0 - m_leave : qMin(1.0, t / 0.12));
        const qreal appear = qMin(1.0, t / 0.35);
        // Окошко чуть «вырастает» из точки курсора
        const qreal k = 0.92 + 0.08 * QEasingCurve(QEasingCurve::OutCubic).valueForProgress(qMin(1.0, t / 0.2));
        const QPointF c = QRectF(rect()).center();
        p.translate(c);
        p.scale(k, k);
        p.translate(-c);
        m_board.paint(p, t, m_drag, appear, !m_flown);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_closing) return;
        const QPointF p = e->position();
        if (e->button() != Qt::LeftButton) return;
        const int b = m_board.badgeAt(p);
        if (b >= 0) { removeTile(b); return; }
        const int i = m_board.tileAt(p);
        if (i >= 0) {
            m_drag = i;
            m_grab = p - m_board.tiles[i].pos;
            return;
        }
        if (!QRectF(m_board.origin, m_board.bg).contains(p)) regrab();  // Мимо окошка
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPointF p = e->position();
        if (m_drag >= 0 && m_drag < m_board.count()) {
            m_board.tiles[m_drag].pos = p - m_grab;
            m_drag = m_board.reorder(m_drag);
            return;
        }
        const bool hand = m_board.badgeAt(p) >= 0 || m_board.tileAt(p) >= 0;
        setCursor(hand ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || m_drag < 0) return;
        m_drag = -1;
        if (onChanged) onChanged(m_board.items());  // Новый порядок
    }

    void wheelEvent(QWheelEvent *e) override
    {
        m_board.scroll = qBound(0, m_board.scroll - e->angleDelta().y() / 2, m_board.maxScroll());
    }

    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_dim && e->type() == QEvent::MouseButtonPress
            && static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton) {
            regrab();
            return true;
        }
        return QWidget::eventFilter(o, e);
    }

private:
    void removeTile(int i)
    {
        m_board.tiles[i].removing = true;
        const QList<CarryItem> left = m_board.items();
        if (left.isEmpty()) { cancel(); return; }
        if (onChanged) onChanged(left);
    }

    // Зажал левую вне окошка: окошко уходит, а всё из него летит в руку
    void regrab()
    {
        if (m_closing) return;
        m_drag = -1;
        QList<QRectF> rects;
        const QPointF off = geometry().topLeft();
        for (int i = 0; i < m_board.count(); ++i)
            if (!m_board.tiles[i].removing)
                rects << CarryUi::fit(m_board.tiles[i].item.thumb, m_board.thumbArea(m_board.tileRect(i))).translated(off);
        m_flown = true;  // Плитки теперь летят отдельно
        close_(true);
        if (onRegrab) onRegrab(m_board.items(), rects);
    }

    void cancel()
    {
        if (m_closing) return;
        close_(false);
        if (onCancel) onCancel();
    }

    void close_(bool)
    {
        m_closing = true;
        m_leaveClock.start();
    }

    void step()
    {
        const qint64 now = m_clock.elapsed();
        const qreal dt = qBound(0.001, (now - m_last) / 1000.0, 0.05);
        m_last = now;
        if (m_board.step(dt, QRectF(rect()).center(), m_drag)) {
            if (m_drag >= m_board.count()) m_drag = -1;
        }
        // Затемнение: плавно темнеет и так же светлеет
        if (m_dim) {
            const qreal in = qMin(1.0, now / 180.0);
            m_dim->setWindowOpacity(0.5 * (m_closing ? (1.0 - m_leave) * in : in));
        }
        // Esc — всё кладётся на место
        const bool esc = CarryUi::down(VK_ESCAPE);
        if (esc && !m_escWas) cancel();
        m_escWas = esc;
        if (m_closing) {
            m_leave = qMin(1.0, m_leaveClock.elapsed() / 170.0);
            if (m_leave >= 1.0) {
                m_tick.stop();
                if (m_dim) { m_dim->close(); m_dim->deleteLater(); m_dim = nullptr; }
                close();
                return;
            }
        }
        update();
    }

    CarryBoard m_board;
    QPointer<QWidget> m_dim;
    QTimer m_tick;
    QElapsedTimer m_clock, m_leaveClock;
    qint64 m_last = 0;
    int m_drag = -1;
    QPointF m_grab;
    bool m_closing = false, m_flown = false, m_escWas = false;
    qreal m_leave = 0;
};

// ─── Маленькая копия окошка (колёсико зажато) ────────────────────────────────
// Полупрозрачная, под курсором, двигается ровно за мышью. Плавно появляется
// и так же плавно исчезает, когда колёсико отпустили.
class CarryPeek : public QWidget
{
public:
    CarryPeek(QWidget *ref, int scalePct, const QList<CarryItem> &items)
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                               Qt::WindowStaysOnTopHint)
        , m_scale(scalePct)
    {
        CarryUi::makeOverlay(this, ref, false);
        m_board.scale = qMax(30, scalePct * 60 / 100);
        m_board.maxH = m_board.s(700);
        m_board.setItems(items);
        const int m = m_board.s(20);
        resize(m_board.size() + QSize(m * 2, m * 2));
        m_board.layout(QRectF(rect()).center(), true);
        m_tick.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_tick, &QTimer::timeout, this, [this]() { step(); });
        place();
        show();
        m_clock.start();
        m_tick.setInterval(Anim::frameMs(this));
        m_tick.start();
    }

    void setItems(const QList<CarryItem> &items)
    {
        m_board.setItems(items);
        m_board.layout(QRectF(rect()).center(), true);
    }

    // Во время переноса Windows таймер почти не срабатывает — двигают отсюда
    void pump()
    {
        if (m_clock.elapsed() - m_last < Anim::frameMs(this) - 1) return;
        step();
        if (isVisible()) repaint();
    }

    // Колёсико отпустили — тает
    void leave()
    {
        if (m_leaving) return;
        m_leaving = true;
        m_leaveAt = m_clock.elapsed();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setOpacity(0.82 * m_op);
        m_board.paint(p, m_clock.elapsed() / 1000.0, -1, 1.0, true);
    }

private:
    int s(int v) const { return v * m_scale / 100; }

    // Под стопкой в руке; не помещается снизу — над курсором
    void place()
    {
        const QPoint c = QCursor::pos();
        QScreen *scr = QGuiApplication::screenAt(c);
        const QRect ag = scr ? scr->availableGeometry() : QRect(c - QPoint(2000, 2000), QSize(4000, 4000));
        int y = c.y() + s(116);
        if (y + height() > ag.bottom()) y = c.y() - s(16) - height();
        const int x = qBound(ag.left(), c.x() - width() / 2, qMax(ag.left(), ag.right() - width()));
        move(x, qMax(ag.top(), y));
    }

    void step()
    {
        const qint64 now = m_clock.elapsed();
        const qreal dt = qBound(0.001, (now - m_last) / 1000.0, 0.05);
        m_last = now;
        m_op = m_leaving ? qMax(0.0, 1.0 - (now - m_leaveAt) / 150.0) * m_opAtLeave() : qMin(1.0, now / 150.0);
        if (m_leaving && m_op <= 0.0) { m_tick.stop(); close(); return; }
        m_board.step(dt, QRectF(rect()).center(), -1);
        place();
        update();
    }
    qreal m_opAtLeave() const { return qMin(1.0, m_leaveAt / 150.0); }

    int m_scale = 100;
    CarryBoard m_board;
    QTimer m_tick;
    QElapsedTimer m_clock;
    qint64 m_last = 0, m_leaveAt = 0;
    qreal m_op = 0;
    bool m_leaving = false;
};

// ─── Рука ────────────────────────────────────────────────────────────────────
// Сама «рука»: что несём, полёты, стопка под курсором, перетаскивание в
// программу, окошко и его копия. Где SmartClip и что под курсором — говорит
// окно через колбэки.
class Carry : public QObject
{
public:
    // Курсор над SmartClip (там можно добирать ещё); вне — бросаем в программу
    std::function<bool(const QPoint &)> inZone;
    // Что под курсором можно взять (фото в колонке, файл в полоске) и откуда оно летит
    std::function<bool(const QPoint &, CarryItem &, QRectF &)> itemAt;
    // Отпустили над SmartClip: true — окно само разобралось (например, вкладка закрепов)
    std::function<bool(const QPoint &, const QList<CarryItem> &)> dropInside;
    std::function<void()> onStart, onChanged, onEnd, onDragStarted;
    std::function<void(bool)> onDragFinished;
    std::function<void(const QPoint &)> onHover;  // Рука над SmartClip — где (подсветить, куда бросишь)

    Carry(QWidget *source, int scalePct) : QObject(source), m_source(source), m_scale(scalePct)
    {
        m_poll.setTimerType(Qt::PreciseTimer);
        m_poll.setInterval(15);
        QObject::connect(&m_poll, &QTimer::timeout, this, [this]() { poll(); });
    }

    ~Carry() override { hookMiddle(false); }

    void setScale(int pct) { m_scale = pct; }
    bool active() const { return m_state != Idle; }
    bool holding() const { return m_state == Holding; }
    bool has(const QString &path) const
    {
        for (const CarryItem &it : m_items) if (it.path == path) return true;
        return false;
    }
    const QList<CarryItem> &items() const { return m_items; }

    // Взяли первое: оно летит из карточки под курсор
    void pickUp(const CarryItem &it, const QRectF &from)
    {
        if (m_state != Idle) { toggle(it, from); return; }
        m_state = Holding;
        m_items = {it};
        m_rightDown = CarryUi::rightHeld();
        m_midDown = false;
        m_seenDowns = s_downs;
        m_midPeek = false;
        qApp->installEventFilter(this);
        hookMiddle(true);
        setHandCursor(true);
        fly(it, from, true, 0);
        m_poll.start();
        if (onStart) onStart();
        if (onChanged) onChanged();
    }

    // Правый клик по фото/файлу: добавить в руку (ещё раз — убрать)
    void toggle(const CarryItem &it, const QRectF &from)
    {
        for (int i = 0; i < m_items.size(); ++i)
            if (m_items[i].path == it.path) {
                if (m_items.size() > 1) {  // Последнее не убираем — рука не пустеет
                    m_items.removeAt(i);
                    m_inFlight.remove(it.path);
                    syncGhost();
                    if (onChanged) onChanged();
                }
                return;
            }
        m_items.append(it);
        fly(it, from, true, 0);
        if (onChanged) onChanged();
    }

    // Всё кладётся на место
    void cancel()
    {
        // Идёт перенос Windows — сначала отменяем его, иначе при отпускании
        // всё равно вставится (закончит startDrag)
        if (m_state == Dragging) { m_cancelAfter = true; abortDrag(); return; }
        if (m_state != Idle) end(false);
    }

protected:
    // Пока несём: правая и колёсико — наши (добавить, окошко), отпускание левой
    // решаем сами (см. poll) — карточки и полоска их не получают. Только у
    // виджетов: окно Qt должно увидеть отпускание, иначе «залипнет» нажатие.
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if ((m_state != Holding && m_state != Dragging) || !o->isWidgetType()) return false;
        switch (e->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            return static_cast<QMouseEvent *>(e)->button() != Qt::LeftButton;
        case QEvent::MouseButtonRelease:
        case QEvent::ContextMenu:
            return true;
        default:
            return false;
        }
    }

private:
    enum State { Idle, Holding, Dragging, Panel };

    int s(int v) const { return v * m_scale / 100; }

    // Где в руке превью первой карточки стопки (см. DragGhost::drawCard)
    QRectF handRect() const
    {
        const QPointF c = QCursor::pos();
        return QRectF(c.x() - s(30), c.y() + s(16), s(60), s(58));
    }

    void fly(const CarryItem &it, const QRectF &from, bool lift, int delay)
    {
        m_inFlight.insert(it.path);
        const QString path = it.path;
        QPointer<Carry> self = this;
        new CarryFlyer(m_source, m_scale, it.thumb, from, [this]() { return handRect(); }, lift, delay,
                       [self, path]() { if (self) self->landed(path); });
    }

    void landed(const QString &path)
    {
        m_inFlight.remove(path);
        if (m_state != Holding && m_state != Dragging) return;
        if (!m_ghost) {
            m_ghost = new DragGhost(m_source, m_scale);
            syncGhost();
            m_ghost->start();
        } else syncGhost();
    }

    // Стопка под курсором — то, что уже долетело
    void syncGhost()
    {
        if (!m_ghost) return;
        QList<DragGhost::Item> g;
        for (const CarryItem &it : m_items)
            if (!m_inFlight.contains(it.path)) g.append({it.thumb, it.name});
        m_ghost->setItems(g);
    }

    void setHandCursor(bool on)
    {
        if (on == m_cursor) return;
        m_cursor = on;
        if (on) QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        else QApplication::restoreOverrideCursor();
    }

    void poll()
    {
        if (m_state == Idle) { m_poll.stop(); return; }
        const QPoint g = QCursor::pos();
        if (checkMiddle()) return;

        if (m_state != Holding) return;  // Тащим в Windows — она сама следит за кнопкой
        if (!CarryUi::leftHeld()) { release(g); return; }

        // Правая: клик — добавить/убрать, зажата и ведёшь — так же со всем подряд
        if (CarryUi::rightHeld()) {
            CarryItem it;
            QRectF from;
            const bool over = itemAt && itemAt(g, it, from);
            if (!m_rightDown) {
                m_rightDown = true;
                m_sweepAdd = !over || !has(it.path);
                m_lastSweep.clear();
            }
            if (over && it.path != m_lastSweep) {
                m_lastSweep = it.path;
                if (has(it.path) != m_sweepAdd) toggle(it, from);
            }
        } else m_rightDown = false;

        if (onHover) onHover(g);
        if (inZone && !inZone(g)) startDrag();
        else squeeze(false);  // Вернул руку из окошка к SmartClip — он разворачивается
    }

    // Пока идёт перенос Windows, poll() не вызывается (перенос крутится внутри
    // него) — сюда зовёт «источник» переноса на каждое движение/нажатие
    void dragTick()
    {
        if (m_ghost) m_ghost->pump();
        if (m_peek) m_peek->pump();
        checkMiddle();
    }

    // true — открыли окошко или отменяем перенос ради него
    bool checkMiddle()
    {
        // Колёсико: короткий клик — окошко, зажато — маленькая копия под курсором
        // Нажатия считает хук (см. hookMiddle): быстрый клик целиком мог
        // проскочить между проверками, и окошко не открывалось
        const bool mid = s_mid;
        const int downs = s_downs;
        const bool pressed = downs != m_seenDowns;
        m_seenDowns = downs;
        if (m_state == Holding || m_state == Dragging) {
            if (pressed && !m_midDown) {
                m_midDown = true;
                m_midPeek = false;
                m_midClock.start();
            } else if (mid && m_midDown && !m_midPeek && m_midClock.elapsed() > 230) {
                m_midPeek = true;
                if (m_peek) m_peek->leave();
                m_peek = new CarryPeek(m_source, m_scale, m_items);
            } else if (!mid && m_midDown) {
                m_midDown = false;
                if (m_midPeek) {
                    if (m_peek) m_peek->leave();
                    m_peek = nullptr;
                } else if (m_state == Dragging) {
                    if (!m_wantPanel) {
                        m_wantPanel = true;   // Перетаскивание Windows сначала отменяем
                        abortDrag();
                    }
                    return true;
                } else {
                    openPanel();
                    return true;
                }
            }
        } else m_midDown = false;
        return false;
    }

    void release(const QPoint &g)
    {
        const bool handled = dropInside && dropInside(g, m_items);
        end(handled);
    }

    // Вынес за SmartClip — настоящее перетаскивание всего, что в руке
    void startDrag()
    {
        if (!CarryUi::leftHeld()) { release(QCursor::pos()); return; }  // Без кнопки Windows бросит сразу
        m_state = Dragging;
        m_abort = false;
        m_cancelAfter = false;
        setHandCursor(false);
        QStringList paths;
        for (const CarryItem &it : m_items) paths << it.path;
        squeeze(true);  // SmartClip прижимается к краям экрана
        bool dropped = false;
        if (IDataObject *obj = CarryUi::filesDataObject(paths)) {
            auto *src = new CarryUi::DropSource(&m_abort, [this]() { dragTick(); });
            DWORD effect = DROPEFFECT_NONE;
            const HRESULT hr = DoDragDrop(obj, src, DROPEFFECT_COPY, &effect);
            dropped = hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE;
            src->Release();
            obj->Release();
        } else {
            // Запасной путь — через Qt
            auto *mime = new QMimeData;
            QList<QUrl> urls;
            for (const QString &p : paths) urls << QUrl::fromLocalFile(p);
            mime->setUrls(urls);
            auto *drag = new QDrag(m_source);
            drag->setMimeData(mime);
            QPixmap none(1, 1);  // Картинку под курсором рисует «живая» стопка (DragGhost)
            none.fill(Qt::transparent);
            drag->setPixmap(none);
            dropped = drag->exec(Qt::CopyAction, Qt::CopyAction) != Qt::IgnoreAction;
        }
        if (m_state != Dragging) return;
        if (m_wantPanel) {  // Кликнули колёсиком во время перетаскивания
            m_wantPanel = false;
            openPanel();  // SmartClip так и остаётся прижатым — рука ещё не над ним
            return;
        }
        if (m_cancelAfter) dropped = false;  // Esc — ничего не вставляем
        end(dropped);
    }

    // SmartClip прижат к краям, пока рука вне его (перенос, окошко по колёсику)
    void squeeze(bool on, bool dropped = false)
    {
        if (m_squeezed == on) return;
        m_squeezed = on;
        if (on) { if (onDragStarted) onDragStarted(); }
        else if (onDragFinished) onDragFinished(dropped);
    }

    // Отменить уже идущий перенос Windows (см. CarryUi::DropSource)
    void abortDrag()
    {
        m_abort = true;
        QDrag::cancel();         // Если шёл запасной путь через Qt
        CarryUi::nudgeMouse();
    }

    void openPanel()
    {
        m_state = Panel;
        setHandCursor(false);
        if (m_peek) { m_peek->leave(); m_peek = nullptr; }
        if (m_ghost) { m_ghost->finish(false); m_ghost = nullptr; }
        m_inFlight.clear();
        m_panel = new CarryPanel(m_source, m_scale, m_items, QCursor::pos());
        m_panel->onChanged = [this](const QList<CarryItem> &items) {
            m_items = items;
            if (onChanged) onChanged();
        };
        m_panel->onCancel = [this]() { m_panel = nullptr; end(false); };
        m_panel->onRegrab = [this](const QList<CarryItem> &items, const QList<QRectF> &rects) {
            m_panel = nullptr;
            m_items = items;
            m_state = Holding;
            m_rightDown = CarryUi::rightHeld();
            m_midDown = false;
            m_seenDowns = s_downs;
            setHandCursor(true);
            for (int i = 0; i < m_items.size() && i < rects.size(); ++i)
                fly(m_items[i], rects[i], false, i * 35);  // По очереди, в порядке вставки
            if (onChanged) onChanged();
        };
    }

    void end(bool dropped)
    {
        m_poll.stop();
        m_state = Idle;
        squeeze(false, dropped);
        qApp->removeEventFilter(this);
        hookMiddle(false);
        setHandCursor(false);
        if (m_ghost) { m_ghost->finish(dropped); m_ghost = nullptr; }
        if (m_peek) { m_peek->leave(); m_peek = nullptr; }
        if (m_panel) { CarryPanel *p = m_panel; m_panel = nullptr; p->onCancel = nullptr; p->close(); }
        m_items.clear();
        m_inFlight.clear();
        m_wantPanel = false;
        if (onEnd) onEnd();
        if (onChanged) onChanged();
    }

    QWidget *m_source = nullptr;
    int m_scale = 100;
    State m_state = Idle;
    QList<CarryItem> m_items;          // По порядку вставки
    QSet<QString> m_inFlight;          // Ещё летят в руку
    QPointer<DragGhost> m_ghost;
    QPointer<CarryPeek> m_peek;
    QPointer<CarryPanel> m_panel;
    QTimer m_poll;
    QElapsedTimer m_midClock;
    bool m_midDown = false, m_midPeek = false, m_wantPanel = false;
    int m_seenDowns = 0;
    bool m_abort = false, m_cancelAfter = false, m_squeezed = false;
    bool m_rightDown = false, m_sweepAdd = true;
    QString m_lastSweep;
    bool m_cursor = false;

    // Колёсико, пока несём, — только наше. Во время перетаскивания Windows
    // любую новую кнопку считает «бросить»: клик колёсиком над рабочим столом
    // вставлял файлы, а не открывал окошко. Поэтому нажатия колёсика ловим
    // раньше всех (низкоуровневый хук) и до Windows не пропускаем.
    // Хук — в своём потоке со своим циклом сообщений: поток интерфейса во
    // время перетаскивания занят Windows, и хук в нём «вешал» мышь и SmartClip.
    void hookMiddle(bool on)
    {
        // Окно не на экране (проверки) — мышь пользователя не трогаем
        if (on && m_source && m_source->window()->testAttribute(Qt::WA_DontShowOnScreen)) return;
        s_eat = on;
        if (!on) s_mid = false;
        if (on && !s_thread) {
            HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            s_thread = CreateThread(nullptr, 0, &Carry::hookThread, ready, 0, &s_threadId);
            if (s_thread) {
                SetThreadPriority(s_thread, THREAD_PRIORITY_HIGHEST);
                WaitForSingleObject(ready, 1000);
            }
            CloseHandle(ready);
        } else if (!on && s_thread) {
            PostThreadMessageW(s_threadId, WM_QUIT, 0, 0);
            WaitForSingleObject(s_thread, 1000);
            CloseHandle(s_thread);
            s_thread = nullptr;
            s_threadId = 0;
        }
    }
    static DWORD WINAPI hookThread(LPVOID ready)
    {
        MSG msg;
        PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // Очередь — до сигнала «готов»
        HHOOK h = SetWindowsHookExW(WH_MOUSE_LL, &Carry::midProc, GetModuleHandleW(nullptr), 0);
        SetEvent(static_cast<HANDLE>(ready));
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_APP + 1) { CarryUi::nudgeMouse(); continue; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (h) UnhookWindowsHookEx(h);
        return 0;
    }
    static LRESULT CALLBACK midProc(int code, WPARAM wp, LPARAM lp)
    {
        // Отпустили кнопку — тоже «будим» перенос, чтобы он сразу закончился
        if (code == HC_ACTION && s_eat && (wp == WM_LBUTTONUP || wp == WM_RBUTTONUP))
            PostThreadMessageW(GetCurrentThreadId(), WM_APP + 1, 0, 0);
        if (code == HC_ACTION && s_eat && (wp == WM_MBUTTONDOWN || wp == WM_MBUTTONUP)) {
            s_mid = wp == WM_MBUTTONDOWN;
            if (wp == WM_MBUTTONDOWN) ++s_downs;
            // Идёт перенос Windows: он заглядывает к нам только на движение
            // мыши — «шевелим» её (уже вне хука, в его же потоке)
            PostThreadMessageW(GetCurrentThreadId(), WM_APP + 1, 0, 0);
            return 1;
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }
    inline static std::atomic<bool> s_eat{false};   // Сейчас колёсико наше
    inline static std::atomic<bool> s_mid{false};   // Колёсико зажато
    inline static std::atomic<int> s_downs{0};      // Сколько раз нажато
    inline static HANDLE s_thread = nullptr;
    inline static DWORD s_threadId = 0;
};
