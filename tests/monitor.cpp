// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  smartclip_testmon — монитор тестов SmartClip (как диспетчер задач)          ║
// ║                                                                              ║
// ║  Значок в трее (у часов): синий — идут тесты, зелёный — всё прошло,          ║
// ║  красный — что-то упало, серый — тестов сейчас нет. Наведи — какой тест     ║
// ║  идёт и сколько осталось. Клик — окно: общий прогресс, текущий тест и       ║
// ║  сколько он идёт, все тесты с результатом, временем и прошлым прогоном,     ║
// ║  подробности у упавших.                                                      ║
// ║                                                                              ║
// ║  Читает status.json и last_results.json рядом с собой — их пишет            ║
// ║  smartclip_tests при каждом прогоне. run_tests.bat запускает монитор сам    ║
// ║  (если он уже запущен — второй не откроется).                               ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTreeWidget>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QPainter>
#include <QScreen>
#include <QLockFile>
#include <QDir>
#include <QCloseEvent>
#include <windows.h>

static QString secText(double s)
{
    if (s < 60) return QString::number(s, 'f', 1) + " с";
    return QString("%1:%2").arg(int(s) / 60).arg(int(s) % 60, 2, 10, QChar('0'));
}

// Значок: кружок цвета состояния и дуга прогресса
static QIcon makeIcon(const QColor &c, double progress)
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 40));
    p.drawEllipse(QRectF(2, 2, 60, 60));
    p.setBrush(c);
    p.drawEllipse(QRectF(16, 16, 32, 32));
    if (progress > 0 && progress < 1) {
        p.setPen(QPen(c, 7, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(7, 7, 50, 50), 90 * 16, -int(progress * 360 * 16));
    }
    return QIcon(pm);
}

class Monitor : public QWidget
{
public:
    explicit Monitor(const QString &dir) : m_dir(dir)
    {
        setWindowTitle("Тесты SmartClip");
        setStyleSheet(
            "QWidget { background: #14141e; color: #e6e6ee; font-size: 13px; }"
            "QTreeWidget, QPlainTextEdit { background: #1b1b28; border: 1px solid #2c2c3c; border-radius: 6px; }"
            "QTreeWidget::item { padding: 3px 0; }"
            "QTreeWidget::item:selected { background: #2e3a5c; }"
            "QHeaderView::section { background: #1f1f2e; color: #9aa0b8; border: none; padding: 5px; }"
            "QProgressBar { background: #22222f; border: none; border-radius: 5px; height: 10px; }"
            "QProgressBar::chunk { background: #5b8cff; border-radius: 5px; }");
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(14, 12, 14, 12);
        lay->setSpacing(8);
        m_title = new QLabel(this);
        m_title->setStyleSheet("font-size: 18px; font-weight: 600;");
        m_bar = new QProgressBar(this);
        m_bar->setTextVisible(false);
        m_bar->setFixedHeight(10);
        m_stats = new QLabel(this);
        m_stats->setStyleSheet("color: #9aa0b8;");
        m_current = new QLabel(this);
        m_current->setWordWrap(true);
        m_current->setStyleSheet("background: #1f2740; border-radius: 6px; padding: 8px; font-size: 14px;");
        m_tree = new QTreeWidget(this);
        m_tree->setColumnCount(5);
        m_tree->setHeaderLabels({"Тест", "Сейчас", "Время", "Прошлый раз", "Тогда"});
        m_tree->setRootIsDecorated(false);
        m_tree->setUniformRowHeights(true);
        m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int c = 1; c < 5; ++c) m_tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
        m_details = new QPlainTextEdit(this);
        m_details->setReadOnly(true);
        m_details->setPlaceholderText("Выбери тест — здесь будет, что с ним не так");
        auto *split = new QSplitter(Qt::Vertical, this);
        split->addWidget(m_tree);
        split->addWidget(m_details);
        split->setStretchFactor(0, 4);
        split->setStretchFactor(1, 1);
        lay->addWidget(m_title);
        lay->addWidget(m_bar);
        lay->addWidget(m_stats);
        lay->addWidget(m_current);
        lay->addWidget(split, 1);
        connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
            m_details->setPlainText(it ? it->data(0, Qt::UserRole).toString() : QString());
        });

        // Размер: по центру экрана, не во весь — по бокам SmartClip (и его тесты)
        // рисуют колонки карточек
        QScreen *scr = QGuiApplication::primaryScreen();
        const QRect av = scr->availableGeometry();
        const QSize sz(qMin(820, av.width() * 45 / 100), av.height() * 85 / 100);
        resize(sz);
        move(av.center() - QPoint(sz.width() / 2, sz.height() / 2));

        m_tray.setIcon(makeIcon(QColor(120, 120, 130), 0));
        m_tray.setToolTip("Тесты SmartClip");
        auto *menu = new QMenu(this);
        menu->addAction("Открыть", this, [this]() { showUp(); });
        menu->addSeparator();
        menu->addAction("Выход", qApp, &QApplication::quit);
        m_tray.setContextMenu(menu);
        connect(&m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
                if (isVisible() && isActiveWindow()) hide(); else showUp();
            }
        });
        m_tray.show();

        connect(&m_timer, &QTimer::timeout, this, [this]() { reload(); });
        m_timer.start(500);
        reload();
    }

