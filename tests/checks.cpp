// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  checks.cpp — проверки, которые сами решают «верно / не верно»               ║
// ║                                                                              ║
// ║  Каждый TEST — отдельный процесс со своей временной папкой. Файлы для       ║
// ║  тестов создаются тут же; настоящие папки и настройки не трогаются, окна    ║
// ║  на экране не показываются (WA_DontShowOnScreen).                           ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QDateTime>
#include <QSettings>
#include <QSqlQuery>
#include "Check.h"
#include "TestUtil.h"

using namespace TestUtil;

static int s4(FolderShelf *s) { return s->s(4); }

// ═══ Логика без окон ═════════════════════════════════════════════════════════

TEST(sort_logic)
{
    struct E { QString name; qint64 size; QDateTime mtime; };
    const QDateTime t0 = QDateTime::currentDateTime();
    QList<E> l = {
        {"файл 10.txt", 5, t0.addSecs(-30)}, {"файл 2.txt", 50, t0.addSecs(-10)}, {"Б.pdf", 5, t0},
        {"а.zip", 7, t0.addSecs(-20)}, {"без_расширения", 0, t0.addSecs(-5)}, {".gitignore", 1, t0.addSecs(-1)},
    };
    auto names = [](const QList<E> &v) { QStringList r; for (const E &e : v) r << e.name; return r.join(" | "); };

    QList<E> a = l;
    Files::sortList(a, "name", false);
    // Числа — как числа (2 раньше 10), регистр не важен
    CHECK(names(a).indexOf("файл 2") < names(a).indexOf("файл 10"), "по имени: %1", names(a));
    CHECK(names(a).indexOf("а.zip") < names(a).indexOf("Б.pdf"), "регистр/алфавит: %1", names(a));
    QList<E> b = l;
    Files::sortList(b, "name", true);
    for (int i = 0; i < a.size(); ++i) CHECK(a[i].name == b[b.size() - 1 - i].name, "по убыванию — ровно наоборот: %1", names(b));

    QList<E> c = l;
    Files::sortList(c, "size", true);
    for (int i = 1; i < c.size(); ++i) CHECK(c[i - 1].size >= c[i].size, "размер по убыванию: %1", names(c));
    // Равные по размеру (5 и 5) — по имени
    int i5a = -1, i5b = -1;
    for (int i = 0; i < c.size(); ++i) { if (c[i].name == "Б.pdf") i5a = i; if (c[i].name == "файл 10.txt") i5b = i; }
    CHECK(i5a >= 0 && i5b >= 0 && i5a < i5b, "равные — по имени: %1", names(c));

    QList<E> d = l;
    Files::sortList(d, "date", true);
    for (int i = 1; i < d.size(); ++i) CHECK(d[i - 1].mtime >= d[i].mtime, "дата по убыванию: %1", names(d));

    QList<E> e = l;
    Files::sortList(e, "type", false);
    CHECK(e.first().name == "без_расширения" || e.first().name == ".gitignore", "без расширения — первыми: %1", names(e));

    QList<E> empty;
    Files::sortList(empty, "name", false);  // Не падает на пустом
    CHECK(empty.isEmpty());
    QList<E> unknown = l;
    Files::sortList(unknown, "что-то-не-то", true);  // Неизвестный ключ — как по дате
    CHECK(unknown.first().name == "Б.pdf", "неизвестный ключ: %1", names(unknown));
}

TEST(categories)
{
    CHECK(Files::category("C:/a/photo.JPG") == "image", "регистр расширения");
    CHECK(Files::category("C:/a/clip.mp4") == "video");
    CHECK(Files::category("C:/a/archive.tar.gz") == "archives", "%1", Files::category("C:/a/archive.tar.gz"));
    CHECK(Files::category("C:/a/noext") == "other");
    CHECK(Files::category("C:/a/.hidden") == "other");
    CHECK(Files::category("C:/a/code.ts") == "code", "ts — код (не видео)");
    CHECK(Files::viewable("C:/a/x.png") && Files::viewable("C:/a/x.mkv") && !Files::viewable("C:/a/x.pdf"));
    for (const QString &t : AppSettings::allFileTypes()) CHECK(!t.isEmpty());
}

