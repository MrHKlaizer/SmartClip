// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  cards_monkey.cpp — «обезьяна» для карточек в колонках                      ║
// ║                                                                              ║
// ║  Ловит «съехавшие карточки»: иногда карточки посреди колонки остаются        ║
// ║  сдвинутыми вбок. Случайно листает (колесом и прыжками), добавляет новые     ║
// ║  записи, переключает История ↔ Закрепы, закрывает и открывает окно,          ║
// ║  меняет стиль прокрутки и экран — с паузами, часто посреди анимаций.         ║
// ║                                                                              ║
// ║  Правило: когда всё доехало, карточка в средней части колонки (не у краёв,   ║
// ║  где её уводит дуга, и не под мышью) стоит ровно на своём месте (arcBase).   ║
// ║  Запуск: smartclip_tests cards <зерно> [шагов].                              ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#include <QRandomGenerator>
#include <QWheelEvent>
#include <QScrollBar>
#include <QHash>
#include <cstdio>
#include "Check.h"
#include "TestUtil.h"
#define private public
#include "ui/CardArc.h"
#include "ui/SmoothScroll.h"
#undef private

using namespace TestUtil;

// Карточки в средней части колонки должны стоять ровно. true — всё хорошо
static bool cardsStraight(MainWindow *w, QString *why)
{
    struct Col { QScrollArea *area; CardArc *arc; const char *name; };
    for (const Col &c : {Col{w->m_leftScroll, w->m_leftArc, "левая"}, Col{w->m_rightScroll, w->m_rightArc, "правая"}}) {
        if (!c.area || !c.area->isVisible() || !c.area->widget() || !c.arc) continue;
        QWidget *content = c.area->widget();
        const int vh = c.area->viewport()->height();
        if (vh < 200) continue;
        // Где карточки колонки стоят «ровно»: у всех одинаково — берём самое частое
        // место по горизонтали (сравнивать с arcBase нельзя: его-то и могли испортить)
        QHash<int, int> xs;
        for (QObject *o : content->children())
            if (auto *card = qobject_cast<QWidget *>(o))
                if (!card->isHidden() && card->property("arcBase").isValid())
                    ++xs[card->property("arcBase").toPoint().x()];
        int flatX = 0, best = -1;
        for (auto it = xs.cbegin(); it != xs.cend(); ++it) if (it.value() > best) { best = it.value(); flatX = it.key(); }
        for (QObject *o : content->children()) {
            auto *card = qobject_cast<QWidget *>(o);
            if (!card || card->isHidden() || !card->property("arcBase").isValid()) continue;
            if (card == c.arc->m_hovered.data() || c.arc->m_hover.value(card, 0.0) > 0.0) continue;  // Под мышью — выдвинута
            if (card->property("lift").toReal() > 0) continue;  // Её тащат
            const int top = content->y() + card->y();
            // Средняя часть колонки: дуга тут не действует ни при каком стиле
            if (top < vh * 0.3 || top + card->height() > vh * 0.75) continue;
            if (card->x() != flatX) {
                *why = QString("%1 колонка: карточка посередине сдвинута вбок на %2 px (y=%3, высота колонки %4, "
                               "запомненное «родное» место сдвинуто на %5)")
                           .arg(c.name).arg(card->x() - flatX).arg(top).arg(vh)
                           .arg(card->property("arcBase").toPoint().x() - flatX);
                return false;
            }
        }
    }
    return true;
}

