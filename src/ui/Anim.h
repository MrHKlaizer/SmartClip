// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  Anim.h — Анимации SmartClip 2.0                                             ║
// ║                                                                              ║
// ║  Почему не просто прозрачность: сама Windows показывает меню и окна        ║
// ║  проявлением, поэтому «fade» выглядит как стандартный. Свой стиль — это    ║
// ║  ДВИЖЕНИЕ ФОРМЫ: меню разворачивается шторкой, окна выпрыгивают зумом,      ║
// ║  панели главного окна съезжаются с краёв экрана.                            ║
// ║                                                                              ║
// ║  Qt Widgets не умеет масштабировать окна, поэтому используем приём, которым ║
// ║  пользуется сам Qt (QRollEffect): делаем СНИМОК окна, прячем настоящее      ║
// ║  (прозрачность 0), и поверх на прозрачной «сцене» анимируем снимок как     ║
// ║  угодно. В конце настоящее окно появляется ровно на месте снимка.           ║
// ║                                                                              ║
// ║  Анимации выключаются, если выбрана «Классика» (кнопка 🖌) или в Windows   ║
// ║  выключено «Эффекты анимации».                                              ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

#include <QWidget>
#include <QApplication>
#include <QLayout>
#include <QCoreApplication>
#include <QPainter>
#include <QPointer>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QList>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <QtMath>
#include <QScreen>
#include <windows.h>
#include "core/AppSettings.h"

namespace Anim {

// Разрешает ли Windows анимации интерфейса
inline bool systemAllows()
{
    BOOL on = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0))
        return on != FALSE;
    return true;
}

// ─── Виды анимаций ───────────────────────────────────────────────────────────
// У каждого вида — свой стиль («Новый» / «Классика») и своя скорость
// (окно «Оформление», кнопка 🖌). Новый вид = новая строка в Kind и в kinds().
enum class Kind { Menu, Dialog, WindowOpen, WindowClose, Scroll, Tabs, Viewer };

struct KindInfo {
    Kind        kind;
    const char *key;     // Ключ в настройках: anim/<key>/on, anim/<key>/speed
    const char *title;   // Название в окне «Оформление» (переводится через tr)
    const char *hint;    // Пояснение под названием
};

inline const QList<KindInfo> &kinds()
{
    static const QList<KindInfo> list = {
        { Kind::Menu,        "menu",   QT_TRANSLATE_NOOP("MainWindow", "Меню"),
          QT_TRANSLATE_NOOP("MainWindow", "Разворачиваются шторкой сверху вниз") },
        { Kind::Dialog,      "dialog", QT_TRANSLATE_NOOP("MainWindow", "Окна и диалоги"),
          QT_TRANSLATE_NOOP("MainWindow", "Выпрыгивают из центра с пружинкой") },
        { Kind::WindowOpen,  "open",   QT_TRANSLATE_NOOP("MainWindow", "Открытие SmartClip"),
          QT_TRANSLATE_NOOP("MainWindow", "Панели съезжаются с краёв, карточки — лесенкой") },
        { Kind::WindowClose, "close",  QT_TRANSLATE_NOOP("MainWindow", "Закрытие SmartClip"),
          QT_TRANSLATE_NOOP("MainWindow", "Карточки разъезжаются обратной лесенкой") },
        { Kind::Scroll,      "scroll", QT_TRANSLATE_NOOP("MainWindow", "Прокрутка"),
          QT_TRANSLATE_NOOP("MainWindow", "«Дуга» — крайние карточки уходят по дуге, под мышью выдвигаются; «Плавная» — без этого") },
        { Kind::Tabs,        "tabs",   QT_TRANSLATE_NOOP("MainWindow", "Вкладки и разделы"),
          QT_TRANSLATE_NOOP("MainWindow", "Выделение переезжает, раздел выезжает с проявлением") },
        { Kind::Viewer,      "viewer", QT_TRANSLATE_NOOP("MainWindow", "Просмотр картинок"),
          QT_TRANSLATE_NOOP("MainWindow", "Картинка вырастает из карточки, при закрытии — обратно") },
    };
    return list;
}

inline QString key(Kind k)
{
    for (const KindInfo &i : kinds())
        if (i.kind == k) return QString::fromLatin1(i.key);
    return QString();
}

// Включён ли новый стиль для этого вида (и разрешает ли анимации Windows)
inline bool enabled(Kind k)
{
    return systemAllows() && AppSettings::get().animOn(key(k));
}

// Стиль прокрутки: "arc" (дуга) или "smooth" (плавная без дуги)
inline QString scrollStyle()
{
    return AppSettings::get().scrollStyle();
}