TEST(folder_settings)
{
    AppSettings &s = AppSettings::get();
    const QString root = env.dir() + "/Root";
    CHECK(s.folderView(root) == s.filesView(), "не задавали — по умолчанию");
    s.setFolderView(root, "list");
    CHECK(s.folderView(root) == "list");
    CHECK(s.folderView(root + "/a/b/c") == "list", "подпапки берут от родителя");
    s.setFolderView(root + "/a", "tiles");
    CHECK(s.folderView(root + "/a/b") == "tiles", "ближайший родитель важнее");
    CHECK(s.folderView(root) == "list", "родителя не задело");
    CHECK(s.folderView(root.toUpper() + "/A") == "tiles", "Windows: регистр в пути не важен");
    CHECK(s.folderView(root + "/") == "list", "косая черта в конце не важна");
    CHECK(s.folderView("*all*") == s.filesView(), "вкладка «Все» — своё");

    QString k;
    bool desc = false;
    s.folderSort(root, k, desc);
    CHECK(k == s.filesSort() && desc == s.filesSortDesc(), "порядок по умолчанию");
    s.setFolderSort(root, "size", false);
    s.folderSort(root + "/x", k, desc);
    CHECK(k == "size" && !desc, "порядок наследуется: %1", k);
    // Испорченное значение в настройках — не падаем, берём по умолчанию
    s.m_s.setValue("files/folderSort/" + AppSettings::folderHex(root + "/bad"), "мусор");
    s.folderSort(root + "/bad", k, desc);
    CHECK(k == s.filesSort(), "испорченное значение: %1", k);
}

TEST(scan_edge)
{
    const QString dir = env.dir() + "/scan";
    QDir().mkpath(dir + "/sub");
    const QDateTime old = QDateTime::currentDateTime().addYears(-3);
    makeFile(dir + "/обычный.txt", 10);
    makeFile(dir + "/эмодзи 😀 файл.png", 10);
    makeFile(dir + "/без_расширения", 10);
    makeFile(dir + "/старый.pdf", 10, old);
    makeFile(dir + "/качается.crdownload", 10);
    makeFile(dir + "/~$временный.docx", 10);
    makeFile(dir + "/ярлык.lnk", 10);
    makeFile(dir + "/sub/внутри.txt", 10);
    const QList<Files::Entry> all = Files::scan({dir}, AppSettings::allFileTypes());
    QStringList n;
    for (const auto &e : all) n << e.name;
    CHECK(n.contains("обычный.txt") && n.contains("эмодзи 😀 файл.png") && n.contains("без_расширения"), "%1", n.join(", "));
    CHECK(n.contains("старый.pdf"), "старые файлы тоже (лимита по дням нет): %1", n.join(", "));
    CHECK(!n.contains("качается.crdownload") && !n.contains("~$временный.docx") && !n.contains("ярлык.lnk"),
          "служебные не показываем: %1", n.join(", "));
    CHECK(!n.contains("внутри.txt"), "подпапки — не лезем внутрь");
    CHECK(n.first() != "старый.pdf", "свежие первыми");
    // Только выбранные типы
    const QList<Files::Entry> imgs = Files::scan({dir}, {"image"});
    CHECK(imgs.size() == 1 && imgs.first().name.startsWith("эмодзи"), "фильтр типов: %1", int(imgs.size()));
    // Папки нет / пустая / две одинаковые — не падаем
    CHECK(Files::scan({dir + "/нет такой"}, AppSettings::allFileTypes()).isEmpty());
    CHECK(Files::scan({}, AppSettings::allFileTypes()).isEmpty());

    // Огромная папка: не больше 5000 и не дольше пары секунд
    const QString big = env.dir() + "/big";
    QDir().mkpath(big);
    for (int i = 0; i < 5200; ++i) makeFile(big + QString("/f%1.txt").arg(i), 1);
    QElapsedTimer t;
    t.start();
    const QList<Files::Entry> many = Files::scan({big}, AppSettings::allFileTypes());
    const qint64 ms = t.elapsed();
    CHECK(many.size() == 5000, "лимит 5000: %1", int(many.size()));
    CHECK(ms < 4000, "5200 файлов сканировались %1 мс", ms);
}