int cardsMonkeyMain(int argc, char **argv, Check::Env &env)
{
    const quint32 seed = argc > 2 ? QString::fromLocal8Bit(argv[2]).toUInt() : 1;
    const int steps = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 200;
    QRandomGenerator rnd(seed);

    App a(env);
    // Тексты разной длины, фото — чтобы были обе колонки
    for (int i = 0; i < 80; ++i)
        a.db.addHistory("text", QString("Запись %1 ").arg(i) + QString("слово ").repeated(rnd.bounded(1, 60)), "", "app");
    QImage pic(320, 200, QImage::Format_RGB32);
    for (int i = 0; i < 25; ++i) {
        pic.fill(QColor::fromHsv(i * 14 % 360, 120, 200));
        const QString f = env.dir() + QString("/img%1.png").arg(i);
        pic.save(f);
        a.db.addHistory("image", "", f, "app");
    }
    for (int i = 0; i < 15; ++i) a.db.addPin("", QString("закреп %1").arg(i), "text", QString("текст закрепа ").repeated(rnd.bounded(1, 20)), "");
    a.start();
    MainWindow *w = a.w;
    const QList<QRect> screens = {QRect(0, 0, 2048, 1152), QRect(0, 0, 1080, 1920), QRect(0, 0, 1440, 960)};

    auto busy = [&]() {
        for (CardArc *arc : {w->m_leftArc, w->m_rightArc})
            if (arc && (arc->m_timer.isActive() || arc->m_reflow)) return true;
        for (SmoothScroller *s : {w->m_leftScroller, w->m_rightScroller})
            if (s && s->m_timer.isActive()) return true;
        return false;
    };

    QStringList log;
    int added = 0;
    for (int step = 0; step < steps; ++step) {
        const int act = rnd.bounded(100);
        QString what;
        QScrollArea *col = rnd.bounded(2) ? w->m_leftScroll : w->m_rightScroll;
        if (act < 35) {
            const int d = (rnd.bounded(2) ? 1 : -1) * 120 * rnd.bounded(1, 6);
            what = QString("колесо %1 %2").arg(col == w->m_leftScroll ? "слева" : "справа").arg(d);
            const QPoint pos(col->viewport()->width() / 2, col->viewport()->height() / 2);
            QWheelEvent we(QPointF(pos), QPointF(col->viewport()->mapToGlobal(pos)), QPoint(), QPoint(0, d), Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(col->viewport(), &we);
        } else if (act < 45) {
            SmoothScroller *s = col == w->m_leftScroll ? w->m_leftScroller : w->m_rightScroller;
            const int to = rnd.bounded(qMax(1, col->verticalScrollBar()->maximum() + 1));
            what = QString("прыжок на %1").arg(to);
            if (s) s->jumpTo(to);
        } else if (act < 60) {
            const int n = rnd.bounded(1, 4);
            what = QString("скопировали %1 записей").arg(n);
            for (int k = 0; k < n; ++k)
                a.db.addHistory("text", QString("новое %1 ").arg(added++) + QString("ещё ").repeated(rnd.bounded(1, 40)), "", "app");
            if (w->m_viewMode == MainWindow::ViewMode::History) w->refreshHistory();
            else w->m_historyDirty = true;
        } else if (act < 70) {
            const bool pins = w->m_viewMode == MainWindow::ViewMode::History;
            what = pins ? "в Закрепы" : "в Историю";
            w->switchView(pins ? MainWindow::ViewMode::Pins : MainWindow::ViewMode::History);
        } else if (act < 80) {
            what = "закрыть и открыть";
            w->hideAnimated();
            Check::Env::wait(rnd.bounded(0, 500));
            w->showWindow();
        } else if (act < 86) {
            const bool arc = Anim::scrollStyle() != QLatin1String("arc");
            what = arc ? "прокрутка: дуга" : "прокрутка: плавная";
            AppSettings::get().setScrollStyle(arc ? "arc" : "smooth");
            if (w->m_leftArc) w->m_leftArc->refresh();
            if (w->m_rightArc) w->m_rightArc->refresh();
        } else if (act < 90) {
            const QRect r = screens[rnd.bounded(int(screens.size()))];
            what = QString("экран %1x%2").arg(r.width()).arg(r.height());
            w->setGeometry(r);
        } else if (act < 94) {
            // Qt иногда раскладывает колонку вхолостую (размер тот же) — как это
            // бывает при перерисовке, смене фокуса и т.п.
            what = "холостая раскладка колонки";
            QVBoxLayout *lay = col == w->m_leftScroll ? w->m_leftLayout : w->m_rightLayout;
            lay->setGeometry(lay->geometry());
        } else {
            what = "листают и сразу копируют";
            const QPoint pos(col->viewport()->width() / 2, col->viewport()->height() / 2);
            QWheelEvent we(QPointF(pos), QPointF(col->viewport()->mapToGlobal(pos)), QPoint(), QPoint(0, -600), Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(col->viewport(), &we);
            Check::Env::wait(rnd.bounded(0, 60));
            a.db.addHistory("text", QString("на ходу %1").arg(added++), "", "app");
            if (w->m_viewMode == MainWindow::ViewMode::History) w->refreshHistory();
        }
        log << QString("%1: %2").arg(step).arg(what);
        static const int pauses[] = {0, 10, 40, 120, 300, 700};
        Check::Env::wait(pauses[rnd.bounded(6)]);
        // Каждые 4 шага — дождаться, пока всё встанет, и проверить
        if (step % 4 == 3 || step == steps - 1) {
            Check::Env::waitFor([&]() { return !busy(); }, 3000);
            Check::Env::wait(20);  // Сразу, как доехало: страховка дуги не должна прятать ошибку
            QString why;
            bool ok = !w->isVisible() || cardsStraight(w, &why);
            // Страховка дуги работает всё время, пока окно открыто
            if (ok && w->isVisible() && !w->m_hidePending)
                for (CardArc *arc : {w->m_leftArc, w->m_rightArc})
                    if (arc && !arc->m_hoverCheck.isActive()) { why = "страховка дуги выключилась, хотя окно открыто"; ok = false; }
            if (!ok) {
                std::fprintf(stderr, "MONKEY cards seed=%u шаг=%d: %s\n", seed, step, qUtf8Printable(why));
                std::fprintf(stderr, "MONKEY повторить: smartclip_tests cards %u %d\n", seed, step + 1);
                for (int i = qMax(0, int(log.size()) - 14); i < log.size(); ++i)
                    std::fprintf(stderr, "    %s\n", qUtf8Printable(log[i]));
                CHECK(false, "карточки съехали: %1", why);
                return 1;
            }
        }
    }
    std::fprintf(stderr, "cards seed=%u: %d шагов — карточки на местах\n", seed, steps);
    CHECK(true);
    return 0;
}

// Тот самый редкий баг, коротко: дуга отвела крайние карточки, Qt разложил
// колонку вхолостую, карточки долистали до середины — они должны стоять ровно
TEST(cards_idle_layout)
{
    App a(env);
    for (int i = 0; i < 60; ++i)
        a.db.addHistory("text", QString("Запись %1 ").arg(i) + QString("слово ").repeated(10 + i % 30), "", "app");
    AppSettings::get().setScrollStyle("arc");
    a.start();
    MainWindow *w = a.w;
    // Листаем вниз — верхние карточки уходят по дуге
    w->m_leftScroller->jumpTo(900);
    Check::Env::wait(600);
    int shifted = 0;
    for (int i = 0; i < w->m_leftLayout->count(); ++i)
        if (QWidget *c = w->m_leftLayout->itemAt(i)->widget())
            if (c->property("arcBase").isValid() && c->x() != c->property("arcBase").toPoint().x()) ++shifted;
    CHECK(shifted > 0, "дуга отвела крайние карточки (иначе тест ничего не проверяет)");
    // Холостая раскладка (размер тот же) — как бывает у Qt
    w->m_leftLayout->setGeometry(w->m_leftLayout->geometry());
    Check::Env::wait(100);
    // Долистали обратно — те карточки теперь в середине
    w->m_leftScroller->jumpTo(450);
    Check::Env::wait(800);
    QString why;
    CHECK(cardsStraight(w, &why), "%1", why);
}