// Длительность с учётом скорости этого вида: base — длительность при 100%
inline int ms(Kind k, int base)
{
    return base * 100 / AppSettings::get().animSpeed(key(k));
}

// ─── Кусок снимка, который анимируется отдельно ──────────────────────────────
// Окно целиком — один кусок (меню, диалог). Главное окно — несколько кусков
// (верхняя панель, колонки, нижняя панель), каждый со своей траекторией.
struct Piece {
    QRect   src;               // Какую часть снимка рисуем (координаты окна)
    QPointF fromOffset;        // Откуда выезжает (смещение в начале, px)
    qreal   fromScaleX = 1.0;  // Масштаб в начале по горизонтали
    qreal   fromScaleY = 1.0;  // ...и по вертикали
    QPointF anchor;            // Точка, вокруг которой масштабируем (координаты окна)
    qreal   delay = 0.0;       // Задержка старта: доля общей длительности (0..0.5)
};

// ─── «Сцена» поверх экрана, на которой играет анимация снимка ───────────────
class SnapshotOverlay : public QWidget
{
public:
    // reverse = true — анимация ухода: снимок проигрывается задом наперёд
    // (от собранного окна к разлетевшимся кускам), а настоящее окно уже скрыто.
    // keepAlive = true — после появления сцена не закрывается, а становится
    // пустой и прозрачной (см. isIdle/replayOut): так уход начнётся мгновенно,
    // без ~130мс на создание нового окна.
    SnapshotOverlay(QWidget *target, const QList<Piece> &pieces, int durationMs,
                    qreal overshoot, bool reverse = false, bool keepAlive = false)
        : QWidget(nullptr,
                  Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                  Qt::WindowStaysOnTopHint)
        , m_target(target)
        , m_pieces(pieces)
        , m_overshoot(overshoot)
        , m_reverse(reverse)
        , m_keepAlive(keepAlive)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);  // Клики идут в настоящее окно
        setAttribute(Qt::WA_ShowWithoutActivating);      // Не отбираем фокус
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlag(Qt::WindowTransparentForInput);  // Даже пустая сцена не мешает кликам

        m_snapshot = target->grab();
        placeOver(target);

        // Окно спрятали или удалили, пока сцена ждала ухода (пустая) — убираем
        // её, чтобы прозрачное окно не висело поверх экрана без дела
        target->installEventFilter(this);
        QObject::connect(target, &QObject::destroyed, this, [this]() { if (m_idle) close(); });

        m_anim.setDuration(durationMs);
        m_anim.setStartValue(reverse ? 1.0 : 0.0);
        m_anim.setEndValue(reverse ? 0.0 : 1.0);
        QObject::connect(&m_anim, &QVariantAnimation::valueChanged, this, [this]() {
            // Окно закрыли раньше, чем доиграла анимация появления — уходим
            if (!m_reverse && (!m_target || !m_target->isVisible())) {
                // Меню закрыли, пока оно ещё разворачивалось — сворачиваем обратно
                if (m_outOnHideMs > 0) { reverseNow(); return; }
                finish();
                return;
            }
            update();
        });
        QObject::connect(&m_anim, &QVariantAnimation::finished, this, [this]() { finish(); });
    }

    ~SnapshotOverlay() override
    {
        // Страховка: после появления настоящее окно не должно остаться
        // невидимым. После УХОДА — наоборот, не трогаем: окно ещё прозрачное
        // и вот-вот спрячется (иначе оно мелькало целиком в конце ухода).
        if (m_target && !m_reverse) m_target->setWindowOpacity(1.0);
    }

    // Анимация ухода (или разворот появления, см. reverseNow)?
    bool isReverse() const { return m_reverse; }
    bool isPlaying() const { return !m_done; }
    bool isIdle() const { return m_idle; }

    // Уход сразу, как только окно спрячут (для меню: Qt прячет меню сам,
    // перехватить закрытие заранее нельзя). ms — длительность ухода.
    void setOutOnHide(int ms) { m_outOnHideMs = ms; }

    // Уход на уже показанной (пустой) сцене: новый снимок окна, куски задом
    // наперёд. onFirstFrame — когда кадр нарисован (тогда можно прятать окно).
    void replayOut(const QList<Piece> &pieces, int durationMs, std::function<void()> onFirstFrame)
    {
        if (!m_target) { if (onFirstFrame) onFirstFrame(); return; }
        m_snapshot = m_target->grab();
        placeOver(m_target);  // Окно могли перетащить, пока оно было открыто
        m_pieces   = pieces;
        m_reverse  = true;
        m_done     = false;
        m_idle     = false;
        m_onFirstFrame = std::move(onFirstFrame);
        m_anim.stop();
        m_anim.setStartValue(1.0);
        m_anim.setEndValue(0.0);
        m_anim.setDuration(durationMs);
        m_anim.start();
        repaint();  // Сцена уже на экране — кадр выводится сразу
    }

    // Окно закрыли, пока ещё играло появление: разворачиваемся с текущего
    // места и уезжаем обратно (раньше анимация просто обрывалась и окно
    // резко пропадало). Время — пропорционально пройденному пути, в 2 раза быстрее.
    void reverseNow()
    {
        if (m_reverse || m_done) return;
        m_reverse = true;
        const qreal cur  = m_anim.currentValue().toReal();
        const int   full = m_anim.duration();
        m_anim.stop();
        m_anim.setStartValue(cur);
        m_anim.setEndValue(0.0);
        m_anim.setDuration(qMax(60, int(full * cur / 2)));
        m_anim.start();
    }

    // Вызвать, когда сцена реально нарисовала первый кадр на экране.
    // Нужно для ухода: прятать настоящее окно можно только ПОСЛЕ этого —
    // иначе между ними виден пустой кадр («моргание»). Время угадывать нельзя:
    // новое окно рисуется, только когда Windows его действительно показала.
    void setOnFirstFrame(std::function<void()> fn) { m_onFirstFrame = std::move(fn); }

    // Вызвать, когда анимация ухода доиграла до конца (не при abort)
    void setOnFinished(std::function<void()> fn) { m_onFinished = std::move(fn); }

    // Немедленно убрать сцену (например, окно снова открывают во время ухода)
    void abort() { abortNow(); }

    // Показать сцену. При уходе первый кадр рисуется сразу (собранное окно),
    // чтобы между скрытием настоящего окна и анимацией не было «дырки».
    void start()
    {
        show();
        raise();  // Поверх всего — в том числе поверх главного окна SmartClip
        m_anim.start();
    }

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == m_target && e->type() == QEvent::Hide && m_idle) {
            if (m_outOnHideMs > 0 && !m_snapshot.isNull()) {
                // Меню: само окно Qt прячет мгновенно (по клику на пункт или
                // мимо), а сцена уже на экране — сразу проигрываем уход по
                // сохранённому снимку. Без пустого кадра и без задержки клика.
                m_idle    = false;
                m_reverse = true;
                m_done    = false;
                m_anim.stop();
                m_anim.setStartValue(1.0);
                m_anim.setEndValue(0.0);
                m_anim.setDuration(m_outOnHideMs);
                m_anim.start();
                repaint();
            } else {
                abortNow();
            }
        }
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        if (m_idle) return;  // Пустая прозрачная сцена (ждёт ухода)
        const qreal t = m_anim.currentValue().toReal();
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);

        QEasingCurve spring(QEasingCurve::OutBack);
        spring.setOvershoot(m_overshoot);
        const QEasingCurve fadeCurve(QEasingCurve::OutQuad);
        const qreal dpr = m_snapshot.devicePixelRatio();

        for (const Piece &pc : m_pieces) {
            // Свой отрезок времени для куска (с учётом задержки)
            const qreal local = qBound(0.0, (t - pc.delay) / (1.0 - pc.delay), 1.0);
            const qreal e = spring.valueForProgress(local);   // Движение (с перелётом)
            const qreal o = fadeCurve.valueForProgress(local); // Прозрачность
            if (o <= 0.0) continue;

            const qreal sx = pc.fromScaleX + (1.0 - pc.fromScaleX) * e;
            const qreal sy = pc.fromScaleY + (1.0 - pc.fromScaleY) * e;
            const QPointF off = pc.fromOffset * (1.0 - e);

            p.save();
            p.setOpacity(o);
            p.translate(m_targetPos - pos());             // Начало координат = угол окна
            p.translate(off);
            p.translate(pc.anchor);
            p.scale(sx, sy);
            p.translate(-pc.anchor);
            const QRectF srcPx(pc.src.x() * dpr, pc.src.y() * dpr,
                               pc.src.width() * dpr, pc.src.height() * dpr);
            p.drawPixmap(QRectF(pc.src), m_snapshot, srcPx);
            p.restore();
        }

        // Первый кадр нарисован — на следующем шаге (после вывода на экран)
        // сообщаем об этом (см. setOnFirstFrame)
        if (m_onFirstFrame) {
            auto fn = std::move(m_onFirstFrame);
            m_onFirstFrame = nullptr;
            QTimer::singleShot(0, this, fn);
        }
    }