// ═══ Полоска файлов ══════════════════════════════════════════════════════════

TEST(strip_all_and_sort)
{
    App a(env);
    const QString dl = env.dir() + "/dl";
    QDir().mkpath(dl);
    const QDateTime old = QDateTime::currentDateTime().addDays(-200);
    for (int i = 0; i < 60; ++i) makeFile(dl + QString("/old%1.txt").arg(i, 2, 10, QChar('0')), 10 + i, old.addSecs(i));
    makeFile(dl + "/new.txt", 1);
    AppSettings::get().setFilesFolders({dl});
    a.start();
    FilesStrip *fs = a.w->m_filesStrip;
    CHECK(Check::Env::waitFor([&]() { return fs->m_items.size() == 61; }), "все файлы, а не 30 за неделю: %1", int(fs->m_items.size()));
    CHECK(!fs->m_items.isEmpty() && fs->m_items.first().name == "new.txt", "по умолчанию — свежие первыми");

    // Порядок по имени — только у вкладки «Все»
    AppSettings::get().setFolderSort(fs->viewKey(), "name", false);
    fs->applyFilter();
    CHECK(isSorted(fs->m_items, "name", false), "по имени");
    fs->m_tabHistory = dl;
    fs->applyFilter();
    CHECK(fs->m_items.first().name == "new.txt", "у вкладки папки свой порядок: %1", fs->m_items.first().name);
    AppSettings::get().setFolderView(fs->viewKey(), "list");
    fs->applyFilter();
    CHECK(fs->m_view == "list");
    fs->m_tabHistory.clear();
    fs->applyFilter();
    CHECK(fs->m_view == "tiles", "у «Все» свой вид: %1", fs->m_view);

    // Превью — только видимым (файлов могут быть тысячи). Списком: одна строка —
    // один файл, на экран влезает меньше 61
    AppSettings::get().setFolderView(fs->viewKey(), "list");
    fs->m_thumbs.clear();
    fs->m_waiting.clear();
    fs->applyFilter();
    fs->expand(true);
    Check::Env::wait(50);
    fs->m_hoverPoll.stop();
    Check::Env::wait(600);
    CHECK(fs->m_waiting.size() + fs->m_thumbs.size() < 61, "превью запрошены для всех %1",
          int(fs->m_waiting.size() + fs->m_thumbs.size()));
    // Кнопка сортировки не налезает на вкладки и кнопки вида
    CHECK(fs->tabsRow().right() < fs->sortBtnRect().left(), "вкладки залезли на сортировку");
    CHECK(fs->sortBtnRect().right() < fs->viewBtnRect(0).left(), "сортировка налезла на вид");
    // В Закрепах сортировки нет
    fs->setPinsMode(true);
    CHECK(!fs->sortBtnAt(fs->sortBtnRect().center() + QPoint(0, fs->top())), "в Закрепах кнопки сортировки нет");
    fs->setPinsMode(false);
}

// ═══ Папка с полки ═══════════════════════════════════════════════════════════