protected:
    void closeEvent(QCloseEvent *e) override { hide(); e->ignore(); }  // Крестик — в трей, не выход

private:
    void showUp()
    {
        show();
        raise();
        activateWindow();
    }

    void reload()
    {
        QFile f(m_dir + "/status.json");
        if (!f.open(QIODevice::ReadOnly)) {
            m_title->setText("Тестов пока не было");
            m_stats->setText("Запусти run_tests.bat — здесь появится ход прогона");
            m_current->hide();
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (o.isEmpty()) return;  // Пишется прямо сейчас — в следующий раз
        const QJsonArray jobs = o["jobs"].toArray();
        const bool finished = o["finished"].toBool();
        // Прогон оборвался (закрыли окно, Ctrl+C): процесса уже нет
        const DWORD pid = DWORD(o["pid"].toInteger());
        bool alive = false;
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            DWORD code = 0;
            alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
            CloseHandle(h);
        }
        int done = 0, failed = 0, okN = 0;
        double spent = 0, remainingGuess = 0;
        for (const QJsonValue &v : jobs) {
            const QJsonObject j = v.toObject();
            const QString s = j["state"].toString();
            if (s == "ok") { ++done; ++okN; spent += j["sec"].toDouble(); }
            else if (s == "fail") { ++done; ++failed; spent += j["sec"].toDouble(); }
            else remainingGuess += j["prevSec"].toDouble(10);  // Не было — считаем ~10 с
        }
        const int total = int(jobs.size());
        const QDateTime started = QDateTime::fromString(o["started"].toString(), Qt::ISODate);
        const double elapsed = started.secsTo(QDateTime::currentDateTime());
        QString state;
        QColor color;
        if (!finished && alive) {
            state = QString("Идёт %1 прогон").arg(o["mode"].toString());
            color = QColor(91, 140, 255);
        } else if (!finished) {
            state = "Прогон оборвался";
            color = QColor(200, 150, 60);
        } else if (failed) {
            state = QString("Готово — есть ошибки (%1)").arg(failed);
            color = QColor(230, 80, 80);
        } else {
            state = "Готово — всё хорошо";
            color = QColor(70, 190, 110);
        }
        m_title->setText(state);
        m_title->setStyleSheet(QString("font-size: 18px; font-weight: 600; color: %1;").arg(color.name()));
        m_bar->setMaximum(qMax(1, total));
        m_bar->setValue(done);
        QString stats = QString("%1 из %2 · прошло %3 · упало %4 · идёт %5")
                            .arg(done).arg(total).arg(okN).arg(failed).arg(secText(elapsed));
        if (!finished && alive) stats += " · осталось ≈ " + secText(remainingGuess);
        m_stats->setText(stats);

        const QString cur = o["current"].toString();
        if (!finished && alive && !cur.isEmpty()) {
            const QDateTime cs = QDateTime::fromString(o["currentStarted"].toString(), Qt::ISODate);
            double prev = 0;
            for (const QJsonValue &v : jobs) if (v.toObject()["title"] == cur) prev = v.toObject()["prevSec"].toDouble();
            QString t = QString("Сейчас: <b>%1</b><br>идёт %2").arg(cur.toHtmlEscaped(), secText(cs.secsTo(QDateTime::currentDateTime())));
            if (prev > 0) t += QString(" (в прошлый раз — %1)").arg(secText(prev));
            m_current->setText(t);
            m_current->show();
        } else {
            m_current->hide();
        }

        // Таблица: обновляем на месте (выбранная строка и прокрутка не сбиваются)
        const bool rebuild = m_tree->topLevelItemCount() != total;
        if (rebuild) m_tree->clear();
        auto stateText = [](const QString &s) {
            if (s == "ok") return QString("✓ прошёл");
            if (s == "fail") return QString("✗ упал");
            if (s == "run") return QString("▶ идёт");
            if (s == "wait") return QString("· ждёт");
            return QString("—");
        };
        auto stateColor = [](const QString &s) {
            if (s == "ok") return QColor(110, 210, 140);
            if (s == "fail") return QColor(240, 100, 100);
            if (s == "run") return QColor(120, 165, 255);
            return QColor(130, 130, 150);
        };
        for (int i = 0; i < total; ++i) {
            const QJsonObject j = jobs[i].toObject();
            QTreeWidgetItem *it = rebuild ? new QTreeWidgetItem(m_tree) : m_tree->topLevelItem(i);
            const QString s = j["state"].toString(), ps = j["prevState"].toString();
            it->setText(0, j["title"].toString());
            it->setText(1, stateText(s));
            it->setForeground(1, stateColor(s));
            it->setText(2, (s == "ok" || s == "fail") ? secText(j["sec"].toDouble()) : QString());
            it->setText(3, ps.isEmpty() ? "—" : stateText(ps));
            it->setForeground(3, stateColor(ps));
            it->setText(4, j["prevSec"].toDouble() > 0 ? secText(j["prevSec"].toDouble()) : QString());
            QString d = j["details"].toString();
            if (d.isEmpty()) d = s == "ok" ? "Прошёл." : s == "run" ? "Идёт…" : s == "wait" ? "Ещё не запускался." : "";
            it->setData(0, Qt::UserRole, d);
            if (s == "run" && m_lastRun != i) { m_tree->scrollToItem(it); m_lastRun = i; }
        }
        if (QTreeWidgetItem *it = m_tree->currentItem()) m_details->setPlainText(it->data(0, Qt::UserRole).toString());

        // Трей
        m_tray.setIcon(makeIcon(color, finished || !alive ? 0 : double(done) / qMax(1, total)));
        QString tip = state + QString("\n%1 из %2, упало %3").arg(done).arg(total).arg(failed);
        if (!finished && alive && !cur.isEmpty()) tip += "\nСейчас: " + cur;
        m_tray.setToolTip(tip.left(120));
        // Только что закончился — всплывашка
        if (finished && !m_wasFinished && m_seenRunning)
            m_tray.showMessage("Тесты SmartClip", state, failed ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 6000);
        if (!finished && alive) m_seenRunning = true;
        m_wasFinished = finished;
    }

    QString m_dir;
    QSystemTrayIcon m_tray;
    QTimer m_timer;
    QLabel *m_title, *m_stats, *m_current;
    QProgressBar *m_bar;
    QTreeWidget *m_tree;
    QPlainTextEdit *m_details;
    int m_lastRun = -1;
    bool m_wasFinished = true, m_seenRunning = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);  // Живёт в трее
    const QString dir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
    // Один монитор: второй запуск ничего не делает
    QLockFile lock(QDir::tempPath() + "/smartclip_testmon.lock");
    if (!lock.tryLock(100)) return 0;
    Monitor m(QFileInfo(QCoreApplication::applicationFilePath()).absolutePath());
    Q_UNUSED(dir)
    if (app.arguments().contains("--show")) m.show();
    return app.exec();
}