private:
    // Поставить сцену над окном: чуть больше окна — чтобы было куда «перелетать»
    // пружинкой, — но НЕ за пределами его монитора (иначе выезжающие с краёв
    // части были видны на соседних мониторах)
    void placeOver(QWidget *target)
    {
        m_targetPos = target->pos();
        const QRect g(m_targetPos, target->size());
        const int margin = 60;
        QRect area = g.adjusted(-margin, -margin, margin, margin);
        if (QScreen *scr = target->screen())
            area = area.intersected(scr->geometry());
        if (geometry() != area) setGeometry(area);
    }

    void finish()
    {
        if (m_done) return;
        m_done = true;
        m_anim.stop();
        // Появление доиграло — показываем настоящее окно. После ухода его НЕ
        // показываем: оно прозрачное и сейчас спрячется по-настоящему (раньше
        // здесь оно на миг становилось видимым — мигание в конце анимации).
        if (m_target && !m_reverse) m_target->setWindowOpacity(1.0);
        if (m_reverse && m_onFinished) {
            auto fn = std::move(m_onFinished);
            m_onFinished = nullptr;
            QTimer::singleShot(0, qApp, fn);
        }
        if (m_keepAlive && !m_reverse && m_target && m_target->isVisible()) {
            // Появление закончилось — сцена остаётся пустой и ждёт ухода
            m_idle = true;
            if (m_outOnHideMs <= 0)
                m_snapshot = QPixmap();  // Освобождаем память снимка (меню он ещё нужен)
            update();
            return;
        }
        close();
    }