// Правила, которые у открытой папки должны выполняться всегда (их же проверяет обезьяна)
TEST(panel_layout)
{
    App a(env);
    const QString shelf = env.dir() + "/Shelf";
    QDir().mkpath(shelf + "/sub");
    for (int i = 0; i < 90; ++i) makeFile(shelf + QString("/f%1.txt").arg(i, 2, 10, QChar('0')), i);
    for (int i = 0; i < 90; ++i) makeFile(shelf + QString("/sub/s%1.txt").arg(i, 2, 10, QChar('0')), i);
    AppSettings::get().setShelf(0, {shelf}, {});
    a.start();
    FolderPanel *fp = a.w->m_folderPanel;
    a.openShelf(0);
    CHECK(fp->isOpen() && fp->mode() == FolderPanel::Full, "плитки, много файлов — во всю ширину: %1", int(fp->mode()));
    QString why;
    CHECK(panelOk(fp, &why), "%1", why);

    fp->setView("list");
    a.settle();
    CHECK(fp->mode() == FolderPanel::Half, "список — не во всю ширину: %1", int(fp->mode()));
    CHECK(fp->width() == a.w->m_shelves[0]->width(), "список — шириной в свою половину");
    CHECK(panelOk(fp, &why), "%1", why);

    // Вид запоминается у папки, подпапка наследует, своё — только у неё
    fp->enter(shelf + "/sub");
    a.settle();
    CHECK(fp->m_view == "list", "подпапка наследует вид");
    fp->setView("tiles");
    a.settle();
    fp->goUp();
    a.settle();
    CHECK(fp->m_view == "list", "у родителя остался свой вид: %1", fp->m_view);

    // Быстро туда-сюда, пока идёт анимация: каждый кадр шапка на месте,
    // в конце — итоговое место
    int badFrames = 0;
    QString frameWhy;
    for (int k = 0; k < 6; ++k) {
        if (k % 2) fp->goUp(); else fp->enter(shelf + "/sub");
        for (int f = 0; f < 4; ++f) {
            Check::Env::wait(12);
            QString w;
            if (!panelFrameOk(fp, &w)) { ++badFrames; frameWhy = w; }
        }
    }
    a.settle();
    CHECK(badFrames == 0, "кадров с поехавшей раскладкой: %1 (%2)", badFrames, frameWhy);
    CHECK(panelOk(fp, &why), "после быстрых переходов: %1", why);
    fp->close();
    a.settle();
    CHECK(!fp->isVisible(), "закрылась");
}