public:
    // Немедленно убрать сцену (в т.ч. пустую)
    void abortNow()
    {
        m_done = true;
        m_anim.stop();
        if (m_target && !m_reverse) m_target->setWindowOpacity(1.0);
        close();
    }

private:

    QPointer<QWidget> m_target;
    QList<Piece>      m_pieces;
    QPixmap           m_snapshot;
    QVariantAnimation m_anim;
    qreal             m_overshoot = 1.2;
    bool              m_reverse = false;
    bool              m_keepAlive = false;
    bool              m_idle = false;
    int               m_outOnHideMs = 0;
    std::function<void()> m_onFirstFrame;
    std::function<void()> m_onFinished;
    QPoint            m_targetPos;  // Где на экране стоит настоящее окно
    bool              m_done = false;
};

// Запустить анимацию появления окна по кускам. Вызывать из showEvent()
// (или сразу после show()), когда окно уже на своём месте и разложено.
inline SnapshotOverlay *playIn(Kind kind, QWidget *w, const QList<Piece> &pieces, int baseMs,
                               qreal overshoot = 1.2, bool keepAlive = false)
{
    if (!enabled(kind)) { w->setWindowOpacity(1.0); return nullptr; }
    w->setWindowOpacity(0.0);  // Настоящее окно прячем — его играет снимок.
                               // (Для главного окна это делается ещё ДО show(),
                               //  иначе оно на миг мелькает уже собранным.)
    auto *ov = new SnapshotOverlay(w, pieces, ms(kind, baseMs), overshoot, false, keepAlive);
    ov->start();
    return ov;
}

// Анимация ухода: тот же снимок и куски, но задом наперёд. Вызывать ДО hide()
// (снимок делается сейчас), а сразу после — прятать настоящее окно.
inline SnapshotOverlay *playOut(Kind kind, QWidget *w, const QList<Piece> &pieces, int baseMs,
                                qreal overshoot = 1.2)
{
    if (!enabled(kind)) return nullptr;
    auto *ov = new SnapshotOverlay(w, pieces, ms(kind, baseMs), overshoot, true);
    ov->start();
    return ov;
}

// ─── Готовые стили ───────────────────────────────────────────────────────────

// Меню разворачивается сверху вниз, как шторка, с лёгкой пружинкой
inline void menuIn(QWidget *menu)
{
    if (!enabled(Kind::Menu)) { menu->setWindowOpacity(1.0); return; }
    // Сцену создаём на шаг ПОЗЖЕ, когда меню уже реально на экране. Если
    // создать её прямо в showEvent (до показа меню), Windows при показе меню
    // поднимает его «хозяина» — главное окно SmartClip — НАД сценой, и под
    // меню «Профили»/сортировки (там колонка с карточками) анимацию не видно.
    // Меню пока прозрачное — задержки не видно.
    menu->setWindowOpacity(0.0);
    QPointer<QWidget> guard(menu);
    QTimer::singleShot(0, menu, [guard]() {
        if (!guard || !guard->isVisible()) return;
        Piece pc;
        pc.src        = guard->rect();
        pc.anchor     = QPointF(guard->width() / 2.0, 0);
        pc.fromScaleX = 0.92;
        pc.fromScaleY = 0.35;
        pc.fromOffset = QPointF(0, -6);
        // Сцена остаётся (пустой) — при закрытии меню сворачивается шторкой обратно
        if (SnapshotOverlay *ov = playIn(Kind::Menu, guard, {pc}, 260, 1.15, /*keepAlive=*/true))
            ov->setOutOnHide(ms(Kind::Menu, 150));
    });
}