// Дерево: раскрыть подпапку — не мигает, не уезжает вверх, видно содержимое
TEST(panel_tree)
{
    App a(env);
    const QString shelf = env.dir() + "/Shelf";
    QDir().mkpath(shelf + "/A/deep");
    QDir().mkpath(shelf + "/B");
    QDir().mkpath(shelf + "/Пусто");
    for (int i = 0; i < 12; ++i) makeFile(shelf + QString("/f%1.txt").arg(i, 2, 10, QChar('0')), i);
    for (int i = 0; i < 40; ++i) makeFile(shelf + QString("/A/a%1.txt").arg(i, 2, 10, QChar('0')), i);
    makeFile(shelf + "/A/deep/x.txt", 1);
    for (int i = 0; i < 3; ++i) makeFile(shelf + QString("/B/b%1.txt").arg(i), i);
    AppSettings::get().setShelf(0, {shelf}, {});
    AppSettings::get().setFilesTree(true);
    AppSettings::get().setFolderView(shelf, "list");
    AppSettings::get().setFolderSort(shelf, "name", false);
    a.start();
    FolderPanel *fp = a.w->m_folderPanel;
    a.openShelf(0);
    CHECK(fp->treeOn(), "дерево включено");
    QString why;
    CHECK(panelOk(fp, &why), "%1", why);

    // Раскрываем A стрелкой
    const int ia = indexOf(fp, "A");
    CHECK(ia >= 0, "есть подпапка A");
    if (ia < 0) return;
    const QRect geoBefore = fp->geometry();
    const int rowYBefore = fp->mapToGlobal(fp->itemRect(ia).topLeft()).y();
    qreal minOpacity = 1.0;
    click(fp, fp->chevronRect(ia).center());
    for (int f = 0; f < 60; ++f) {  // Каждые ~10 мс: не мигает ли содержимое
        Check::Env::wait(10);
        if (fp->m_anim.isActive()) minOpacity = qMin(minOpacity, fp->contentOpacity());
    }
    a.settle();
    CHECK(minOpacity > 0.99, "содержимое мигнуло (прозрачность падала до %1)", minOpacity);
    CHECK(fp->geometry() == geoBefore, "панель поменяла размер/место при раскрытии (было %1,%2 %3x%4, стало %5,%6 %7x%8)",
          geoBefore.x(), geoBefore.y(), geoBefore.width(), geoBefore.height(),
          fp->geometry().x(), fp->geometry().y(), fp->geometry().width(), fp->geometry().height());
    const int ia2 = indexOf(fp, "A");
    CHECK(ia2 >= 0 && fp->mapToGlobal(fp->itemRect(ia2).topLeft()).y() == rowYBefore,
          "строка, по которой кликнули, уехала: %1 → %2", rowYBefore,
          ia2 >= 0 ? fp->mapToGlobal(fp->itemRect(ia2).topLeft()).y() : -1);
    CHECK(indexOf(fp, "a00.txt") == ia2 + 2 && fp->shown()[ia2 + 2].depth == 1, "содержимое A под ней со сдвигом");
    CHECK(panelOk(fp, &why), "%1", why);

    // Вложенная (deep) и пустая подпапка
    const int id = indexOf(fp, "deep");
    click(fp, fp->chevronRect(id).center());
    a.settle();
    CHECK(indexOf(fp, "x.txt") == id + 1 && fp->shown()[id + 1].depth == 2, "вложенное раскрытие");
    const int ie = indexOf(fp, "Пусто");
    if (ie >= 0 && fp->itemRect(ie).bottom() < fp->height()) {
        click(fp, fp->chevronRect(ie).center());
        a.settle();
        CHECK(panelOk(fp, &why), "пустая подпапка: %1", why);
    }
    // Свернуть A — её содержимое (и deep) пропало, размер тот же
    click(fp, fp->chevronRect(indexOf(fp, "A")).center());
    a.settle();
    CHECK(indexOf(fp, "a00.txt") < 0 && indexOf(fp, "x.txt") < 0, "свернулась");
    CHECK(fp->geometry() == geoBefore, "размер не скачет при сворачивании");

    // Раскрытая подпапка у нижнего края — докручивается, но сама остаётся видна
    const int ib = indexOf(fp, "B");
    fp->m_scroll = fp->maxScroll();
    fp->update();
    if (ib >= 0) {
        click(fp, fp->chevronRect(indexOf(fp, "B")).center());
        a.settle();
        const int nb = indexOf(fp, "B");
        const QRect body = fp->bodyRect();
        CHECK(nb >= 0 && fp->itemRect(nb).top() >= body.top() - 1, "раскрытая подпапка ушла за верх");
        const int lastChild = indexOf(fp, "b2.txt");
        CHECK(lastChild >= 0 && fp->itemRect(lastChild).bottom() <= body.bottom() + 1, "содержимое подпапки не видно");
    }

    // Файл появился в раскрытой подпапке — сам появляется в дереве
    click(fp, fp->chevronRect(indexOf(fp, "A")).center());
    a.settle();
    makeFile(shelf + "/A/a99_new.txt", 1);
    CHECK(Check::Env::waitFor([&]() { return indexOf(fp, "a99_new.txt") >= 0; }, 3000), "новый файл в раскрытой подпапке не появился");
    CHECK(fp->geometry() == geoBefore, "размер не скачет при обновлении");

    // Клик по самой строке подпапки — заходим внутрь, размер — уже по ней
    const int rowA = indexOf(fp, "A");
    click(fp, fp->itemRect(rowA).center());
    a.settle();
    CHECK(QFileInfo(fp->path()).fileName() == "A", "клик по строке — зайти: %1", fp->path());
    CHECK(panelOk(fp, &why), "%1", why);

    // Выключили дерево в настройках — обычный список
    AppSettings::get().setFilesTree(false);
    fp->settingsChanged();
    a.settle();
    CHECK(!fp->treeOn() && panelOk(fp, &why), "%1", why);
}

// Порядок у каждой папки свой и применяется к уже открытой
TEST(panel_sort)
{
    App a(env);
    const QString shelf = env.dir() + "/Shelf";
    QDir().mkpath(shelf + "/dir");
    makeFile(shelf + "/big.bin", 9000);
    makeFile(shelf + "/small.txt", 3);
    makeFile(shelf + "/mid.pdf", 500);
    // Столько, чтобы на любом экране открылась панелью, а не встала в полку
    // (там нет шапки с кнопками)
    for (int i = 0; i < 30; ++i) makeFile(shelf + QString("/z%1.txt").arg(i, 2, 10, QChar('0')), 1);
    AppSettings::get().setShelf(0, {shelf}, {});
    a.start();
    FolderPanel *fp = a.w->m_folderPanel;
    QString gotKey;
    bool gotDesc = true;
    std::function<void(const QString &, bool)> apply;
    fp->onSortMenu = [&](const QPoint &, const QString &k, bool d, const std::function<void(const QString &, bool)> &ap) {
        gotKey = k; gotDesc = d; apply = ap;
    };
    a.openShelf(0);
    click(fp, fp->sortRect().center());
    CHECK(apply != nullptr && gotKey == "date" && gotDesc, "кнопка сортировки: %1", gotKey);
    if (!apply) return;
    apply("size", true);
    CHECK(fp->mode() != FolderPanel::Inline, "открылась панелью");
    QStringList order;
    for (const auto &it : fp->shown()) order << it.name;
    CHECK(order.mid(0, 4) == QStringList({"dir", "big.bin", "mid.pdf", "small.txt"}), "папки первыми, потом по размеру: %1",
          order.mid(0, 5).join(","));
    QString k;
    bool d;
    AppSettings::get().folderSort(shelf, k, d);
    CHECK(k == "size" && d, "запомнилось у папки");
    AppSettings::get().folderSort(env.dir() + "/другая", k, d);
    CHECK(k == "date", "у другой папки — не изменилось");
}

// ═══ Полки: перестановка папок ═══════════════════════════════════════════════