// ─── Анимации диалога: появление и уход ──────────────────────────────────────
// Появление: окно выпрыгивает из центра (зум 82% → 100% с перелётом).
// Уход: то же задом наперёд, в ~1.7 раза быстрее (крестик, «Отмена»,
// «Сохранить», Esc, клик мимо окна).
//
// Как подключить к своему QDialog:
//   showEvent(): QDialog::showEvent(e); m_anim->onShow();
//   done(r):     if (!m_anim->handleDone([self = QPointer<Me>(this), r] {
//                        if (self) self->QDialog::done(r); }))
//                    QDialog::done(r);
class DialogAnimator : public QObject
{
public:
    explicit DialogAnimator(QWidget *dlg) : QObject(dlg), m_dlg(dlg)
    {
        dlg->installEventFilter(this);
        m_start.setSingleShot(true);
        connect(&m_start, &QTimer::timeout, this, [this]() { startIn(); });
    }

    void onShow()
    {
        m_closing = false;
        if (!enabled(Kind::Dialog)) { m_dlg->setWindowOpacity(1.0); return; }
        // Старт откладываем, пока окно не получит ОКОНЧАТЕЛЬНЫЙ размер.
        // В момент showEvent он ещё не финальный: Qt доводит раскладку позже,
        // а если окно открывается на мониторе с другим масштабом — ещё и
        // пересчитывает размер под этот монитор (~100мс). Снимок, снятый
        // раньше, был «сжатым», и окно потом «расправлялось».
        // Ждём, пока размер перестанет меняться (не дольше 0.3с); окно
        // в это время прозрачное, ожидания не видно.
        m_dlg->setWindowOpacity(0.0);
        m_waiting = true;
        m_waitClock.start();
        m_start.start(40);
    }

    // Окно закрывают. true — уход с анимацией начат (reallyDone будет вызван
    // сам, когда сцена перекроет окно), false — закрывайте сразу.
    bool handleDone(std::function<void()> reallyDone)
    {
        if (m_closing) return true;  // Уже уходим — повторный вызов игнорируем
        if (!enabled(Kind::Dialog) || !m_dlg || !m_dlg->isVisible()) return false;

        // Ещё играет появление — разворачиваем его, окно и так прозрачное
        if (m_ov && m_ov->isPlaying() && !m_ov->isReverse()) {
            m_closing = true;
            m_ov->reverseNow();
            reallyDone();
            return true;
        }
        // Сцена от появления ждёт — уход стартует мгновенно
        if (m_ov && m_ov->isIdle()) {
            m_closing = true;
            m_ov->replayOut({piece()}, ms(Kind::Dialog, 200), reallyDone);
            return true;
        }
        return false;
    }

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        // Размер ещё меняется — ждём дальше (но не бесконечно)
        if (obj == m_dlg && e->type() == QEvent::Resize && m_waiting && m_waitClock.elapsed() < 300)
            m_start.start(40);
        return false;
    }

private:
    void startIn()
    {
        m_waiting = false;
        if (!m_dlg || !m_dlg->isVisible() || m_closing) return;
        if (m_dlg->layout()) m_dlg->layout()->activate();
        m_ov = playIn(Kind::Dialog, m_dlg, {piece()}, 340, 1.4, /*keepAlive=*/true);
    }

    Piece piece() const
    {
        Piece pc;
        pc.src        = m_dlg->rect();
        pc.anchor     = QPointF(m_dlg->width() / 2.0, m_dlg->height() / 2.0);
        pc.fromScaleX = 0.82;
        pc.fromScaleY = 0.82;
        pc.fromOffset = QPointF(0, 18);
        return pc;
    }

    QPointer<QWidget>         m_dlg;
    QPointer<SnapshotOverlay> m_ov;
    bool                      m_closing = false;
    bool                      m_waiting = false;  // Ждём окончательного размера окна
    QTimer                    m_start;            // Старт появления (после «успокоения» размера)
    QElapsedTimer             m_waitClock;
};

} // namespace Anim