TEST(shelf_reorder)
{
    App a(env);
    QStringList paths;
    for (const QString &n : {"A", "B", "C", "D"}) { QDir().mkpath(env.dir() + "/" + n); paths << env.dir() + "/" + n; }
    QDir().mkpath(env.dir() + "/E");
    AppSettings::get().setShelf(0, paths, {});
    AppSettings::get().setShelf(1, {env.dir() + "/E"}, {});
    a.start();
    FolderShelf *sh = a.w->m_shelves[0], *other = a.w->m_shelves[1];
    auto names = [](FolderShelf *s) { QStringList l; for (const auto &f : s->folders()) l << FolderShelf::label(f); return l.join(","); };
    auto send = [](QWidget *w, QEvent::Type t, const QPoint &pos, Qt::MouseButtons btns) {
        QMouseEvent e(t, pos, w->mapToGlobal(pos), Qt::LeftButton, btns, Qt::NoModifier);
        QApplication::sendEvent(w, &e);
        QApplication::processEvents();
    };
    // Тянем A (первую) вправо на третье место — плавными шагами, как рукой
    const QPoint start = sh->itemRect(0).center();
    const QPoint end = sh->itemRect(2).center() + QPoint(sh->pitch() / 4, 0);
    send(sh, QEvent::MouseButtonPress, start, Qt::LeftButton);
    int notUnder = 0;
    for (int k = 1; k <= 20; ++k) {
        const QPoint p = start + (end - start) * k / 20;
        send(sh, QEvent::MouseMove, p, Qt::LeftButton);
        Check::Env::wait(8);
        // Правило «под мышью»: поднятая папка — под курсором
        if (sh->dragging() && !sh->dragRect().translated(-sh->m_scroll, 0).contains(QPointF(p))) ++notUnder;
    }
    CHECK(sh->dragging(), "потянул — папка поднялась");
    CHECK(notUnder == 0, "поднятая папка отставала от мыши %1 раз", notUnder);
    CHECK(names(sh) == "A,B,C,D", "пока тащим — порядок ещё не меняется");
    Check::Env::wait(300);  // Соседи доехали
    CHECK(sh->shownRect(1).left() == sh->itemRect(0).left() && sh->shownRect(2).left() == sh->itemRect(1).left(),
          "соседи расступились: B на %1 (ждали %2)", sh->shownRect(1).left(), sh->itemRect(0).left());
    CHECK(sh->shownRect(3).left() == sh->itemRect(3).left(), "D осталась на месте");
    send(sh, QEvent::MouseButtonRelease, end, Qt::NoButton);
    a.settle();
    CHECK(!sh->dragging() && names(sh) == "B,C,A,D", "встала на третье место: %1", names(sh));
    CHECK(AppSettings::get().shelfPaths(0).value(2).endsWith("/A"), "порядок сохранился в настройках");
    CHECK(!a.w->m_folderPanel->isOpen(), "перетаскивание — не клик: папка не открылась");
    // Соседи — на своих местах после того, как разъехались
    Check::Env::wait(300);
    bool placed = true;
    for (int i = 0; i < sh->folders().size(); ++i) placed = placed && sh->shownRect(i) == sh->itemRect(i);
    CHECK(placed, "после перестановки папки стоят ровно");

    // Короткий клик — по-прежнему открывает
    click(sh, sh->itemRect(0).center());
    a.settle();
    CHECK(a.w->m_folderPanel->isOpen() && a.w->m_folderPanel->root().endsWith("/B"), "клик открыл папку B");
    a.w->m_folderPanel->close();
    a.settle();

    // Esc посреди перетаскивания — всё как было
    send(sh, QEvent::MouseButtonPress, sh->itemRect(0).center(), Qt::LeftButton);
    send(sh, QEvent::MouseMove, sh->itemRect(3).center(), Qt::LeftButton);
    CHECK(sh->dragging());
    CHECK(sh->cancelDrag());
    send(sh, QEvent::MouseButtonRelease, sh->itemRect(3).center(), Qt::NoButton);
    a.settle();
    CHECK(names(sh) == "B,C,A,D", "Esc — ничего не поменялось: %1", names(sh));
    CHECK(!a.w->m_folderPanel->isOpen(), "и ничего не открылось");

    // На другую полку: D → в начало правой
    const QPoint dStart = sh->itemRect(3).center();
    const QPoint onOther = sh->mapFromGlobal(other->mapToGlobal(other->itemRect(0).topLeft() + QPoint(s4(other), other->height() / 2)));
    send(sh, QEvent::MouseButtonPress, dStart, Qt::LeftButton);
    for (int k = 1; k <= 10; ++k) send(sh, QEvent::MouseMove, dStart + (onOther - dStart) * k / 10, Qt::LeftButton);
    CHECK(other->m_insert == 0, "на другой полке видно, куда встанет: %1", other->m_insert);
    send(sh, QEvent::MouseButtonRelease, onOther, Qt::NoButton);
    a.settle();
    CHECK(names(sh) == "B,C,A" && names(other) == "D,E", "переехала на другую полку: %1 | %2", names(sh), names(other));
    CHECK(other->m_insert == -1, "подсветка места убрана");
    CHECK(AppSettings::get().shelfPaths(1).value(0).endsWith("/D"), "сохранилось");

    // Отпустил в пустоте (не над полками) — вернулась на место
    const QPoint farPt = sh->mapFromGlobal(a.w->mapToGlobal(QPoint(a.w->width() / 2, a.w->height() / 4)));
    send(sh, QEvent::MouseButtonPress, sh->itemRect(0).center(), Qt::LeftButton);
    for (int k = 1; k <= 6; ++k) send(sh, QEvent::MouseMove, sh->itemRect(0).center() + (farPt - sh->itemRect(0).center()) * k / 6, Qt::LeftButton);
    send(sh, QEvent::MouseButtonRelease, farPt, Qt::NoButton);
    a.settle();
    CHECK(names(sh) == "B,C,A" && names(other) == "D,E", "в пустоте — ничего не потерялось: %1 | %2", names(sh), names(other));
}
