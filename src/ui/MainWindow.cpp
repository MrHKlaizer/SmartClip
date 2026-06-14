#include "MainWindow.h"
#include "ClickableCard.h"
#include "SmartButton.h"
#include "ImageViewer.h"
#include "HotkeyEdit.h"
#include "SettingsDialog.h"
#include "core/Database.h"
#include "core/AppSettings.h"

#include <QScreen>
#include <QKeyEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QStandardPaths>
#include "core/Version.h"
#include "core/UpdateChecker.h"
#include <QStyle>
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QLabel>
#include <QPixmap>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPaintEvent>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>
#include <functional>
#include <windows.h>
#include <windowsx.h>
#include <ShObjIdl.h>   // IShellItemImageFactory

// DROPFILES не экспо��и��е�ся в э�ой кон�иг��а�ии MinGW � оп�еделяем в���н��.
// С���к���а с�абил�на с Windows 3.1 и не менялас�.
struct SC_DROPFILES {
    DWORD pFiles; // сме�ение до списка п��ей (всегда sizeof(SC_DROPFILES))
    POINT pt;     // �о�ка сб�оса (для clipboard не испол�з�е�ся)
    BOOL  fNC;    // non-client area (не испол�з�е�ся)
    BOOL  fWide;  // TRUE = п��и в UTF-16 (wchar_t)
};

// �� ��ев�� видео �е�ез Windows Shell (�е же миниа���� ��о в п�оводнике) �����

// �онве��и��ем HBITMAP в QPixmap �е�ез GDI (GetDIBits) � не зависи� о� Qt WinExtras
static QPixmap hbitmapToPixmap(HBITMAP hBmp)
{
    BITMAP bm = {};
    if (!GetObject(hBmp, sizeof(BITMAP), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return {};

    BITMAPINFOHEADER bi = {};
    bi.biSize        = sizeof(BITMAPINFOHEADER);
    bi.biWidth       = bm.bmWidth;
    bi.biHeight      = -bm.bmHeight; // top-down
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) return {};

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);
    HBITMAP hOld  = static_cast<HBITMAP>(SelectObject(hdcMem, hBmp));

    GetDIBits(hdcMem, hBmp, 0, bm.bmHeight,
              img.bits(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    // Shell возв�а�ае� BGRA � Qt Format_ARGB32_Premultiplied �оже BGRA на Windows, вс� ок
    return QPixmap::fromImage(img);
}

static QPixmap shellThumbnail(const QString &filepath, int w, int h)
{
    // �спол�з�ем на�ивн�е �аздели�ели � Shell API ��еб�е� об�а�н�е сле�и
    QString native = QDir::toNativeSeparators(filepath);

    IShellItemImageFactory *pFactory = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(
        native.toStdWString().c_str(), nullptr,
        IID_PPV_ARGS(&pFactory));
    if (FAILED(hr) || !pFactory) return {};

    HBITMAP hBmp = nullptr;
    SIZE sz = { w, h };
    // SIIGBF_BIGGERSIZEOK � ве�н��� ��о ес��, даже бол��е зап�о�енного
    hr = pFactory->GetImage(sz, SIIGBF_BIGGERSIZEOK, &hBmp);
    pFactory->Release();
    if (FAILED(hr) || !hBmp) return {};

    QPixmap result = hbitmapToPixmap(hBmp);
    DeleteObject(hBmp);
    return result;
}

// �� Умн�й поиск �������������������������������������������������������������

// Расс�ояние �евен��ейна межд� дв�мя с��оками (коли�ес�во замен/вс�авок/�далений).
// Тен� для ка��о�ек-пли�ок
static void applyCardShadow(QWidget *w)
{
    auto *fx = new QGraphicsDropShadowEffect(w);
    fx->setBlurRadius(40);
    fx->setOffset(0, 8);
    fx->setColor(QColor(0, 0, 0, 240));
    w->setGraphicsEffect(fx);
}

// Кнопка-крестик: рисует ✕ через QPainter, прижатый к правому краю
class CloseBtn : public QAbstractButton {
public:
    explicit CloseBtn(QWidget *p) : QAbstractButton(p) {
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        bool h = underMouse();
        p.setPen(h ? QColor("#ff6666") : QColor(255, 255, 255, 150));
        QFont f = font(); f.setPixelSize(15); p.setFont(f);
        // ✕ прижат к правому краю: отступ 4px справа, вся высота
        p.drawText(rect().adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter, "✕");
    }
    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QAbstractButton::leaveEvent(e); }
};

// Диалог без OS-рамки: скруглённые углы через QPainter, своя шапка с drag + закрытие
class AppDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AppDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);

        // Шапка растянута на всю ширину (без отступов master-лейаута)
        // чтобы крестик был у самого правого края диалога
        auto *master = new QVBoxLayout(this);
        master->setContentsMargins(0, 0, 0, 0);
        master->setSpacing(0);

        m_titleBar = new QWidget(this);
        m_titleBar->setFixedHeight(36);
        m_titleBar->setStyleSheet("background: transparent;");
        auto *tl = new QHBoxLayout(m_titleBar);
        tl->setContentsMargins(14, 0, 2, 0);   // слева текст, справа минимум
        tl->setSpacing(0);
        m_titleLabel = new QLabel(m_titleBar);
        m_titleLabel->setStyleSheet("color: #e0e0e0; font-size: 13px; font-weight: bold; background: transparent;");
        tl->addWidget(m_titleLabel, 1);

        m_closeBtn = new CloseBtn(m_titleBar);
        connect(m_closeBtn, &QAbstractButton::clicked, this, &QDialog::reject);
        tl->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

        master->addWidget(m_titleBar);

        // Тело с внутренними отступами
        auto *bodyWrap = new QWidget(this);
        bodyWrap->setStyleSheet("background: transparent;");
        auto *bwl = new QVBoxLayout(bodyWrap);
        bwl->setContentsMargins(12, 2, 12, 12);
        bwl->setSpacing(0);
        m_body = new QWidget(bodyWrap);
        m_body->setStyleSheet("background: transparent;");
        bwl->addWidget(m_body, 1);
        master->addWidget(bodyWrap, 1);
    }

    void setWindowTitle(const QString &t) { QDialog::setWindowTitle(t); if (m_titleLabel) m_titleLabel->setText(t); }
    QWidget *body() { return m_body; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1), 12, 12);
        p.fillPath(path, QColor(18, 18, 30, 252));
        p.setPen(QPen(QColor(255,255,255,55), 2));
        p.drawPath(path);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (m_titleBar->geometry().contains(e->pos()) && e->button() == Qt::LeftButton)
            m_drag = e->globalPosition().toPoint() - frameGeometry().topLeft();
        else m_drag = {};
        QDialog::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!m_drag.isNull() && (e->buttons() & Qt::LeftButton))
            move(e->globalPosition().toPoint() - m_drag);
    }
    void mouseReleaseEvent(QMouseEvent *e) override { m_drag = {}; QDialog::mouseReleaseEvent(e); }

private:
    QWidget         *m_titleBar   = nullptr;
    QWidget         *m_body       = nullptr;
    QLabel          *m_titleLabel = nullptr;
    QAbstractButton *m_closeBtn   = nullptr;
    QPoint           m_drag;
};

// Ввод строки в нашем стиле — замена QInputDialog::getText
static QString askText(QWidget *parent,
                       const QString &title,
                       const QString &label,
                       const QString &initial,
                       bool *ok)
{
    AppDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(340);

    QVBoxLayout *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    QLabel *lbl = new QLabel(label, dlg.body());
    lbl->setStyleSheet("color: #c0c0d0; font-size: 13px; background: transparent;");

    QLineEdit *edit = new QLineEdit(initial, dlg.body());
    edit->setStyleSheet(
        "background: rgba(28,28,44,245); color: #f0f0f0;"
        "border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
        "padding: 5px 10px; font-size: 13px;"
        "selection-background-color: rgba(100,150,255,150);");
    edit->selectAll();

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    QPushButton *cancelBtn = new QPushButton(QObject::tr("Отмена"), dlg.body());
    QPushButton *okBtn     = new QPushButton(QObject::tr("ОК"),     dlg.body());
    okBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
        "border:1px solid rgba(120,160,255,160);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:70px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    cancelBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(255,255,255,200);"
        "border:1px solid rgba(255,255,255,55);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:70px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    okBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);

    vl->addWidget(lbl);
    vl->addWidget(edit);
    vl->addStretch();
    vl->addLayout(btnRow);

    QObject::connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(edit, &QLineEdit::returnPressed,  &dlg, &QDialog::accept);

    bool accepted = (dlg.exec() == QDialog::Accepted);
    if (ok) *ok = accepted;
    return accepted ? edit->text() : QString{};
}

// Диалог подтверждения в нашем стиле — замена QMessageBox::warning/critical
// danger=false → обычный; danger=true → красная иконка ⚠
static bool confirmDialog(QWidget *parent,
                          const QString &title,
                          const QString &message,
                          bool danger = false)
{
    AppDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(340);

    QVBoxLayout *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(14);

    // Строка с иконкой и текстом
    QHBoxLayout *msgRow = new QHBoxLayout();
    msgRow->setSpacing(12);
    if (danger) {
        QLabel *icon = new QLabel("⚠", dlg.body());
        icon->setStyleSheet("color: #ff6644; font-size: 24px; background: transparent;");
        icon->setFixedWidth(28);
        icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        msgRow->addWidget(icon);
    }
    QLabel *msgLbl = new QLabel(message, dlg.body());
    msgLbl->setWordWrap(true);
    msgLbl->setStyleSheet("color: #d0d0d0; font-size: 13px; background: transparent;");
    msgRow->addWidget(msgLbl, 1);
    vl->addLayout(msgRow);

    // Кнопки
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    QPushButton *cancelBtn = new QPushButton(QObject::tr("Отмена"), dlg.body());
    QPushButton *okBtn     = new QPushButton(QObject::tr("Да"),     dlg.body());
    okBtn->setStyleSheet(
        danger
        ? "QPushButton{background:rgba(100,20,20,220);color:#ffaaaa;"
          "border:1px solid rgba(200,60,60,160);border-radius:6px;"
          "padding:5px 18px;font-size:13px;min-width:60px;}"
          "QPushButton:hover{background:rgba(160,30,30,240);color:#fff;}"
        : "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
          "border:1px solid rgba(120,160,255,160);border-radius:6px;"
          "padding:5px 18px;font-size:13px;min-width:60px;}"
          "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    cancelBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(255,255,255,200);"
        "border:1px solid rgba(255,255,255,55);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:60px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    okBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    vl->addLayout(btnRow);

    QObject::connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    return dlg.exec() == QDialog::Accepted;
}

// Выбор элемента из списка в нашем стиле — замена QInputDialog::getItem
static QString askItem(QWidget *parent,
                       const QString &title,
                       const QString &label,
                       const QStringList &items,
                       int current,
                       bool *ok)
{
    AppDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(320);

    QVBoxLayout *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    QLabel *lbl = new QLabel(label, dlg.body());
    lbl->setStyleSheet("color: #c0c0d0; font-size: 13px; background: transparent;");

    QComboBox *combo = new QComboBox(dlg.body());
    combo->addItems(items);
    combo->setCurrentIndex(qBound(0, current, items.size() - 1));
    combo->setStyleSheet(
        "QComboBox { background: rgba(28,28,44,245); color: #f0f0f0;"
        "  border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
        "  padding: 5px 10px; font-size: 13px; }"
        "QComboBox:hover { border: 1px solid rgba(255,255,255,90); }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow { image: none; width: 0; }"
        "QComboBox QAbstractItemView {"
        "  background: rgba(22,22,36,252); color: #e0e0e0;"
        "  border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
        "  selection-background-color: rgba(255,255,255,18);"
        "  outline: none; padding: 2px; }");

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    QPushButton *cancelBtn = new QPushButton(QObject::tr("Отмена"), dlg.body());
    QPushButton *okBtn     = new QPushButton(QObject::tr("ОК"),     dlg.body());
    okBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
        "border:1px solid rgba(120,160,255,160);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:60px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    cancelBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(255,255,255,200);"
        "border:1px solid rgba(255,255,255,55);border-radius:6px;"
        "padding:5px 18px;font-size:13px;min-width:60px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    okBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);

    vl->addWidget(lbl);
    vl->addWidget(combo);
    vl->addStretch();
    vl->addLayout(btnRow);

    QObject::connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    bool accepted = (dlg.exec() == QDialog::Accepted);
    if (ok) *ok = accepted;
    return accepted ? combo->currentText() : QString{};
}

// Строка поиска: контейнер рисует скруглённый фон через QPainter (как SmartButton),
// внутри прозрачный QLineEdit без рамки — никаких артефактов на углах
class SearchBar : public QWidget
{
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAutoFillBackground(false);
        m_edit = new QLineEdit(this);
        m_edit->setFrame(false);
        m_edit->setStyleSheet(
            "background: transparent; border: none; "
            "color: #f0f0f0; font-size: 13px; "
            "selection-background-color: rgba(100,150,255,150);"
        );
        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 0, 12, 0);
        lay->addWidget(m_edit);
        // Перерисовка при смене фокуса (рамка меняет яркость)
        connect(m_edit, &QLineEdit::textChanged, this, [this](){ update(); });
        m_edit->installEventFilter(this);
    }
    QLineEdit *edit() { return m_edit; }

    bool eventFilter(QObject *o, QEvent *e) override {
        if (o == m_edit && (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut))
            update();
        return QWidget::eventFilter(o, e);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        bool focused = m_edit->hasFocus();
        QColor border = focused ? QColor(255,255,255,90) : QColor(255,255,255,55);
        QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        QPainterPath path;
        path.addRoundedRect(r, 8, 8);
        p.fillPath(path, QColor(20, 20, 32, 245));
        p.setPen(QPen(border, 2));
        p.drawPath(path);
    }
private:
    QLineEdit *m_edit;
};

// QMenu с настоящими скруглёнными углами через QPainter — без артефактов
// Фон рисуем сами, QMenu рисует пункты поверх прозрачного фона
class RoundedMenu : public QMenu
{
public:
    explicit RoundedMenu(QWidget *parent = nullptr) : QMenu(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        // Делаем фон QMenu прозрачным — рисуем его сами в paintEvent
        setStyleSheet(
            "QMenu { background: transparent; border: none; padding: 4px 0; }"
            "QMenu::item { color: #e0e0e0; padding: 6px 20px; border-radius: 5px; margin: 1px 4px; font-size: 13px; }"
            "QMenu::item:selected { background: rgba(255,255,255,18); color: #ffffff; border: 1px solid rgba(255,255,255,35); }"
            "QMenu::item:disabled { color: rgba(255,255,255,55); }"
            "QMenu::separator { height: 1px; background: rgba(255,255,255,18); margin: 3px 8px; }"
        );
    }
protected:
    void paintEvent(QPaintEvent *e) override
    {
        // 1. Рисуем скруглённый фон через QPainter (без артефактов)
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
        p.fillPath(path, QColor(18, 18, 30, 250));
        p.setPen(QPen(QColor(255, 255, 255, 55), 2));
        p.drawPath(path);
        p.end();
        // 2. QMenu рисует пункты поверх (фон у него прозрачный)
        QMenu::paintEvent(e);
    }
};

// Тень для кнопок и строки поиска — те же параметры что у карточек
static void applyBtnShadow(QWidget *w)
{
    auto *fx = new QGraphicsDropShadowEffect(w);
    fx->setBlurRadius(40);
    fx->setOffset(0, 8);
    fx->setColor(QColor(0, 0, 0, 220));
    w->setGraphicsEffect(fx);
}

static int levenshtein(const QString &a, const QString &b)
{
    int m = a.size(), n = b.size();
    // �п�имиза�ия: �або�аем дв�мя с��оками вмес�о полной ма��и��
    QVector<int> prev(n + 1), curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;
    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            if (a[i-1] == b[j-1])
                curr[j] = prev[j-1];
            else
                curr[j] = 1 + std::min({prev[j], curr[j-1], prev[j-1]});
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

// Subsequence: все б�кв� needle вс��е�а��ся в haystack по по�ядк� (не обяза�ел�но под�яд).
// ��име�: "�кс" � subsequence "�икс" (��к�с).
static bool isSubsequence(const QString &needle, const QString &haystack)
{
    int ni = 0;
    for (int hi = 0; hi < haystack.size() && ni < needle.size(); ++hi)
        if (haystack[hi] == needle[ni]) ++ni;
    return ni == needle.size();
}

// Не���кое совпадение слова зап�оса qw с одним словом �екс�а tw.
// ��ове�яе� нескол�ко с��а�егий:
//   1. То�ное в�ождение qw в tw
//   2. Subsequence: б�кв� qw вс��е�а��ся в tw по по�ядк�
//   3. Levenshtein на полн�� слова� (если длин� близки)
//   4. Prefix fuzzy: с�авниваем qw с на�алом tw �ой же длин� �1
//      � лови� "клива" � "клипбо�д" (пе�в�е 3 б�кв� совпада��)
static bool wordFuzzyMatch(const QString &qw, const QString &tw, int maxDist)
{
    // 1. �одс��ока
    if (tw.contains(qw)) return true;

    // 2. Subsequence
    if (isSubsequence(qw, tw)) return true;

    // 3. Levenshtein на полн�� слова�
    if (std::abs(tw.size() - qw.size()) <= maxDist + 1 &&
        levenshtein(qw, tw) <= maxDist) return true;

    // 4. Prefix fuzzy: qw с�авнивае�ся с п�е�иксом tw �ол�ко если tw заме�но длиннее
    //    (пол�зова�ел� вв�л на�ало длинного слова, как "клива" � "клипбо�д").
    //    �сли слова одинаковой длин� � prefix не н�жен, ина�е ложн�е совпадения.
    if (tw.size() > qw.size() + 1) {
        int lo = std::max(2, (int)qw.size() - 1);
        int hi = std::min((int)qw.size() + 1, (int)tw.size());
        for (int plen = lo; plen <= hi; ++plen) {
            if (levenshtein(qw, tw.left(plen)) <= maxDist) return true;
        }
    }

    return false;
}

// Умн�й не���кий поиск зап�оса в �екс�е.
// �аждое слово зап�оса (� 2 б�кв) должно най�и совпадение �о�� в одном слове �екс�а.
// �оп�ск о�ибок по длине слова:
//   2 б�кв:  0 (�ол�ко �о�но)
//   3�4 б�кв: 1 о�ибка
//   5�7 б�кв: 2 о�ибки
//   8+ б�кв:  3 о�ибки
static bool fuzzyMatch(const QString &text, const QString &query)
{
    if (query.isEmpty()) return true;

    const QString tl = text.toLower();
    const QString ql = query.toLower();

    // ��с���й п���: �о�ное в�ождение всего зап�оса
    if (tl.contains(ql)) return true;

    const QStringList queryWords = ql.split(' ', Qt::SkipEmptyParts);
    const QStringList textWords  = tl.split(QRegularExpression("[\\s\\-_/\\\\.,;:]+"),
                                             Qt::SkipEmptyParts);

    for (const QString &qw : queryWords) {
        if (qw.size() <= 1) continue; // одино�н�е символ� п�оп�скаем

        int maxDist;
        if      (qw.size() >= 8) maxDist = 3;
        else if (qw.size() >= 5) maxDist = 2;
        else if (qw.size() >= 3) maxDist = 1;
        else                     maxDist = 0;

        bool found = false;
        for (const QString &tw : textWords) {
            if (wordFuzzyMatch(qw, tw, maxDist)) { found = true; break; }
        }
        if (!found) return false;
    }
    return !queryWords.isEmpty();
}

// �� �одсве�ка совпадений поиска ���������������������������������������������
// ��е� слова зап�оса в plain-�екс�е ка��о�ки и �ис�е� ж�л��й <span>.
// ��и п�с�ом query восс�анавливае� об��н�й plain text.
static void applyHighlight(QLabel *card, const QString &query)
{
    QString plain = card->property("displayText").toString();
    if (plain.isEmpty()) return;

    if (query.isEmpty()) {
        card->setTextFormat(Qt::PlainText);
        card->setText(plain);
        return;
    }

    // Соби�аем пози�ии п�ям�� совпадений каждого слова зап�оса
    QString lower = plain.toLower();
    QStringList queryWords = query.toLower().split(' ', Qt::SkipEmptyParts);
    QList<QPair<int,int>> hits; // {start, length}

    for (const QString &qw : queryWords) {
        if (qw.size() < 2) continue;
        int pos = 0, idx;
        while ((idx = lower.indexOf(qw, pos)) != -1) {
            hits.append({idx, (int)qw.size()});
            pos = idx + qw.size();
        }
    }

    if (hits.isEmpty()) {
        card->setTextFormat(Qt::PlainText);
        card->setText(plain);
        return;
    }

    // Со��и��ем и ме�джим пе�есека��иеся ин�е�вал�
    std::sort(hits.begin(), hits.end());
    QList<QPair<int,int>> merged;
    for (auto &h : hits) {
        if (!merged.isEmpty() && h.first < merged.last().first + merged.last().second)
            merged.last().second = std::max(merged.last().second,
                                            h.first + h.second - merged.last().first);
        else
            merged.append(h);
    }

    // С��оим HTML � \n � <br>, спе�символ� эк�ани��ем
    QString html;
    int pos = 0;
    for (auto &[start, len] : merged) {
        html += plain.mid(pos, start - pos).toHtmlEscaped().replace('\n', "<br>");
        html += "<span style='background:#e8b800;color:#111;border-radius:2px;"
                "padding:0 1px;'>";
        html += plain.mid(start, len).toHtmlEscaped();
        html += "</span>";
        pos = start + len;
    }
    html += plain.mid(pos).toHtmlEscaped().replace('\n', "<br>");

    card->setTextFormat(Qt::RichText);
    card->setText(html);
}

// �� �с�авляе� мягкие пе�енос� (\n) вн���и длинн�� "слов" без п�обелов.
// �омае� �ол�ко после на���ал�н�� �аздели�елей: / \ _ - . : ��об� не �еза�� посе�едине �окена.
static QString softWrap(const QString &text, int threshold = 28)
{
    static const QString breakAfter = QStringLiteral("\\/._-:,;@");
    QString result;
    result.reserve(text.size() + 16);
    int segLen = 0;

    for (int i = 0; i < text.size(); ++i) {
        QChar c = text[i];
        result += c;

        if (c == ' ' || c == '\n' || c == '\t') {
            segLen = 0;
        } else {
            ++segLen;
            // �сли сегмен� �же длинн�й и �ек��ий символ � �о�о�ее мес�о для �аз��ва
            if (segLen >= threshold && breakAfter.contains(c)) {
                result += '\n';
                segLen = 0;
            }
        }
    }
    return result;
}

MainWindow::MainWindow(Database *db, QWidget *parent)
    : QWidget(parent)
    , m_db(db)
    , m_leftLayout(nullptr)
    , m_rightLayout(nullptr)
    , m_leftScroll(nullptr)
    , m_rightScroll(nullptr)
    , m_topPanel(nullptr)
    , m_bottomPanel(nullptr)
    , m_centerSpacer(nullptr)
    , m_searchEdit(nullptr)
{
    setObjectName("mainWindow");
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setupLayout();

    m_imageViewer = new ImageViewer(this);

    // Тихая проверка обновлений через 3 сек после старта
    QTimer::singleShot(3000, this, [this]() {
        auto *uc = new UpdateChecker(this);
        connect(uc, &UpdateChecker::updateAvailable,
                this, &MainWindow::onUpdateAvailable);
        uc->check(/*silent=*/true);
    });
}


void MainWindow::setupLayout()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // �� �е��няя панел� �������������������������������������������������������
    m_topPanel = new QWidget(this);
    m_topPanel->setObjectName("topPanel");
    m_topPanel->setFixedHeight(TOP_HEIGHT);;
    QHBoxLayout *topLayout = new QHBoxLayout(m_topPanel);
    topLayout->setContentsMargins(12, 8, 12, 18);
    topLayout->setSpacing(8);

    // Группа «переключатели вида» — слева, одинаковая ширина по самому длинному
    const int BTN_H = 32;
    QFont btnFont = font();
    btnFont.setPixelSize(13);

    m_historyBtn = new SmartButton(tr("История"), m_topPanel);
    m_historyBtn->setFixedHeight(BTN_H);
    m_historyBtn->setFont(btnFont);
    m_historyBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,150), 8);
    m_historyBtn->setActiveBtnStyle(QColor(40,40,58,245), Qt::white);
    m_historyBtn->setActiveState(true);
    applyBtnShadow(m_historyBtn);

    m_pinsBtn = new SmartButton(tr("⭐ Закрепы"), m_topPanel);
    m_pinsBtn->setFixedHeight(BTN_H);
    m_pinsBtn->setFont(btnFont);
    m_pinsBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,150), 8);
    m_pinsBtn->setActiveBtnStyle(QColor(40,40,58,245), Qt::white);
    m_pinsBtn->setActiveState(false);
    applyBtnShadow(m_pinsBtn);

    connect(m_historyBtn, &QPushButton::clicked, this, [this]() { switchView(ViewMode::History); });
    connect(m_pinsBtn,    &QPushButton::clicked, this, [this]() { switchView(ViewMode::Pins);    });

    // Группа «правые действия»
    m_profilesBtn = new SmartButton(tr("📋  Профили"), m_topPanel);
    m_profilesBtn->setFixedHeight(BTN_H);
    m_profilesBtn->setFont(btnFont);
    m_profilesBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,160), 6);
    applyBtnShadow(m_profilesBtn);
    connect(m_profilesBtn, &QPushButton::clicked, this, &MainWindow::showProfilesMenu);

    m_sortBtn = new SmartButton(sortLabel(), m_topPanel);
    m_sortBtn->setFixedHeight(BTN_H);
    m_sortBtn->setFont(btnFont);
    m_sortBtn->setMinimumWidth(145);
    m_sortBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,160), 6);
    applyBtnShadow(m_sortBtn);
    connect(m_sortBtn, &QPushButton::clicked, this, &MainWindow::showSortMenu);

    m_clearBtn = new SmartButton(tr("✕  Очистить всё"), m_topPanel);
    m_clearBtn->setFixedHeight(BTN_H);
    m_clearBtn->setFont(btnFont);
    m_clearBtn->setBtnStyle(QColor(55,12,12,245), QColor(255,130,130,220), 6);
    applyBtnShadow(m_clearBtn);
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::showClearMenu);

    // Иконка настроек — квадратная
    m_settingsBtn = new SmartButton("⚙️", m_topPanel);
    m_settingsBtn->setFixedSize(BTN_H, BTN_H);
    m_settingsBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,160), 6);
    m_settingsBtn->setToolTip(tr("Настройки"));
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    applyBtnShadow(m_settingsBtn);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::showSettings);

    // Кнопка режима мультивыбора — видна только в Закрепах
    m_editBtn = new SmartButton(tr("✏"), m_topPanel);
    m_editBtn->setFixedSize(BTN_H, BTN_H);
    m_editBtn->setFont(btnFont);
    m_editBtn->setBtnStyle(QColor(20,20,32,245), QColor(255,255,255,150), 8);
    m_editBtn->setActiveBtnStyle(QColor(80,60,10,245), QColor(255,220,80));
    m_editBtn->setActiveState(false);
    m_editBtn->setToolTip(tr("Режим выбора нескольких закрепов"));
    m_editBtn->setCursor(Qt::PointingHandCursor);
    m_editBtn->setVisible(false);
    applyBtnShadow(m_editBtn);
    connect(m_editBtn, &QPushButton::clicked, this, &MainWindow::toggleEditMode);

    topLayout->addWidget(m_historyBtn);
    topLayout->addWidget(m_pinsBtn);
    topLayout->addStretch();
    topLayout->addWidget(m_editBtn);
    topLayout->addWidget(m_profilesBtn);
    topLayout->addWidget(m_sortBtn);
    topLayout->addWidget(m_clearBtn);
    topLayout->addWidget(m_settingsBtn);

    // �� �олоса папок (показ�вае�ся �ол�ко в �ежиме �ак�епов) �����������������
    m_foldersBar = new QWidget(this);
    m_foldersBar->setObjectName("foldersBar");
    m_foldersBar->setFixedHeight(FOLDERS_HEIGHT);;
    m_foldersLayout = new QHBoxLayout(m_foldersBar);
    m_foldersLayout->setContentsMargins(12, 6, 12, 6);
    m_foldersLayout->setSpacing(6);
    m_foldersLayout->setAlignment(Qt::AlignVCenter);
    m_foldersLayout->addStretch();
    m_foldersBar->hide(); // ск���а пока не пе�е�ли в �ежим �ак�епов

    // �� С�едняя зона ���������������������������������������������������������
    QHBoxLayout *middleLayout = new QHBoxLayout();
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);

    // �евая колонка � �екс��
    m_leftScroll = new QScrollArea(this);
    m_leftScroll->setObjectName("leftScroll");
    m_leftScroll->setFixedWidth(LEFT_WIDTH);
    m_leftScroll->setWidgetResizable(true);
    m_leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_leftScroll->setFocusPolicy(Qt::WheelFocus);
    m_leftScroll->viewport()->setStyleSheet("background: transparent;");
    QScrollArea *leftScroll = m_leftScroll;
    QWidget *leftContent = new QWidget();
    leftContent->setStyleSheet("background-color: transparent;");
    m_leftLayout = new QVBoxLayout(leftContent);
    m_leftLayout->setAlignment(Qt::AlignTop);
    m_leftLayout->setContentsMargins(14, 8, 14, 24); // �вели�ено для �ени (blurRadius=40)
    m_leftLayout->setSpacing(12);
    leftScroll->setWidget(leftContent);

    // ��оз�а�ная се�едина
    m_centerSpacer = new QWidget(this);
    m_centerSpacer->setStyleSheet("background-color: transparent;");
    m_centerSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QWidget *centerSpacer = m_centerSpacer;

    // ��авая колонка � ка��инки
    m_rightScroll = new QScrollArea(this);
    m_rightScroll->setObjectName("rightScroll");
    m_rightScroll->setFixedWidth(RIGHT_WIDTH);
    m_rightScroll->setWidgetResizable(true);
    m_rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_rightScroll->setFocusPolicy(Qt::WheelFocus);
    m_rightScroll->viewport()->setStyleSheet("background: transparent;");
    QScrollArea *rightScroll = m_rightScroll;
    QWidget *rightContent = new QWidget();
    rightContent->setStyleSheet("background-color: transparent;");
    m_rightLayout = new QVBoxLayout(rightContent);
    m_rightLayout->setAlignment(Qt::AlignTop);
    m_rightLayout->setContentsMargins(14, 8, 14, 24); // �вели�ено для �ени (blurRadius=40)
    m_rightLayout->setSpacing(12);
    rightScroll->setWidget(rightContent);

    middleLayout->addWidget(leftScroll);
    middleLayout->addWidget(centerSpacer);
    middleLayout->addWidget(rightScroll);

    // �� Нижняя панел� ��������������������������������������������������������
    m_bottomPanel = new QWidget(this);
    m_bottomPanel->setObjectName("bottomPanel");
    m_bottomPanel->setFixedHeight(BOTTOM_HEIGHT);
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomPanel);
    bottomLayout->setContentsMargins(12, 6, 12, 14);
    auto *searchBar = new SearchBar(m_bottomPanel);
    applyBtnShadow(searchBar);
    m_searchEdit = searchBar->edit();
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->setPlaceholderText(tr("Поиск по тексту..."));
    bottomLayout->addWidget(searchBar);

    // ��и вводе � �ил����ем в зависимос�и о� �ек��его �ежима
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &q) {
        if (m_viewMode == ViewMode::History) {
            filterHistory(q);
        } else {
            // � �ежиме �ак�епов: если ес�� зап�ос � заг��жаем �С� папки и �ил����ем;
            // если зап�ос п�с� � возв�а�аемся к �ек��ей папке
            if (q.isEmpty())
                loadPins(false);
            else
                loadPins(true);
            filterPins(q);
        }
    });

    mainLayout->addWidget(m_topPanel);
    mainLayout->addWidget(m_foldersBar);   // ск���а по �мол�ани�
    mainLayout->addLayout(middleLayout);
    mainLayout->addWidget(m_bottomPanel);
}

void MainWindow::loadHistory()
{
    // ��меняем незаве���нн�� ленив�� заг��зк� п�о�лого поколения
    ++m_loadGeneration;
    m_pendingImages.clear();

    // ��и�аем колонки пе�ед заг��зкой
    QLayoutItem *item;
    while ((item = m_leftLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    while ((item = m_rightLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    // �� Фаза 1: �екс�� � мгновенно ������������������������������������������
    QList<HistoryItem> texts = m_db->getHistoryByType("text", 40, 0);

    // ��именяем со��и�овк� (�� всегда возв�а�ае� DESC � п�и ASC п�ос�о �азво�а�иваем)
    if (m_sortOrder == SortOrder::DateAsc)
        std::reverse(texts.begin(), texts.end());
    for (const HistoryItem &entry : texts) {
        ClickableCard *card = new ClickableCard();
        applyCardShadow(card);
        card->setWordWrap(true);
        card->setMaximumWidth(LEFT_WIDTH - 36); // да�м Qt �в��д�� �и�ин� � длинн�е слова лома��ся
        card->setCardStyle(QColor(20, 20, 32, 245));
        card->setStyleSheet("color: #f0f0f0; padding: 8px; font-size: 12px;");

        QString display = entry.content;
        if (display.length() > 300)
            display = display.left(300) + "...";
        QString displayWrapped = softWrap(display);
        card->setText(displayWrapped);

        card->setProperty("fullText",    entry.content);
        card->setProperty("displayText", displayWrapped);
        card->setProperty("itemId",      entry.id);

        QString fullContent = entry.content;
        connect(card, &ClickableCard::clicked, this, [this, fullContent]() {
            pasteText(fullContent);
        });

        // ��ав�й клик � кон�екс�ное мен� (�ак�епи�� / Удали��)
        int textId = entry.id;
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QWidget::customContextMenuRequested, this,
                [this, textId, fullContent, card](const QPoint &pos) {
            showTextContextMenu(textId, fullContent, card, card->mapToGlobal(pos));
        });

        m_leftLayout->addWidget(card);
    }

    // �� Фаза 2: ка��инки � сна�ала загл��ки, по�ом ленивая заг��зка ���������
    QList<HistoryItem> images = m_db->getHistoryByType("image", 40, 0);

    if (m_sortOrder == SortOrder::DateAsc)
        std::reverse(images.begin(), images.end());
    for (const HistoryItem &entry : images) {
        ClickableCard *card = new ClickableCard();
        applyCardShadow(card);
        card->setFixedSize(LEFT_WIDTH - 36, 120);
        card->setAlignment(Qt::AlignCenter);
        // Се�ая загл��ка � о�об�ажае�ся мгновенно, до заг��зки �еал�ного �айла
        card->setCardStyle(QColor(20, 20, 32, 245));
        card->setStyleSheet("color: rgba(255,255,255,60); font-size: 20px;");
        card->setText("");  // ней��ал�н�й плейс�олде�

        card->setProperty("filepath", entry.filepath);
        card->setProperty("itemId", entry.id);

        QString fp = entry.filepath;
        connect(card, &ClickableCard::clicked, this, [this, fp]() {
            pasteImage(fp);
        });

        // ��ав�й клик � кон�екс�ное мен� (��осмо��е�� / �ак�епи�� / Удали��)
        int imgId = entry.id;
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QWidget::customContextMenuRequested, this,
                [this, imgId, fp, card](const QPoint &pos) {
            showImageContextMenu(imgId, fp, card, card->mapToGlobal(pos));
        });

        m_rightLayout->addWidget(card);

        // �обавляем в о�е�ед� ленивой заг��зки
        m_pendingImages.append({QPointer<QLabel>(card), fp});
    }

    // �� Фаза 3: видео из папки мони�о�инга ����������������������������������
    QList<HistoryItem> videos = m_db->getHistoryByType("video", 40, 0);
    if (m_sortOrder == SortOrder::DateAsc)
        std::reverse(videos.begin(), videos.end());

    for (const HistoryItem &entry : videos) {
        ClickableCard *card = new ClickableCard();
        applyCardShadow(card);
        card->setFixedSize(RIGHT_WIDTH - 36, 72);
        card->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        card->setWordWrap(true);
        card->setCardStyle(QColor(10, 20, 48, 245));
        card->setStyleSheet("color: #aaccff; padding: 8px; font-size: 12px;");

        QString name = entry.content.isEmpty()
            ? QFileInfo(entry.filepath).fileName()
            : entry.content;
        if (name.length() > 50) name = name.left(47) + "...";
        card->setText(QString("🎬  %1").arg(name));
        card->setProperty("pinName", name);

        QString fp = entry.filepath;
        connect(card, &ClickableCard::clicked, this, [this, fp]() {
            pasteFile(fp);
        });

        int vidId = entry.id;
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QWidget::customContextMenuRequested, this,
                [this, vidId, fp, card](const QPoint &pos) {
            RoundedMenu menu(this);
            menu.addAction(tr("Поиск по названию..."), [this, fp]() {
                bool ok = false;
                QString name = QInputDialog::getText(
                    this, tr("Изображение"), tr("Новые сначала"),
                    QLineEdit::Normal, QFileInfo(fp).baseName(), &ok);
                if (!ok || name.trimmed().isEmpty()) return;
                QString folder = "";
                if (!AppSettings::get().pinsNoFolder()) {
                    bool cancelled = false;
                    folder = askFolder(cancelled);
                    if (cancelled) return;
                }
                m_db->addPin(folder, name.trimmed(), "video", "", fp);
            });
            menu.addSeparator();
            menu.addAction(tr("Старые сначала"), [this, vidId, card]() {
                deleteCard(vidId, card);
            });
            menu.exec(card->mapToGlobal(pos));
        });

        m_rightLayout->addWidget(card);
    }

    // �ап�скаем ленив�� заг��зк� (о�да�м �п�авление в event loop после каждого �айла)
    if (!m_pendingImages.isEmpty()) {
        int gen = m_loadGeneration;
        QTimer::singleShot(0, this, [this, gen]() { loadNextImage(gen); });
    }
}

void MainWindow::loadNextImage(int gen)
{
    // �сли loadHistory() в�звали снова � поколение сменилос�, п�е��ваемся
    if (gen != m_loadGeneration || m_pendingImages.isEmpty())
        return;

    auto [cardPtr, filepath] = m_pendingImages.takeFirst();

    // QPointer<QLabel> ав�ома�и�ески �авен nullptr если видже� б�л �дал�н
    if (cardPtr) {
        QPixmap pixmap(filepath);
        if (!pixmap.isNull()) {
            cardPtr->setPixmap(pixmap.scaled(
                cardPtr->width() - 8, cardPtr->height() - 8,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            ));
            cardPtr->setStyleSheet(""); // �би�аем плейс�олде� "�"
        } else {
            cardPtr->setText(tr("Изображение"));
            cardPtr->setStyleSheet("color: rgba(255,255,255,80); font-size: 12px;");
        }
    }

    // След���ая ка��инка � на след���ей и�е�а�ии event loop
    if (!m_pendingImages.isEmpty()) {
        QTimer::singleShot(0, this, [this, gen]() { loadNextImage(gen); });
    }
}

void MainWindow::showWindow()
{
    QScreen *screen = QApplication::primaryScreen();
    setGeometry(screen->geometry());

    // Запоминаем окно, которое было активным ДО нас — вернём ему фокус при вставке
    m_prevFocusHwnd = reinterpret_cast<quintptr>(GetForegroundWindow());

    // Сб�ас�ваем поиск
    if (m_searchEdit)
        m_searchEdit->clear();

    // �оказ�ваем окно СРА�У � пол�зова�ел� не жд�� пока заг��зя�ся данн�е
    show();
    raise();
    activateWindow();


    // �бновляем данн�е если н�жно
    if (m_viewMode == ViewMode::History && m_historyDirty) {
        loadHistory();
        m_historyDirty = false;
    } else if (m_viewMode == ViewMode::Pins) {
        loadPins(); // зак�епов об��но мало � всегда пе�ег��жаем
    }
}

void MainWindow::markHistoryDirty()
{
    m_historyDirty = true;
}

void MainWindow::filterHistory(const QString &query)
{
    for (int i = 0; i < m_leftLayout->count(); ++i) {
        QWidget *widget = m_leftLayout->itemAt(i)->widget();
        if (!widget) continue;
        QString fullText = widget->property("fullText").toString();

        if (query.isEmpty()) {
            widget->show();
        } else {
            widget->setVisible(fuzzyMatch(fullText, query));
        }

        // �одсве�ка совпадений в �екс�е (�ол�ко � ка��о�ек с displayText)
        if (widget->isVisible()) {
            if (QLabel *label = qobject_cast<QLabel*>(widget))
                applyHighlight(label, query);
        }
    }
}

void MainWindow::filterPins(const QString &query)
{
    bool tagsOnly = query.startsWith('#');
    QString effectiveQuery = tagsOnly ? query.mid(1).trimmed() : query;

    auto filterLayout = [&](QVBoxLayout *layout) {
        for (int i = 0; i < layout->count(); ++i) {
            QWidget *widget = layout->itemAt(i)->widget();
            if (!widget) continue;
            QString name = widget->property("pinName").toString();
            QString tags = widget->property("pinTags").toString();

            if (effectiveQuery.isEmpty()) {
                widget->show();
            } else {
                bool matchesName = !tagsOnly && (name.isEmpty() || fuzzyMatch(name, effectiveQuery));
                bool matchesTags = !tags.isEmpty() && fuzzyMatch(tags, effectiveQuery);
                widget->setVisible(matchesName || matchesTags);
            }

            // �одсве�ка в �екс�ов�� пин-ка��о�ка�
            if (widget->isVisible()) {
                if (QLabel *label = qobject_cast<QLabel*>(widget))
                    applyHighlight(label, effectiveQuery);
            }
        }
    };
    filterLayout(m_leftLayout);
    filterLayout(m_rightLayout);
}

bool MainWindow::isInteractiveArea(const QPoint &pos) const
{
    // childAt возв�а�ае� сам�й гл�бокий до�е�ний видже� в э�ой �о�ке
    QWidget *child = childAt(pos);

    // �сли видже�а не� или э�о п�оз�а�н�й �ен�� � клик п�оваливае�ся
    if (!child || child == m_centerSpacer)
        return false;

    return true;
}

void MainWindow::pasteText(const QString &text)
{
    // 1. Сообщаем ClipboardManager: следующее WM_CLIPBOARDUPDATE — наше, не писать в историю
    emit pasteRequested();

    // 2. Кладём текст в системный буфер обмена
    QApplication::clipboard()->setText(text);

    // 3. Возвращаем фокус окну, которое было активным ДО SmartClip
    //    (делаем это синхронно, пока наш процесс ещё является foreground-процессом)
    HWND target = reinterpret_cast<HWND>(m_prevFocusHwnd);
    hide();
    if (target && IsWindow(target))
        SetForegroundWindow(target);

    // 4. Ждём 150мс пока фокус успеет перейти, затем симулируем Ctrl+V
    QTimer::singleShot(150, []() {
        INPUT inputs[4] = {};

        inputs[0].type   = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_CONTROL;           // нажать Ctrl

        inputs[1].type   = INPUT_KEYBOARD;
        inputs[1].ki.wVk = 'V';                  // нажать V

        inputs[2].type         = INPUT_KEYBOARD;
        inputs[2].ki.wVk       = 'V';            // отпустить V
        inputs[2].ki.dwFlags   = KEYEVENTF_KEYUP;

        inputs[3].type         = INPUT_KEYBOARD;
        inputs[3].ki.wVk       = VK_CONTROL;     // отпустить Ctrl
        inputs[3].ki.dwFlags   = KEYEVENTF_KEYUP;

        SendInput(4, inputs, sizeof(INPUT));
    });
}

void MainWindow::pasteImage(const QString &filepath)
{
    // �аг��жаем ка��инк� из �айла
    QPixmap pixmap(filepath);
    if (pixmap.isNull()) {
        qDebug() << "pasteImage: не �далос� заг��зи��" << filepath;
        return;
    }

    // Аналогично тексту — подавляем запись в историю
    emit pasteRequested();

    // Qt сам конвертирует QPixmap в нужный формат буфера
    QApplication::clipboard()->setPixmap(pixmap);

    HWND target = reinterpret_cast<HWND>(m_prevFocusHwnd);
    hide();
    if (target && IsWindow(target))
        SetForegroundWindow(target);

    QTimer::singleShot(150, []() {
        INPUT inputs[4] = {};
        inputs[0].type   = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type   = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
        inputs[2].type   = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type   = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));
    });
}

void MainWindow::pasteFile(const QString &filepath)
{
    // �лад�м �айл в б��е� как CF_HDROP (как п�и копи�овании �айла в п�оводнике).
    // Э�о позволяе� вс�ави�� �айл �е�ез Ctrl+V в Telegram, Discord, по��� и �.д.
    QString nativePath = QDir::toNativeSeparators(filepath);
    std::wstring wPath = nativePath.toStdWString();

    // Разме� б��е�а: SC_DROPFILES + п��� (UTF-16) + двойной н�л�-�е�мина�о�
    size_t pathBytes = (wPath.size() + 2) * sizeof(wchar_t); // п��� + 2x '\0'
    size_t bufSize   = sizeof(SC_DROPFILES) + pathBytes;

    HGLOBAL hGlobal = GlobalAlloc(GHND, bufSize);
    if (!hGlobal) return;

    SC_DROPFILES *pDrop = static_cast<SC_DROPFILES*>(GlobalLock(hGlobal));
    pDrop->pFiles = sizeof(SC_DROPFILES); // сме�ение до пе�вого п��и
    pDrop->fWide  = TRUE;                 // п��и в UTF-16
    pDrop->pt     = {0, 0};
    pDrop->fNC    = FALSE;

    wchar_t *pPaths = reinterpret_cast<wchar_t*>(
        reinterpret_cast<BYTE*>(pDrop) + sizeof(SC_DROPFILES));
    wmemcpy(pPaths, wPath.c_str(), wPath.size() + 1); // п��� + н�л�
    pPaths[wPath.size() + 1] = L'\0';                  // в�о�ой н�л� = коне� списка

    GlobalUnlock(hGlobal);

    emit pasteRequested();

    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        SetClipboardData(CF_HDROP, hGlobal);
        CloseClipboard();
    } else {
        GlobalFree(hGlobal);
        return;
    }

    HWND target = reinterpret_cast<HWND>(m_prevFocusHwnd);
    hide();
    if (target && IsWindow(target))
        SetForegroundWindow(target);

    QTimer::singleShot(150, []() {
        INPUT inputs[4] = {};
        inputs[0].type   = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type   = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
        inputs[2].type   = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V';
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type   = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));
    });
}

void MainWindow::importVideo()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Импорт видео"), "",
        tr("Видео файлы (*.mp4 *.mkv *.mov *.avi *.wmv *.webm)")
    );
    if (files.isEmpty()) return;

    for (const QString &fp : files) {
        bool ok = false;
        QString name = QInputDialog::getText(
            this, tr("Закрепить видео"),
            tr("Название закрепа:"),
            QLineEdit::Normal, QFileInfo(fp).baseName(), &ok
        );
        if (!ok) continue;

        QString folder = "";
        if (!AppSettings::get().pinsNoFolder()) {
            bool cancelled = false;
            folder = askFolder(cancelled);
            if (cancelled) continue;
        }
        m_db->addPin(folder, name.trimmed(), "video", "", fp);
    }
    loadPins();
}

void MainWindow::importAudio()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Импорт аудио"), "",
        tr("Аудио файлы (*.mp3 *.wav *.ogg *.flac *.aac *.m4a)")
    );
    if (files.isEmpty()) return;

    for (const QString &fp : files) {
        bool ok = false;
        QString name = QInputDialog::getText(
            this, tr("Закрепить видео"),
            tr("Название закрепа:"),
            QLineEdit::Normal, QFileInfo(fp).baseName(), &ok
        );
        if (!ok) continue;

        QString folder = "";
        if (!AppSettings::get().pinsNoFolder()) {
            bool cancelled = false;
            folder = askFolder(cancelled);
            if (cancelled) continue;
        }
        m_db->addPin(folder, name.trimmed(), "audio", "", fp);
    }
    loadPins();
}

void MainWindow::showTextContextMenu(int id, const QString &content,
                                     QLabel *card, const QPoint &globalPos)
{
    RoundedMenu menu(this);

    menu.addAction(tr("📌  Закрепить"), [this, content]() { pinText(content); });
    menu.addSeparator();
    menu.addAction(tr("🗑  Удалить"),   [this, id, card]() { deleteCard(id, card); });

    menu.exec(globalPos);
}

void MainWindow::showImageContextMenu(int id, const QString &filepath,
                                      QLabel *card, const QPoint &globalPos)
{
    RoundedMenu menu(this);

    menu.addAction(tr("🔍  Просмотреть"), [this, filepath]() { showImageViewer(filepath); });
    menu.addSeparator();
    menu.addAction(tr("📌  Закрепить"),   [this, filepath]() { pinImage(filepath); });
    menu.addSeparator();
    menu.addAction(tr("🗑  Удалить"),     [this, id, card]() { deleteCard(id, card); });

    menu.exec(globalPos);
}

void MainWindow::pinText(const QString &content)
{
    QString name;

    if (AppSettings::get().pinsNoName()) {
        // �ез имени � п�с�ая с��ока, с�аз� в�би�аем папк�
    } else {
        bool ok = false;
        name = askText(this, tr("Закрепить текст"), tr("Название:"), content.trimmed().left(40), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
    }

    QString folder = "";
    if (!AppSettings::get().pinsNoFolder()) {
        bool cancelled = false;
        folder = askFolder(cancelled);
        if (cancelled) return;
    }

    m_db->addPin(folder, name, "text", content, "");
}

void MainWindow::pinImage(const QString &filepath)
{
    QString name = "";
    if (!AppSettings::get().pinsNoName()) {
        bool ok = false;
        name = askText(this, tr("Закрепить скриншот"), tr("Название:"), "", &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        name = name.trimmed();
    }

    QString folder = "";
    if (!AppSettings::get().pinsNoFolder()) {
        bool cancelled = false;
        folder = askFolder(cancelled);
        if (cancelled) return;
    }

    m_db->addPin(folder, name, "image", "", filepath);
}

void MainWindow::deleteCard(int id, QLabel *card)
{
    // Удаляем из ��
    m_db->deleteHistory(id);

    // Удаляем видже� из лейа��а и памя�и
    // deleteLater() � безопасно, даже если на ка��о�к� ес�� pending соб��ия
    if (card) {
        // �п�еделяем в каком лейа��е лежи� ка��о�ка и �би�аем о���да
        QVBoxLayout *layout = nullptr;
        if (m_leftLayout->indexOf(card) != -1)
            layout = m_leftLayout;
        else if (m_rightLayout->indexOf(card) != -1)
            layout = m_rightLayout;

        if (layout)
            layout->removeWidget(card);

        card->deleteLater();
    }
}

void MainWindow::showImageViewer(const QString &filepath)
{
    m_imageViewer->showImage(filepath);
}

bool MainWindow::confirmTwice(const QString &title,
                              const QString &msg1, const QString &msg2)
{
    if (!confirmDialog(this, title, msg1, false)) return false;
    return confirmDialog(this, title, msg2, true);
}

void MainWindow::showClearMenu()
{
    RoundedMenu menu(this);

    // � �ежиме ис�о�ии показ�ваем оп�ии о�ис�ки ис�о�ии
    if (m_viewMode == ViewMode::History) {
        int totalTexts  = m_db->countHistory("text");
        int totalImages = m_db->countHistory("image");

        menu.addAction(
            tr("Очистить тексты  (%1)").arg(totalTexts),
            [this]() { clearHistory("text"); }
        );
        menu.addAction(
            tr("Очистить скриншоты  (%1)").arg(totalImages),
            [this]() { clearHistory("image"); }
        );
        menu.addSeparator();
        menu.addAction(
            tr("Очистить всю историю  (%1)").arg(totalTexts + totalImages),
            [this]() { clearHistory(""); }
        );
        menu.addSeparator();
        menu.addAction(tr("Очистить по приложению..."), [this]() { clearHistoryByApp(); });
    } else {
        menu.addAction(tr("(Удаление закрепов — через ПКМ по карточке)"))->setEnabled(false);
    }

    menu.exec(m_clearBtn->mapToGlobal(QPoint(0, m_clearBtn->height() + 6)));
}

void MainWindow::clearHistory(const QString &type)
{
    QString what = type.isEmpty() ? tr("всю историю")
                 : (type == "text" ? tr("историю текстов") : tr("историю скриншотов"));
    int count = m_db->countHistory(type);

    if (!confirmTwice(
        tr("Очистить %1").arg(what),
        tr("Удалить %1?\nЭто действие нельзя отменить.").arg(what),
        tr("Вы точно уверены?\nБудет безвозвратно удалено %1 записей.").arg(count)
    )) return;

    m_db->deleteAllHistory(type);
    m_historyDirty = true;
    loadHistory();
}

void MainWindow::clearHistoryByApp()
{
    QStringList apps = m_db->getAppNames();
    if (apps.isEmpty()) {
        confirmDialog(this, tr("Очистить по приложению"),
                      tr("История пуста или приложения не определены.\nДанные об источнике появятся у новых записей."), false);
        return;
    }

    bool ok = false;
    QString app = askItem(this, tr("Очистить по приложению"),
                          tr("Выберите приложение:"), apps, 0, &ok);
    if (!ok || app.isEmpty()) return;

    int count = m_db->countHistoryByApp(app);

    if (!confirmTwice(
        tr("Очистить историю %1").arg(app),
        tr("Удалить все записи из %1?\nЭто действие нельзя отменить.").arg(app),
        tr("Вы точно уверены?\nБудет удалено %1 записей из %2.").arg(count).arg(app)
    )) return;

    m_db->deleteHistoryByApp(app);
    m_historyDirty = true;
    loadHistory();
}

QString MainWindow::sortLabel() const
{
    switch (m_sortOrder) {
        case SortOrder::DateDesc: return tr("Новые сначала");
        case SortOrder::DateAsc:  return tr("Старые сначала");
        case SortOrder::NameAsc:  return tr("По имени А→Я");
        case SortOrder::NameDesc: return tr("По имени Я→А");
    }
    return "↕";
}

void MainWindow::showSortMenu()
{
    RoundedMenu menu(this);

    // ��нк� с гало�кой � ак�ивного ва�иан�а
    auto addSortAction = [&](const QString &label, SortOrder order) {
        QString text = (m_sortOrder == order) ? "✓  " + label : "    " + label;
        menu.addAction(text, [this, order]() { applySortOrder(order); });
    };

    addSortAction(tr("Новые сначала"),  SortOrder::DateDesc);
    addSortAction(tr("Старые сначала"), SortOrder::DateAsc);

    if (m_viewMode == ViewMode::Pins) {
        menu.addSeparator();
        addSortAction(tr("По имени А→Я"), SortOrder::NameAsc);
        addSortAction(tr("По имени Я→А"), SortOrder::NameDesc);
    }

    // �оказ�ваем мен� под кнопкой
    menu.exec(m_sortBtn->mapToGlobal(QPoint(0, m_sortBtn->height() + 6)));
}

void MainWindow::applySortOrder(SortOrder order)
{
    m_sortOrder = order;

    // �бновляем �екс� кнопки
    if (m_sortBtn)
        m_sortBtn->setText(sortLabel());

    // �е�езаг��жаем �ек��ий вид с новой со��и�овкой
    if (m_viewMode == ViewMode::History)
        loadHistory();
    else
        loadPins();
}

void MainWindow::loadFolderBar()
{
    // ��и�аем все видже�� полос� папок
    QLayoutItem *item;
    while ((item = m_foldersLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    const int FH = 28;
    QFont fbarFont = font();
    fbarFont.setPixelSize(12);

    if (!m_currentFolder.isEmpty()) {
        // �� �н���и папки: кнопка "�" + �лебная к�о�ка �����������������������
        SmartButton *backBtn = new SmartButton("←", m_foldersBar);
        backBtn->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,150), 5);
        backBtn->setFixedSize(FH, FH);
        backBtn->setFont(fbarFont);
        backBtn->setToolTip(tr("Назад"));
        applyBtnShadow(backBtn);
        connect(backBtn, &QPushButton::clicked, this, [this]() {
            int sep = m_currentFolder.lastIndexOf('/');
            m_currentFolder = (sep >= 0) ? m_currentFolder.left(sep) : "";
            loadFolderBar();
            loadPins();
        });
        m_foldersLayout->addWidget(backBtn);

        // Хлебная к�о�ка — SmartButton в активном стиле (показывает текущий путь)
        QString leafName = m_currentFolder.mid(m_currentFolder.lastIndexOf('/') + 1);
        SmartButton *breadcrumb = new SmartButton("📁 " + leafName, m_foldersBar);
        breadcrumb->setFixedHeight(FH);
        breadcrumb->setFont(fbarFont);
        breadcrumb->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,120), 5);
        breadcrumb->setActiveBtnStyle(QColor(48,33,6,245), QColor(0xFF,0xDD,0x88));
        breadcrumb->setActiveState(true);
        applyBtnShadow(breadcrumb);
        connect(breadcrumb, &QPushButton::clicked, this, [this]() {
            loadFolderBar(); loadPins();
        });
        m_foldersLayout->addWidget(breadcrumb);

    } else {
        // �� �о�ен�: кнопка "Все" ���������������������������������������������
        SmartButton *allBtn = new SmartButton(tr("Все"), m_foldersBar);
        allBtn->setFixedHeight(FH);
        allBtn->setFont(fbarFont);
        allBtn->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,150), 5);
        allBtn->setActiveBtnStyle(QColor(48,33,6,245), QColor(0xFF,0xDD,0x88));
        allBtn->setActiveState(true);
        applyBtnShadow(allBtn);
        connect(allBtn, &QPushButton::clicked, this, [this]() {
            m_currentFolder = "";
            loadFolderBar();
            loadPins();
        });
        m_foldersLayout->addWidget(allBtn);
    }

    // �� �одпапки �ек��его ��овня ����������������������������������������������
    QList<FolderItem> folders = m_db->getFolders(m_currentFolder);
    for (const FolderItem &folder : folders) {
        // �оказ�ваем �ол�ко лис�овое имя (без �оди�ел�ского п��и)
        QString displayName = folder.name.mid(folder.name.lastIndexOf('/') + 1);
        QString fullName    = folder.name;
        // �ак�епл�нн�е папки поме�аем б�лавкой
        QString btnLabel = folder.pinned ? ("📌 " + displayName) : displayName;

        SmartButton *btn = new SmartButton(btnLabel, m_foldersBar);
        btn->setFixedHeight(FH);
        btn->setFont(fbarFont);
        btn->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,150), 5);
        btn->setActiveBtnStyle(QColor(48,33,6,245), QColor(0xFF,0xDD,0x88));
        btn->setActiveState(folder.name == m_currentFolder);
        applyBtnShadow(btn);

        // �лик � навига�ия вгл�б�
        connect(btn, &QPushButton::clicked, this, [this, fullName]() {
            m_currentFolder = fullName;
            loadFolderBar();
            loadPins();
        });

        // ���: �ак�епи�� / �е�еименова�� / Созда�� подпапк� / Удали��
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QWidget::customContextMenuRequested, this,
                [this, fullName, displayName, btn, isPinned = folder.pinned](const QPoint &pos) {
            RoundedMenu menu(this);

            // �ак�епи�� / ��к�епи�� папк� (всегда пе�вая)
            QString pinLabel = isPinned ? tr("📌  Открепить папку") : tr("📌  Закрепить первой");
            menu.addAction(pinLabel, [this, fullName, isPinned]() {
                m_db->setFolderPinned(fullName, !isPinned);
                loadFolderBar();
            });
            menu.addSeparator();

            menu.addAction(tr("✏️  Переименовать"), [this, fullName, displayName]() {
                bool ok = false;
                QString newLeaf = askText(this, tr("Переименовать папку"), tr("Новое название:"), displayName, &ok);
                if (!ok || newLeaf.trimmed().isEmpty() || newLeaf.trimmed() == displayName) return;
                // С��оим нов�й полн�й п��� (меняем �ол�ко лис�овое имя)
                int sep = fullName.lastIndexOf('/');
                QString newFullName = (sep >= 0)
                    ? fullName.left(sep + 1) + newLeaf.trimmed()
                    : newLeaf.trimmed();
                m_db->renameFolder(fullName, newFullName);
                if (m_currentFolder == fullName)
                    m_currentFolder = newFullName;
                loadFolderBar();
                loadPins();
            });

            menu.addAction(tr("📁  Создать подпапку"), [this, fullName]() {
                bool ok = false;
                QString name = askText(this, tr("Новая подпапка"), tr("Название подпапки:"), "", &ok);
                if (ok && !name.trimmed().isEmpty()) {
                    m_db->addFolder(name.trimmed(), fullName);
                    loadFolderBar();
                }
            });

            menu.addSeparator();
            menu.addAction(tr("🗑  Удалить папку"), [this, fullName]() {
                m_db->deleteFolder(fullName);
                if (m_currentFolder == fullName ||
                    m_currentFolder.startsWith(fullName + "/"))
                    m_currentFolder = "";
                loadFolderBar();
                loadPins();
            });

            menu.exec(btn->mapToGlobal(pos));
        });

        m_foldersLayout->addWidget(btn);
    }

    // �� �нопки импо��а видео и а�дио �����������������������������������������
    SmartButton *importVideoBtn = new SmartButton(tr("🎬 Видео"), m_foldersBar);
    importVideoBtn->setFixedHeight(FH);
    importVideoBtn->setFont(fbarFont);
    importVideoBtn->setBtnStyle(QColor(10,20,48,245), QColor(160,200,255,220), 5);
    importVideoBtn->setToolTip(tr("Импортировать видео в закрепы"));
    applyBtnShadow(importVideoBtn);
    connect(importVideoBtn, &QPushButton::clicked, this, &MainWindow::importVideo);
    m_foldersLayout->addWidget(importVideoBtn);

    SmartButton *importAudioBtn = new SmartButton(tr("🎵 Аудио"), m_foldersBar);
    importAudioBtn->setFixedHeight(FH);
    importAudioBtn->setFont(fbarFont);
    importAudioBtn->setBtnStyle(QColor(32,14,55,245), QColor(210,160,255,220), 5);
    importAudioBtn->setToolTip(tr("Импортировать аудио в закрепы"));
    applyBtnShadow(importAudioBtn);
    connect(importAudioBtn, &QPushButton::clicked, this, &MainWindow::importAudio);
    m_foldersLayout->addWidget(importAudioBtn);

    // Кнопка "+" — квадратная, как backBtn
    SmartButton *addBtn = new SmartButton("+", m_foldersBar);
    addBtn->setFixedSize(FH, FH);
    addBtn->setFont(fbarFont);
    addBtn->setBtnStyle(QColor(20,20,32,242), QColor(255,255,255,150), 5);
    addBtn->setToolTip(m_currentFolder.isEmpty() ? tr("Новая папка") : tr("Новая подпапка"));
    applyBtnShadow(addBtn);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        QString title = m_currentFolder.isEmpty() ? tr("Новая папка") : tr("Новая подпапка");
        QString name  = askText(this, title, tr("Название:"), "", &ok);
        if (ok && !name.trimmed().isEmpty()) {
            m_db->addFolder(name.trimmed(), m_currentFolder);
            loadFolderBar();
        }
    });
    m_foldersLayout->addWidget(addBtn);
    m_foldersLayout->addStretch();
}

QString MainWindow::askFolder(bool &cancelled)
{
    // getAllFolders() возв�а�ае� все папки вкл��ая подпапки (полн�е п��и)
    QList<FolderItem> folders = m_db->getAllFolders();

    if (folders.isEmpty()) {
        cancelled = false;
        return ""; // папок не� � клад�м без папки
    }

    QStringList options;
    options << tr("Без папки");
    for (const FolderItem &f : folders)
        options << f.name;

    int defaultIdx = 0;
    if (!m_currentFolder.isEmpty()) {
        int idx = options.indexOf(m_currentFolder);
        if (idx >= 0) defaultIdx = idx;
    }

    bool ok = false;
    QString selected = askItem(this, tr("Выбрать папку"),
                               tr("В какую папку закрепить?"), options, defaultIdx, &ok);

    cancelled = !ok;
    if (!ok || selected == tr("Без папки"))
        return "";
    return selected;
}

void MainWindow::switchView(ViewMode mode)
{
    m_viewMode = mode;

    // �бновляем ак�ивн�� вкладк� �е�ез SmartButton::setActiveState
    m_historyBtn->setActiveState(mode == ViewMode::History);
    m_pinsBtn->setActiveState(   mode == ViewMode::Pins);

    // �еняем плейс�олде� и сб�ас�ваем поиск п�и смене �ежима
    if (m_searchEdit) {
        m_searchEdit->clear();
        m_searchEdit->setPlaceholderText(
            mode == ViewMode::History ? tr("Поиск по тексту...") : tr("Поиск по названию...")
        );
    }

    // Кнопка мультивыбора видна только в Закрепах
    if (m_editBtn) m_editBtn->setVisible(mode == ViewMode::Pins);

    // При уходе из Закрепов сбрасываем режим выбора
    if (mode != ViewMode::Pins && m_editMode) {
        m_editMode = false;
        m_selectedPinIds.clear();
        if (m_editBtn) m_editBtn->setActiveState(false);
    }

    if (mode == ViewMode::History) {
        m_foldersBar->hide();
        loadHistory();
    } else {
        m_currentFolder = ""; // п�и каждом пе�е�оде сб�ас�ваем на "�се"
        m_foldersBar->show();
        loadFolderBar();
        loadPins();
    }
}

void MainWindow::toggleEditMode()
{
    m_editMode = !m_editMode;
    m_selectedPinIds.clear();
    if (m_editBtn) m_editBtn->setActiveState(m_editMode);

    auto updateLayout = [&](QVBoxLayout *layout) {
        for (int i = 0; i < layout->count(); ++i) {
            if (auto *card = qobject_cast<ClickableCard*>(layout->itemAt(i)->widget())) {
                card->setEditMode(m_editMode);
                card->setSelected(false);
            }
        }
    };
    updateLayout(m_leftLayout);
    updateLayout(m_rightLayout);
}

void MainWindow::showPinBatchContextMenu(const QPoint &globalPos)
{
    int n = m_selectedPinIds.count();
    RoundedMenu menu(this);

    QList<FolderItem> folders = m_db->getAllFolders();
    if (!folders.isEmpty()) {
        QMenu *moveMenu = menu.addMenu(tr("📁  Переместить в папку (%1)").arg(n));
        moveMenu->addAction(tr("Без папки"), [this]() {
            for (int id : std::as_const(m_selectedPinIds))
                m_db->movePinToFolder(id, "");
            m_selectedPinIds.clear();
            loadFolderBar();
            loadPins();
        });
        moveMenu->addSeparator();
        for (const FolderItem &f : folders) {
            QString fName = f.name;
            moveMenu->addAction(fName, [this, fName]() {
                for (int id : std::as_const(m_selectedPinIds))
                    m_db->movePinToFolder(id, fName);
                m_selectedPinIds.clear();
                loadFolderBar();
                loadPins();
            });
        }
        menu.addSeparator();
    }

    menu.addAction(tr("🗑  Удалить выбранные (%1)").arg(n), [this]() {
        for (int id : std::as_const(m_selectedPinIds))
            m_db->deletePin(id);
        m_selectedPinIds.clear();
        loadPins();
    });

    menu.exec(globalPos);
}

void MainWindow::loadPins(bool allFolders)
{
    // ��меняем незаве���нн�� ленив�� заг��зк�
    ++m_loadGeneration;
    m_pendingImages.clear();

    // ��и�аем колонки
    QLayoutItem *item;
    while ((item = m_leftLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    while ((item = m_rightLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    // allFolders=true испол�з�е�ся п�и поиске, ��об� най�и зак�еп� из л�бой папки
    QList<PinItem> pins = allFolders ? m_db->getAllPins() : m_db->getPins(m_currentFolder);

    // Со��и�овка
    switch (m_sortOrder) {
        case SortOrder::DateAsc:
            std::reverse(pins.begin(), pins.end()); // �� да�� DESC, �азво�а�иваем
            break;
        case SortOrder::NameAsc:
            std::sort(pins.begin(), pins.end(), [](const PinItem &a, const PinItem &b) {
                return a.name.toLower() < b.name.toLower();
            });
            break;
        case SortOrder::NameDesc:
            std::sort(pins.begin(), pins.end(), [](const PinItem &a, const PinItem &b) {
                return a.name.toLower() > b.name.toLower();
            });
            break;
        default: break; // DateDesc � �же п�авил�н�й по�ядок из ��
    }

    for (const PinItem &pin : pins) {
        if (pin.type == "text") {
            // �� Текс�ов�й зак�еп ���������������������������������������������
            ClickableCard *card = new ClickableCard();
            applyCardShadow(card);
            card->setWordWrap(true);
            card->setMaximumWidth(LEFT_WIDTH - 36);
            card->setCardStyle(QColor(42, 30, 8, 245));
            card->setStyleSheet("color: #f0e0c0; padding: 8px; font-size: 12px;");

            // Название зак�епа жи�н�м + п�ев�� кон�ен�а
            QString preview = pin.content.trimmed().left(200);
            if (pin.content.length() > 200) preview += "...";
            QString pinDisplay = QString("⭐ %1\n%2").arg(pin.name, softWrap(preview));
            card->setText(pinDisplay);

            // Со��аняем полн�й �екс�, имя и �еги � н�жн� для поиска и �едак�и�ования
            card->setProperty("fullText",    pin.content);
            card->setProperty("displayText", pinDisplay);
            card->setProperty("pinName",     pin.name);
            card->setProperty("pinTags",     pin.tags);
            card->setProperty("pinId",       pin.id);
            card->setEditMode(m_editMode);

            // Чи�аем fullText из свойс�ва � �ак вс�ави�ся о��едак�и�ованн�й �екс�
            connect(card, &ClickableCard::clicked, this, [this, card, pid = pin.id]() {
                if (m_editMode) {
                    bool sel = !m_selectedPinIds.contains(pid);
                    if (sel) m_selectedPinIds.insert(pid); else m_selectedPinIds.remove(pid);
                    card->setSelected(sel);
                    return;
                }
                m_db->touchPin(pid);
                pasteText(card->property("fullText").toString());
            });

            int pinId = pin.id;
            QString pinFolder = pin.folder;
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, this,
                    [this, pinId, pinFolder, card](const QPoint &pos) {
                if (m_editMode && !m_selectedPinIds.isEmpty()) {
                    showPinBatchContextMenu(card->mapToGlobal(pos));
                    return;
                }
                showPinContextMenu(pinId, pinFolder, card, card->mapToGlobal(pos));
            });

            connect(card, &ClickableCard::dragStarted, this, [this, card](QPoint) {
                m_dragCard = card; m_dragLayout = m_leftLayout;
            });
            connect(card, &ClickableCard::dragMoved, this, [this](QPoint gp) {
                onCardDragMoved(gp);
            });
            connect(card, &ClickableCard::dragFinished, this, [this]() {
                onCardDragFinished();
            });

            m_leftLayout->addWidget(card);

        } else if (pin.type == "image") {
            // �� �зоб�ажение-зак�еп �������������������������������������������
            // �а��о�ка: ��пл�й �он как � �екс�ов�� зак�епов, све��� "⭐ Название"
            ClickableCard *card = new ClickableCard();
            applyCardShadow(card);
            card->setFixedSize(RIGHT_WIDTH - 36, 148);
            card->setCardStyle(QColor(42, 30, 8, 245));
            card->setProperty("pinName", pin.name);
            card->setProperty("pinTags", pin.tags);
            card->setProperty("pinId",   pin.id);
            card->setEditMode(m_editMode);

            // �� С��ока с именем све��� (как � �екс�ов��) ���������������������
            QString displayName = pin.name.isEmpty() ? "⭐" : QString("⭐ %1").arg(pin.name);
            QLabel *nameLabel = new QLabel(displayName, card);
            nameLabel->setStyleSheet(
                "color: #f0e0c0; font-size: 12px; font-weight: bold;"
                "background: transparent; padding: 5px 8px 3px 8px;"
            );
            nameLabel->setFixedWidth(card->width());
            nameLabel->setFixedHeight(24);
            nameLabel->move(0, 0);
            nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            nameLabel->show();

            // �� �зоб�ажение ниже с��оки с именем �����������������������������
            QLabel *imgLabel = new QLabel(card);
            const int imgTop = 26;
            imgLabel->setFixedSize(card->width() - 4, card->height() - imgTop - 2);
            imgLabel->move(2, imgTop);
            imgLabel->setAlignment(Qt::AlignCenter);
            imgLabel->setStyleSheet("background: transparent;");
            imgLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

            QPixmap pixmap(pin.filepath);
            if (!pixmap.isNull()) {
                imgLabel->setPixmap(pixmap.scaled(
                    imgLabel->width(), imgLabel->height(),
                    Qt::KeepAspectRatio, Qt::SmoothTransformation
                ));
            } else {
                imgLabel->setText(tr("Название для «%1»:"));
                imgLabel->setStyleSheet("color: #888; background: transparent;");
            }
            imgLabel->show();

            connect(card, &ClickableCard::clicked, this, [this, card, fp = pin.filepath, pid = pin.id]() {
                if (m_editMode) {
                    bool sel = !m_selectedPinIds.contains(pid);
                    if (sel) m_selectedPinIds.insert(pid); else m_selectedPinIds.remove(pid);
                    card->setSelected(sel);
                    return;
                }
                m_db->touchPin(pid);
                pasteImage(fp);
            });

            int pinId = pin.id;
            QString imgPinFolder = pin.folder;
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, this,
                    [this, pinId, imgPinFolder, pin, card, nameLabel](const QPoint &pos) {
                if (m_editMode && !m_selectedPinIds.isEmpty()) {
                    showPinBatchContextMenu(card->mapToGlobal(pos));
                    return;
                }
                RoundedMenu menu(this);

                menu.addAction(tr("🔍  Просмотреть"), [this, fp = pin.filepath]() {
                    showImageViewer(fp);
                });
                menu.addSeparator();

                // �е�еименова��
                menu.addAction(tr("🏷  Переименовать"), [this, pinId, card, nameLabel]() {
                    // ���езаем "⭐ " из �ек��его имени
                    QString cur = card->property("pinName").toString();
                    bool ok = false;
                    QString newName = askText(this, tr("Переименовать закреп"), tr("Новое название:"), cur, &ok);
                    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == cur) return;
                    m_db->updatePinName(pinId, newName.trimmed());
                    card->setProperty("pinName", newName.trimmed());
                    nameLabel->setText(QString("⭐ %1").arg(newName.trimmed()));
                });
                menu.addAction(tr("#  Теги"), [this, pinId, card]() {
                    showTagsDialog(pinId, card);
                });
                menu.addSeparator();

                // �е�емес�и�� в папк�
                QList<FolderItem> folders = m_db->getAllFolders();
                if (!folders.isEmpty()) {
                    QMenu *moveMenu = menu.addMenu(tr("📁  Переместить в папку"));
                    if (!imgPinFolder.isEmpty()) {
                        moveMenu->addAction(tr("Без папки"), [this, pinId]() {
                            m_db->movePinToFolder(pinId, "");
                            loadFolderBar(); loadPins();
                        });
                        moveMenu->addSeparator();
                    }
                    for (const FolderItem &f : folders) {
                        if (f.name == imgPinFolder) continue;
                        QString fName = f.name;
                        moveMenu->addAction(fName, [this, pinId, fName]() {
                            m_db->movePinToFolder(pinId, fName);
                            loadFolderBar(); loadPins();
                        });
                    }
                    menu.addSeparator();
                }

                menu.addAction(tr("🗑  Открепить"), [this, pinId, card]() {
                    deletePinCard(pinId, card);
                });
                menu.exec(card->mapToGlobal(pos));
            });

            connect(card, &ClickableCard::dragStarted, this, [this, card](QPoint) {
                m_dragCard = card; m_dragLayout = m_rightLayout;
            });
            connect(card, &ClickableCard::dragMoved, this, [this](QPoint gp) {
                onCardDragMoved(gp);
            });
            connect(card, &ClickableCard::dragFinished, this, [this]() {
                onCardDragFinished();
            });

            m_rightLayout->addWidget(card);

        } else if (pin.type == "video") {
            // �� �идео-зак�еп ��������������������������������������������������
            ClickableCard *card = new ClickableCard();
            applyCardShadow(card);
            card->setFixedSize(RIGHT_WIDTH - 36, 148);
            card->setCardStyle(QColor(10, 20, 48, 245));
            card->setProperty("pinName", pin.name);
            card->setProperty("pinTags", pin.tags);
            card->setProperty("pinId",   pin.id);
            card->setEditMode(m_editMode);

            // С��ока с именем све���
            QString displayName = pin.name.isEmpty()
                ? QFileInfo(pin.filepath).fileName() : pin.name;
            QLabel *nameLabel = new QLabel(QString("🎬  %1").arg(displayName), card);
            nameLabel->setStyleSheet(
                "color: #aaccff; font-size: 12px; font-weight: bold;"
                "background: transparent; padding: 5px 8px 3px 8px;"
            );
            nameLabel->setFixedWidth(card->width());
            nameLabel->setFixedHeight(24);
            nameLabel->move(0, 0);
            nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            nameLabel->show();

            // ��ев�� ниже с��оки с именем
            QLabel *imgLabel = new QLabel(card);
            const int imgTop = 26;
            imgLabel->setFixedSize(card->width() - 4, card->height() - imgTop - 2);
            imgLabel->move(2, imgTop);
            imgLabel->setAlignment(Qt::AlignCenter);
            imgLabel->setStyleSheet("background: transparent;");
            imgLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

            // ���зим п�ев�� �е�ез Shell � �е же миниа���� ��о в п�оводнике
            QPixmap thumb = shellThumbnail(pin.filepath, imgLabel->width(), imgLabel->height());
            if (!thumb.isNull()) {
                imgLabel->setPixmap(thumb.scaled(
                    imgLabel->width(), imgLabel->height(),
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
            } else {
                imgLabel->setText("🎬");
                imgLabel->setStyleSheet("color: #6688bb; font-size: 32px; background: transparent;");
            }
            imgLabel->show();

            connect(card, &ClickableCard::clicked, this, [this, card, fp = pin.filepath, pid = pin.id]() {
                if (m_editMode) {
                    bool sel = !m_selectedPinIds.contains(pid);
                    if (sel) m_selectedPinIds.insert(pid); else m_selectedPinIds.remove(pid);
                    card->setSelected(sel);
                    return;
                }
                m_db->touchPin(pid);
                pasteFile(fp);
            });

            int pinId = pin.id;
            QString pinFolder = pin.folder;
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, this,
                    [this, pinId, pinFolder, card, nameLabel, fp = pin.filepath](const QPoint &pos) {
                if (m_editMode && !m_selectedPinIds.isEmpty()) {
                    showPinBatchContextMenu(card->mapToGlobal(pos));
                    return;
                }
                RoundedMenu menu(this);
                menu.addAction(tr("🔍  Просмотреть"), [fp]() {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(fp));
                });
                menu.addSeparator();
                menu.addAction(tr("🏷  Переименовать"), [this, pinId, card, nameLabel]() {
                    bool ok = false;
                    QString cur = card->property("pinName").toString();
                    QString newName = askText(this, tr("Переименовать закреп"), tr("Новое название:"), cur, &ok);
                    if (!ok || newName.trimmed().isEmpty()) return;
                    m_db->updatePinName(pinId, newName.trimmed());
                    card->setProperty("pinName", newName.trimmed());
                    nameLabel->setText(QString("🎬  %1").arg(newName.trimmed()));
                });
                menu.addAction(tr("#  Теги"), [this, pinId, card]() {
                    showTagsDialog(pinId, card);
                });
                menu.addSeparator();
                QList<FolderItem> folders = m_db->getAllFolders();
                if (!folders.isEmpty()) {
                    QMenu *mv = menu.addMenu(tr("📁  Переместить в папку"));
                    if (!pinFolder.isEmpty()) {
                        mv->addAction(tr("Без папки"), [this, pinId]() {
                            m_db->movePinToFolder(pinId, ""); loadFolderBar(); loadPins();
                        });
                        mv->addSeparator();
                    }
                    for (const FolderItem &f : folders) {
                        if (f.name == pinFolder) continue;
                        QString fn = f.name;
                        mv->addAction(fn, [this, pinId, fn]() {
                            m_db->movePinToFolder(pinId, fn); loadFolderBar(); loadPins();
                        });
                    }
                    menu.addSeparator();
                }
                menu.addAction(tr("🗑  Открепить"), [this, pinId, card]() {
                    deletePinCard(pinId, card);
                });
                menu.exec(card->mapToGlobal(pos));
            });

            connect(card, &ClickableCard::dragStarted, this, [this, card](QPoint) {
                m_dragCard = card; m_dragLayout = m_rightLayout;
            });
            connect(card, &ClickableCard::dragMoved, this, [this](QPoint gp) {
                onCardDragMoved(gp);
            });
            connect(card, &ClickableCard::dragFinished, this, [this]() {
                onCardDragFinished();
            });

            m_rightLayout->addWidget(card);

        } else if (pin.type == "audio") {
            // �� А�дио-зак�еп ��������������������������������������������������
            ClickableCard *card = new ClickableCard();
            applyCardShadow(card);
            card->setWordWrap(true);
            card->setMaximumWidth(LEFT_WIDTH - 36);
            card->setCardStyle(QColor(32, 14, 55, 245));
            card->setStyleSheet("color: #ddaaff; padding: 8px; font-size: 12px;");

            QString displayName = pin.name.isEmpty()
                ? QFileInfo(pin.filepath).fileName() : pin.name;
            card->setText(QString("🎵  %1").arg(displayName));
            card->setProperty("pinName", pin.name);
            card->setProperty("pinTags", pin.tags);
            card->setProperty("pinId",   pin.id);
            card->setEditMode(m_editMode);

            connect(card, &ClickableCard::clicked, this, [this, card, fp = pin.filepath, pid = pin.id]() {
                if (m_editMode) {
                    bool sel = !m_selectedPinIds.contains(pid);
                    if (sel) m_selectedPinIds.insert(pid); else m_selectedPinIds.remove(pid);
                    card->setSelected(sel);
                    return;
                }
                m_db->touchPin(pid);
                pasteFile(fp);
            });

            int pinId = pin.id;
            QString pinFolder = pin.folder;
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, this,
                    [this, pinId, pinFolder, card, fp = pin.filepath](const QPoint &pos) {
                if (m_editMode && !m_selectedPinIds.isEmpty()) {
                    showPinBatchContextMenu(card->mapToGlobal(pos));
                    return;
                }
                RoundedMenu menu(this);
                menu.addAction(tr("🎵 Аудио"), [fp]() {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(fp));
                });
                menu.addSeparator();
                menu.addAction(tr("🏷  Переименовать"), [this, pinId, card]() {
                    bool ok = false;
                    QString cur = card->property("pinName").toString();
                    QString newName = askText(this, tr("Переименовать закреп"), tr("Новое название:"), cur, &ok);
                    if (!ok || newName.trimmed().isEmpty()) return;
                    m_db->updatePinName(pinId, newName.trimmed());
                    card->setProperty("pinName", newName.trimmed());
                    card->setText(QString("🎵  %1").arg(newName.trimmed()));
                });
                menu.addAction(tr("#  Теги"), [this, pinId, card]() {
                    showTagsDialog(pinId, card);
                });
                menu.addSeparator();
                QList<FolderItem> folders = m_db->getAllFolders();
                if (!folders.isEmpty()) {
                    QMenu *mv = menu.addMenu(tr("📁  Переместить в папку"));
                    if (!pinFolder.isEmpty()) {
                        mv->addAction(tr("Без папки"), [this, pinId]() {
                            m_db->movePinToFolder(pinId, ""); loadFolderBar(); loadPins();
                        });
                        mv->addSeparator();
                    }
                    for (const FolderItem &f : folders) {
                        if (f.name == pinFolder) continue;
                        QString fn = f.name;
                        mv->addAction(fn, [this, pinId, fn]() {
                            m_db->movePinToFolder(pinId, fn); loadFolderBar(); loadPins();
                        });
                    }
                    menu.addSeparator();
                }
                menu.addAction(tr("🗑  Открепить"), [this, pinId, card]() {
                    deletePinCard(pinId, card);
                });
                menu.exec(card->mapToGlobal(pos));
            });

            connect(card, &ClickableCard::dragStarted, this, [this, card](QPoint) {
                m_dragCard = card; m_dragLayout = m_leftLayout;
            });
            connect(card, &ClickableCard::dragMoved, this, [this](QPoint gp) {
                onCardDragMoved(gp);
            });
            connect(card, &ClickableCard::dragFinished, this, [this]() {
                onCardDragFinished();
            });

            m_leftLayout->addWidget(card);
        }
    }

}

void MainWindow::onCardDragMoved(const QPoint &globalPos)
{
    if (!m_dragCard || !m_dragLayout) return;

    // Текущая позиция перетаскиваемой карточки
    int currentIdx = -1;
    for (int i = 0; i < m_dragLayout->count(); ++i) {
        if (m_dragLayout->itemAt(i)->widget() == m_dragCard) { currentIdx = i; break; }
    }
    if (currentIdx == -1) return;

    // Ищем позицию вставки (перед первой карточкой, верхний край которой ниже курсора)
    int insertIdx = m_dragLayout->count(); // по умолчанию — в конец
    for (int i = 0; i < m_dragLayout->count(); ++i) {
        QWidget *w = m_dragLayout->itemAt(i)->widget();
        if (!w || w == m_dragCard) continue;
        if (globalPos.y() < w->mapToGlobal(QPoint(0, w->height() / 2)).y()) {
            insertIdx = i;
            break;
        }
    }

    // Не двигаем если карточка уже на нужном месте
    if (insertIdx == currentIdx || insertIdx == currentIdx + 1) return;

    m_dragLayout->removeWidget(m_dragCard);
    int adjustedIdx = (insertIdx > currentIdx) ? insertIdx - 1 : insertIdx;
    m_dragLayout->insertWidget(adjustedIdx, m_dragCard);
}

void MainWindow::onCardDragFinished()
{
    if (m_dragLayout) savePinsColumnOrder(m_dragLayout);
    m_dragCard   = nullptr;
    m_dragLayout = nullptr;
}

void MainWindow::savePinsColumnOrder(QVBoxLayout *layout)
{
    QList<QPair<int,int>> orders;
    for (int i = 0; i < layout->count(); ++i) {
        QWidget *w = layout->itemAt(i)->widget();
        if (!w) continue;
        int pid = w->property("pinId").toInt();
        if (pid > 0) orders.append({pid, i + 1});
    }
    if (!orders.isEmpty())
        m_db->updatePinsSortOrder(orders);
}

void MainWindow::showTagsDialog(int pinId, QWidget *card)
{
    QString currentTags = card ? card->property("pinTags").toString() : "";
    QStringList tags = currentTags.split(' ', Qt::SkipEmptyParts);

    AppDialog dlg(this);
    dlg.setWindowTitle(tr("Теги закрепа"));
    dlg.setMinimumWidth(440);
    dlg.setMinimumHeight(360);
    dlg.resize(440, 420);
    dlg.body()->setStyleSheet(
        "QLabel     { color: #aaa; font-size: 12px; }"
        "QScrollArea{ background: rgba(28,28,44,200); border: 1px solid rgba(255,255,255,40); border-radius: 6px; }"
        "QWidget#chipsWidget { background: transparent; }"
        "QLineEdit  { background: rgba(28,28,44,245); color: #eee; border: 1px solid rgba(255,255,255,55);"
        "             border-radius: 4px; padding: 6px; font-size: 13px; }"
        "QPushButton { background: rgba(40,40,60,220); color: #ccc; border: 1px solid rgba(255,255,255,40);"
        "              border-radius: 4px; padding: 6px 18px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(60,60,90,240); color: #fff; }"
        "QPushButton#addBtn  { background: rgba(30,80,40,220); color: #aaffaa;"
        "                      border: 1px solid rgba(100,200,100,80);"
        "                      border-radius: 4px; padding: 6px 14px; font-size: 16px; font-weight: bold; }"
        "QPushButton#addBtn:hover { background: rgba(40,120,55,240); color: #fff; }"
        "QPushButton#deleteBtn { background: transparent; color: #666; padding: 2px 6px;"
        "                        font-size: 12px; border: none; border-radius: 3px; }"
        "QPushButton#deleteBtn:hover { background: rgba(180,60,60,180); color: #fff; }"
    );

    QVBoxLayout *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    // �� ��ок���иваем�й список �егов ������������������������������������������
    QScrollArea *scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(120);

    QWidget *chipsWidget = new QWidget();
    chipsWidget->setObjectName("chipsWidget");
    QVBoxLayout *chipsLayout = new QVBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(8, 6, 8, 6);
    chipsLayout->setSpacing(4);
    chipsLayout->addStretch();

    scroll->setWidget(chipsWidget);
    vl->addWidget(scroll);

    // Ф�нк�ия пе�ес��ойки списка �егов (std::function � ��об� лямбда могла за�ва�и�� себя)
    std::function<void()> rebuildChips = [&]() {
        // Удаляем все с��оки к�оме последнего stretch
        while (chipsLayout->count() > 1)
            delete chipsLayout->takeAt(0)->widget();

        for (int i = 0; i < tags.size(); ++i) {
            QWidget *row = new QWidget(chipsWidget);
            row->setStyleSheet("background: transparent;");
            QHBoxLayout *hl = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(6);

            QLabel *lbl = new QLabel(QString("<b>#%1</b>").arg(tags[i]), row);
            lbl->setStyleSheet("color: #aaddff; font-size: 13px; background: transparent;");

            QPushButton *del = new QPushButton("✕", row);
            del->setObjectName("deleteBtn");
            del->setFixedSize(22, 22);
            del->setCursor(Qt::PointingHandCursor);
            del->setToolTip(tr("Удалить"));

            int idx = i;
            connect(del, &QPushButton::clicked, [&tags, idx, &rebuildChips]() {
                tags.removeAt(idx);
                rebuildChips();
            });

            hl->addWidget(lbl);
            hl->addStretch();
            hl->addWidget(del);

            // �с�авляем пе�ед stretch
            chipsLayout->insertWidget(chipsLayout->count() - 1, row);
        }

        if (tags.isEmpty()) {
            QLabel *empty = new QLabel(tr("Нет тегов. Добавь первый тег ниже."), chipsWidget);
            empty->setStyleSheet("color: #555; font-size: 12px; background: transparent;");
            empty->setAlignment(Qt::AlignCenter);
            chipsLayout->insertWidget(0, empty);
        }
    };

    rebuildChips();

    // �� �опи�ова�� / �с�ави�� �еги �������������������������������������������
    QHBoxLayout *clipRow = new QHBoxLayout();
    clipRow->setSpacing(6);

    QPushButton *copyBtn  = new QPushButton(tr("Копировать теги"), &dlg);
    QPushButton *pasteBtn = new QPushButton(tr("Вставить теги"),   &dlg);
    copyBtn->setCursor(Qt::PointingHandCursor);
    pasteBtn->setCursor(Qt::PointingHandCursor);

    // �опи�ова��: соби�аем �ек��ие �еги с # и клад�м в б��е�
    connect(copyBtn, &QPushButton::clicked, [&]() {
        if (tags.isEmpty()) return;
        QStringList withHash;
        for (const QString &t : tags) withHash << "#" + t;
        QApplication::clipboard()->setText(withHash.join(" "));
    });

    // �с�ави��: �и�аем из б��е�а, па�сим �еги, добавляем без д�блика�ов
    connect(pasteBtn, &QPushButton::clicked, [&]() {
        QString raw = QApplication::clipboard()->text();
        raw.replace(',', ' ').replace('#', ' ');
        QStringList parts = raw.split(' ', Qt::SkipEmptyParts);
        bool added = false;
        for (const QString &p : parts) {
            QString t = p.toLower().trimmed();
            if (!t.isEmpty() && !tags.contains(t)) {
                tags.append(t);
                added = true;
            }
        }
        if (added) rebuildChips();
    });

    clipRow->addWidget(copyBtn);
    clipRow->addWidget(pasteBtn);
    clipRow->addStretch();
    vl->addLayout(clipRow);

    // �� �оле добавления нового �ега ������������������������������������������
    QHBoxLayout *addRow = new QHBoxLayout();
    addRow->setSpacing(6);
    QLineEdit *addEdit = new QLineEdit(&dlg);
    addEdit->setPlaceholderText(tr("Новый тег..."));
    QPushButton *addBtn = new QPushButton("+", &dlg);
    addBtn->setObjectName("addBtn");
    addBtn->setFixedWidth(42);
    addRow->addWidget(addEdit);
    addRow->addWidget(addBtn);
    vl->addLayout(addRow);

    auto addTag = [&]() {
        QString raw = addEdit->text();
        raw.replace(',', ' ').replace('#', ' ');
        QStringList parts = raw.split(' ', Qt::SkipEmptyParts);
        bool added = false;
        for (const QString &p : parts) {
            QString t = p.toLower().trimmed();
            if (!t.isEmpty() && !tags.contains(t)) {
                tags.append(t);
                added = true;
            }
        }
        if (added) { addEdit->clear(); rebuildChips(); }
    };
    connect(addBtn,  &QPushButton::clicked, addTag);
    connect(addEdit, &QLineEdit::returnPressed, addTag);

    // �� �нопки ���������������������������������������������������������������
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    QPushButton *cancelBtn = new QPushButton(tr("Отмена"), &dlg);
    QPushButton *okBtn     = new QPushButton(tr("Сохранить"), &dlg);
    okBtn->setDefault(true);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    vl->addLayout(btnRow);

    connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    QString newTags = tags.join(' ');
    m_db->updatePinTags(pinId, newTags);
    if (card) card->setProperty("pinTags", newTags);
}

void MainWindow::showPinContextMenu(int pinId, const QString &currentFolder,
                                    QLabel *card, const QPoint &globalPos)
{
    RoundedMenu menu(this);

    // �е�еименова�� зак�еп
    menu.addAction(tr("✏️  Переименовать"), [this, pinId, card]() {
        if (!card) return;
        // Тек��ее имя � пе�вая с��ока �екс�а ка��о�ки (без "⭐ ")
        QString oldText  = card->text();
        int nl           = oldText.indexOf('\n');
        QString nameLine = (nl >= 0) ? oldText.left(nl) : oldText;
        QString oldName  = nameLine.mid(nameLine.indexOf(' ') + 1); // �би�аем "⭐ "

        bool ok = false;
        QString newName = askText(this, tr("Переименовать закреп"), tr("Новое название:"), oldName, &ok);
        if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName) return;

        m_db->updatePinName(pinId, newName.trimmed());

        // �бновляем ка��о�к� без пе�езаг��зки
        QString content = card->property("fullText").toString();
        QString preview = content.trimmed().left(80);
        if (content.length() > 80) preview += "...";
        card->setText(QString("⭐ %1\n%2").arg(newName.trimmed(), preview));
    });

    // Редак�и�ова�� �екс� зак�епа
    menu.addAction(tr("📝  Редактировать"), [this, pinId, card]() {
        QString content = card ? card->property("fullText").toString() : "";
        editPin(pinId, content, card);
    });

    menu.addAction(tr("#  Теги"), [this, pinId, card]() {
        showTagsDialog(pinId, card);
    });
    menu.addSeparator();

    // �одмен� "�е�емес�и�� в папк�"
    QList<FolderItem> folders = m_db->getAllFolders();
    if (!folders.isEmpty()) {
        QMenu *moveMenu = menu.addMenu(tr("📁  Переместить в папку"));

        // "�ез папки" если зак�еп сей�ас в папке
        if (!currentFolder.isEmpty()) {
            moveMenu->addAction(tr("Без папки"), [this, pinId, card]() {
                m_db->movePinToFolder(pinId, "");
                loadFolderBar();
                loadPins();
            });
            moveMenu->addSeparator();
        }

        for (const FolderItem &f : folders) {
            if (f.name == currentFolder) continue; // �же в э�ой папке
            QString fName = f.name;
            moveMenu->addAction(fName, [this, pinId, fName]() {
                m_db->movePinToFolder(pinId, fName);
                loadFolderBar();
                loadPins();
            });
        }
    }

    menu.addSeparator();
    menu.addAction(tr("🗑  Удалить"), [this, pinId, card]() {
        deletePinCard(pinId, card);
    });

    menu.exec(globalPos);
}

void MainWindow::deletePinCard(int pinId, QLabel *card)
{
    m_db->deletePin(pinId);

    if (card) {
        QVBoxLayout *layout = nullptr;
        if (m_leftLayout->indexOf(card) != -1)
            layout = m_leftLayout;
        else if (m_rightLayout->indexOf(card) != -1)
            layout = m_rightLayout;

        if (layout)
            layout->removeWidget(card);

        card->deleteLater();
    }
}

void MainWindow::pasteTextSilent(const QString &text)
{
    // �с�ави�� �екс� п�о�иля без о�к���ия/зак���ия окна SmartClip
    emit pasteRequested();
    QApplication::clipboard()->setText(text);

    QTimer::singleShot(150, []() {
        INPUT inputs[4] = {};
        inputs[0].type   = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type   = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
        inputs[2].type   = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V';
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type   = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));
    });
}

void MainWindow::showProfilesMenu()
{
    QList<ProfileItem> profiles = m_db->getProfiles();

    RoundedMenu menu(this);

    if (profiles.isEmpty()) {
        menu.addAction(tr("Нет профилей"))->setEnabled(false);
    } else {
        for (const ProfileItem &p : profiles) {
            QString label = QString("%1  [%2]").arg(p.name, p.hotkey);
            menu.addAction(label, [this, text = p.text]() {
                pasteText(text); // вс�авляем и п�я�ем окно
            });
        }
    }

    menu.addSeparator();
    menu.addAction(tr("✏️  Управление профилями"), [this]() {
        showProfileManager();
    });

    menu.exec(m_profilesBtn->mapToGlobal(QPoint(0, m_profilesBtn->height() + 6)));
}

void MainWindow::showProfileManager()
{
    AppDialog dialog(this);
    dialog.setWindowTitle(tr("Профили"));
    dialog.setMinimumSize(480, 420);

    QVBoxLayout *mainLayout = new QVBoxLayout(dialog.body());
    mainLayout->setContentsMargins(16, 16, 16, 12);
    mainLayout->setSpacing(8);

    QLabel *hint = new QLabel(
        tr("Профили быстрой вставки — нажми хоткей или выбери из меню."), &dialog);
    hint->setStyleSheet("color: #888888; font-size: 11px;");
    mainLayout->addWidget(hint);

    QScrollArea *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("background: transparent; border: none;");
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget    *listWidget = new QWidget();
    QVBoxLayout *listLayout = new QVBoxLayout(listWidget);
    listLayout->setAlignment(Qt::AlignTop);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(6);
    listWidget->setStyleSheet("background: transparent;");
    scroll->setWidget(listWidget);
    mainLayout->addWidget(scroll, 1);

    // �� �спомога�ел�ная ��нк�ия: о�к���� �о�м� добавления/�едак�и�ования �����
    auto openForm = [&](int editId, const QString &initName,
                        const QString &initHotkey, const QString &initText) -> bool
    {
        AppDialog form(&dialog);
        form.setWindowTitle(editId < 0 ? tr("Новый профиль") : tr("Редактировать профиль"));
        form.setMinimumSize(400, 340);

        QVBoxLayout *fl = new QVBoxLayout(form.body());
        fl->setContentsMargins(16, 16, 16, 12);
        fl->setSpacing(8);

        auto mkField = [&](const QString &label) -> QLineEdit* {
            fl->addWidget(new QLabel(label, &form));
            QLineEdit *f = new QLineEdit(&form);
            f->setStyleSheet(
                "background: #1e1e1e; color: #ddd; border: 1px solid #555;"
                "border-radius: 5px; padding: 5px 8px;"
            );
            fl->addWidget(f);
            return f;
        };

        QLineEdit *nameField = mkField(tr("Название:"));

        // �оле за�ва�а �о�кея � кликни и нажми комбина�и�
        fl->addWidget(new QLabel(tr("Горячая клавиша:"), &form));
        HotkeyEdit *hotkeyField = new HotkeyEdit(&form);
        fl->addWidget(hotkeyField);

        fl->addWidget(new QLabel(tr("Текст:"), &form));
        QTextEdit *textField = new QTextEdit(&form);
        textField->setStyleSheet(
            "background: #1e1e1e; color: #ddd; border: 1px solid #555;"
            "border-radius: 5px; padding: 5px;"
        );
        fl->addWidget(textField, 1);

        nameField->setText(initName);
        hotkeyField->setText(initHotkey);
        textField->setPlainText(initText);

        QHBoxLayout *btns = new QHBoxLayout();
        QPushButton *cancelBtn = new QPushButton(tr("Отмена"), &form);
        QPushButton *saveBtn   = new QPushButton(tr("Сохранить"), &form);
        cancelBtn->setStyleSheet(
            "QPushButton { background: rgba(60,60,60,200); color: #aaa; "
            "border-radius: 6px; padding: 6px 20px; border: none; }"
            "QPushButton:hover { background: rgba(80,80,80,220); }"
        );
        saveBtn->setStyleSheet(
            "QPushButton { background: rgba(40,100,40,200); color: #ccffcc; "
            "border-radius: 6px; padding: 6px 20px; border: none; }"
            "QPushButton:hover { background: rgba(60,140,60,220); color: #fff; }"
        );
        saveBtn->setDefault(true);
        connect(cancelBtn, &QPushButton::clicked, &form, &QDialog::reject);
        connect(saveBtn,   &QPushButton::clicked, &form, &QDialog::accept);
        btns->addStretch();
        btns->addWidget(cancelBtn);
        btns->addWidget(saveBtn);
        fl->addLayout(btns);

        if (form.exec() != QDialog::Accepted) return false;

        QString n = nameField->text().trimmed();
        QString h = hotkeyField->text().trimmed();
        QString t = textField->toPlainText();
        if (n.isEmpty() || t.isEmpty()) return false;

        if (editId < 0)
            m_db->addProfile(n, h, t);
        else
            m_db->updateProfile(editId, n, h, t);

        emit profilesChanged();
        return true;
    };

    // �� �е�ес��ойка списка (�е�ез QTimer ��об� не �даля�� видже� из его �эндле�а)
    std::function<void()> rebuild = [&]() {
        QLayoutItem *item;
        while ((item = listLayout->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }

        QList<ProfileItem> profiles = m_db->getProfiles();
        if (profiles.isEmpty()) {
            QLabel *empty = new QLabel(tr("Нет профилей"), listWidget);
            empty->setAlignment(Qt::AlignCenter);
            empty->setStyleSheet("color: #666; padding: 20px;");
            listLayout->addWidget(empty);
            return;
        }

        for (const ProfileItem &prof : profiles) {
            QWidget *row = new QWidget(listWidget);
            row->setStyleSheet(
                // "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                // "stop:0 rgba(255,255,255,38),stop:0.2 rgba(22,22,35,242),stop:1 rgba(15,15,25,248));"
                "background: rgba(20,20,32,245);"
                "border-radius: 8px;"
                "border: 2px solid rgba(255,255,255,55);"
            );
            QHBoxLayout *rowL = new QHBoxLayout(row);
            rowL->setContentsMargins(10, 8, 10, 8);
            rowL->setSpacing(8);

            QVBoxLayout *info = new QVBoxLayout();
            QLabel *nameLabel = new QLabel(
                QString("<b>%1</b>  <span style='color:#888;font-size:11px;'>[%2]</span>")
                    .arg(prof.name.toHtmlEscaped(), prof.hotkey.toHtmlEscaped()),
                row
            );
            nameLabel->setStyleSheet("color: #dddddd;");
            QString preview = prof.text.trimmed().left(60);
            if (prof.text.length() > 60) preview += "...";
            QLabel *prevLabel = new QLabel(preview.toHtmlEscaped(), row);
            prevLabel->setStyleSheet("color: #888888; font-size: 11px;");
            info->addWidget(nameLabel);
            info->addWidget(prevLabel);
            rowL->addLayout(info, 1);

            QPushButton *editBtn = new QPushButton("✏️", row);
            editBtn->setFixedSize(28, 28);
            editBtn->setStyleSheet(
                "QPushButton { background: rgba(60,60,60,200); color: #aaa; "
                "border-radius: 5px; border: none; font-size: 14px; }"
                "QPushButton:hover { background: rgba(80,80,80,220); color: #fff; }"
            );
            editBtn->setCursor(Qt::PointingHandCursor);

            QPushButton *delBtn = new QPushButton("✕", row);
            delBtn->setFixedSize(28, 28);
            delBtn->setStyleSheet(
                "QPushButton { background: rgba(60,60,60,200); color: #cc4444; "
                "border-radius: 5px; border: none; font-size: 14px; }"
                "QPushButton:hover { background: rgba(100,30,30,220); color: #ff6666; }"
            );
            delBtn->setCursor(Qt::PointingHandCursor);

            rowL->addWidget(editBtn);
            rowL->addWidget(delBtn);
            listLayout->addWidget(row);

            int id = prof.id;
            QString pName = prof.name, pHotkey = prof.hotkey, pText = prof.text;

            // Редак�и�ова��: о�к��ваем �о�м�, после � о�ложенная пе�ес��ойка
            connect(editBtn, &QPushButton::clicked, [=, &openForm, &rebuild]() {
                if (openForm(id, pName, pHotkey, pText))
                    QTimer::singleShot(0, [&rebuild]() { rebuild(); });
            });

            // Удали��: сна�ала возв�а�аемся из �эндле�а, по�ом �даляем видже��
            connect(delBtn, &QPushButton::clicked, [=, &rebuild]() {
                m_db->deleteProfile(id);
                emit profilesChanged();
                QTimer::singleShot(0, [&rebuild]() { rebuild(); });
            });
        }
    };

    rebuild();

    // �� Нижняя панел� ��������������������������������������������������������
    QHBoxLayout *bottomRow = new QHBoxLayout();

    QPushButton *addBtn = new QPushButton(tr("+ Добавить профиль"), &dialog);
    addBtn->setStyleSheet(
        "QPushButton { background: rgba(50,80,50,200); color: #aaddaa; "
        "border-radius: 6px; padding: 6px 16px; border: none; }"
        "QPushButton:hover { background: rgba(60,120,60,220); color: #fff; }"
    );
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, [&]() {
        if (openForm(-1, "", "", ""))
            QTimer::singleShot(0, [&rebuild]() { rebuild(); });
    });

    QPushButton *closeBtn = new QPushButton(tr("Закрыть"), &dialog);
    closeBtn->setStyleSheet(
        "QPushButton { background: rgba(60,60,60,200); color: #aaa; "
        "border-radius: 6px; padding: 6px 20px; border: none; }"
        "QPushButton:hover { background: rgba(80,80,80,220); color: #ddd; }"
    );
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    bottomRow->addWidget(addBtn);
    bottomRow->addStretch();
    bottomRow->addWidget(closeBtn);
    mainLayout->addLayout(bottomRow);

    dialog.exec();
}

void MainWindow::editPin(int id, const QString &content, QLabel *card)
{
    AppDialog dialog(this);
    dialog.setWindowTitle(tr("Редактировать закреп"));
    dialog.setMinimumSize(420, 300);

    QVBoxLayout *layout = new QVBoxLayout(dialog.body());
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    QLabel *hint = new QLabel(tr("Текст закрепа:"), &dialog);
    layout->addWidget(hint);

    QTextEdit *edit = new QTextEdit(&dialog);
    edit->setPlainText(content);
    edit->setStyleSheet(
        "QTextEdit { background-color: #1e1e1e; color: #dddddd; "
        "border: 1px solid #555; border-radius: 6px; "
        "font-size: 13px; padding: 6px; }"
    );
    layout->addWidget(edit);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);

    QPushButton *cancelBtn = new QPushButton(tr("Отмена"), &dialog);
    cancelBtn->setStyleSheet(
        "QPushButton { background: rgba(60,60,60,200); color: #aaaaaa; "
        "border-radius: 6px; padding: 6px 20px; border: none; }"
        "QPushButton:hover { background: rgba(80,80,80,220); color: #dddddd; }"
    );

    QPushButton *saveBtn = new QPushButton(tr("Сохранить"), &dialog);
    saveBtn->setStyleSheet(
        "QPushButton { background: rgba(40,100,40,200); color: #ccffcc; "
        "border-radius: 6px; padding: 6px 20px; border: none; }"
        "QPushButton:hover { background: rgba(60,140,60,220); color: #ffffff; }"
    );
    saveBtn->setDefault(true);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(saveBtn,   &QPushButton::clicked, &dialog, &QDialog::accept);

    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    layout->addLayout(btnRow);

    if (dialog.exec() != QDialog::Accepted) return;

    QString newContent = edit->toPlainText();
    if (newContent == content) return; // ни�его не изменилос�

    m_db->updatePinContent(id, newContent);

    // �бновляем ка��о�к� без полной пе�езаг��зки
    if (card) {
        card->setProperty("fullText", newContent);
        // �е��м с�а�ое название из �екс�а ка��о�ки (пе�вая с��ока после "⭐ ")
        QString oldText = card->text();
        int nl = oldText.indexOf('\n');
        QString nameLine = (nl >= 0) ? oldText.left(nl) : oldText;
        QString preview  = newContent.trimmed().left(80);
        if (newContent.length() > 80) preview += "...";
        card->setText(nameLine + "\n" + preview);
    }
}

void MainWindow::showSettings()
{
    SettingsDialog dlg(m_db, this);

    connect(&dlg, &SettingsDialog::mainHotkeyChanged,
            this, &MainWindow::mainHotkeyChanged);
    connect(&dlg, &SettingsDialog::settingsChanged,
            this, &MainWindow::settingsChanged);

    dlg.exec();
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG *msg = static_cast<MSG*>(message);

    if (msg->message == WM_NCHITTEST) {
        // QCursor::pos() � �же в логи�ески� пикселя�, без DPI п�облем
        QPoint localPos = mapFromGlobal(QCursor::pos());

        if (!isInteractiveArea(localPos)) {
            *result = HTTRANSPARENT;
            return true;
        }
        return false;
    }

    if (msg->message == WM_ACTIVATE) {
        if (LOWORD(msg->wParam) == WA_INACTIVE) {
            // lParam � �эндл окна ко�о�ое �А��РА�Т �ок�с
            // �сли э�о окно на�его же п�о�есса (диалог QInputDialog и �.п.) � не п�я�емся
            HWND activating = reinterpret_cast<HWND>(msg->lParam);
            DWORD activatingPid = 0;
            if (activating)
                GetWindowThreadProcessId(activating, &activatingPid);

            if (activatingPid != GetCurrentProcessId())
                hide();
        }
    }

    return QWidget::nativeEvent(eventType, message, result);
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        hide();
    else
        QWidget::keyPressEvent(event);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    if (AppSettings::get().solidPanels()) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Тёмный полупрозрачный цвет панелей
        const QColor panelColor(12, 12, 24, 210);
        const int    radius = 0; // колонки — без скругления, они у краёв экрана

        const int W = width();
        const int H = height();

        // ── Верхняя панель ────────────────────────────────────────────
        p.fillRect(0, 0, W, TOP_HEIGHT, panelColor);

        // ── Полоса папок (если видима) ────────────────────────────────
        if (m_foldersBar && m_foldersBar->isVisible())
            p.fillRect(0, TOP_HEIGHT, W, FOLDERS_HEIGHT, panelColor);

        // ── Левая колонка ─────────────────────────────────────────────
        int colTop = TOP_HEIGHT +
                     (m_foldersBar && m_foldersBar->isVisible() ? FOLDERS_HEIGHT : 0);
        int colH   = H - colTop - BOTTOM_HEIGHT;
        p.fillRect(0, colTop, LEFT_WIDTH + 18, colH, panelColor);

        // ── Правая колонка ────────────────────────────────────────────
        p.fillRect(W - RIGHT_WIDTH - 18, colTop, RIGHT_WIDTH + 18, colH, panelColor);

        // ── Нижняя панель ─────────────────────────────────────────────
        p.fillRect(0, H - BOTTOM_HEIGHT, W, BOTTOM_HEIGHT, panelColor);
    }

    QWidget::paintEvent(event);
}

// ─── Авто-обновление ─────────────────────────────────────────────────────────
void MainWindow::onUpdateAvailable(const QString &version, const QString &downloadUrl)
{
    // ── Диалог "доступно обновление" ──────────────────────────────────────────
    AppDialog dlg(this);
    dlg.setWindowTitle(tr("Обновление SmartClip"));
    dlg.setMinimumWidth(420);

    // Кладём всё прямо в dlg.body() — AppDialog уже создал его внутри
    QWidget *bodyW = dlg.body();
    auto *layout = new QVBoxLayout(bodyW);
    layout->setContentsMargins(24, 8, 24, 16);
    layout->setSpacing(14);

    auto *icon = new QLabel("🚀", bodyW);
    icon->setStyleSheet("font-size: 32px;");
    icon->setAlignment(Qt::AlignCenter);

    auto *title = new QLabel(
        tr("Доступна новая версия SmartClip!"), bodyW);
    title->setStyleSheet("color:#fff; font-size:15px; font-weight:bold;");
    title->setAlignment(Qt::AlignCenter);

    auto *sub = new QLabel(
        QString(tr("Версия %1 готова к установке.")).arg(version), bodyW);
    sub->setStyleSheet("color:rgba(255,255,255,160); font-size:12px;");
    sub->setAlignment(Qt::AlignCenter);

    // Прогресс-бар (скрыт до начала загрузки)
    auto *progress = new QProgressBar(bodyW);
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setTextVisible(true);
    progress->setStyleSheet(
        "QProgressBar{background:rgba(255,255,255,20);border-radius:5px;height:14px;}"
        "QProgressBar::chunk{background:#ffdd88;border-radius:5px;}");
    progress->hide();

    auto *statusLbl = new QLabel("", bodyW);
    statusLbl->setStyleSheet("color:rgba(255,255,255,120); font-size:11px;");
    statusLbl->setAlignment(Qt::AlignCenter);
    statusLbl->hide();

    auto *btnRow    = new QHBoxLayout();
    auto *updateBtn = new SmartButton(tr("⬇  Скачать и установить"), bodyW);
    auto *laterBtn  = new SmartButton(tr("Позже"), bodyW);
    updateBtn->setBtnStyle(QColor(48,33,6,245), QColor(0xFF,0xDD,0x88));
    updateBtn->setFixedHeight(34);
    laterBtn->setBtnStyle(QColor(30,30,50,200), QColor(180,180,200));
    laterBtn->setFixedHeight(34);
    btnRow->addWidget(laterBtn);
    btnRow->addWidget(updateBtn);

    layout->addWidget(icon);
    layout->addWidget(title);
    layout->addWidget(sub);
    layout->addSpacing(4);
    layout->addWidget(progress);
    layout->addWidget(statusLbl);
    layout->addLayout(btnRow);

    connect(laterBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    // ── Скачивание ────────────────────────────────────────────────────────────
    connect(updateBtn, &QPushButton::clicked, &dlg, [&]() {
        updateBtn->setEnabled(false);
        laterBtn->setEnabled(false);
        progress->show();
        statusLbl->show();
        statusLbl->setText(tr("Подключение..."));

        // Временный файл в Downloads
        QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                           + "/SmartClip_Installer.exe";

        auto *nam   = new QNetworkAccessManager(&dlg);
        auto *reply = nam->get(QNetworkRequest(QUrl(downloadUrl)));

        connect(reply, &QNetworkReply::downloadProgress, &dlg,
                [progress, statusLbl](qint64 received, qint64 total) {
            if (total > 0) {
                int pct = static_cast<int>(received * 100 / total);
                progress->setValue(pct);
                statusLbl->setText(QString("%1 / %2 MB")
                    .arg(received / 1048576.0, 0, 'f', 1)
                    .arg(total    / 1048576.0, 0, 'f', 1));
            }
        });

        connect(reply, &QNetworkReply::finished, &dlg,
                [reply, tempPath, &dlg, version, statusLbl]() {
            reply->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                statusLbl->setText(tr("Ошибка: ") + reply->errorString());
                return;
            }

            // Сохраняем файл
            QFile f(tempPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(reply->readAll());
                f.close();
            }

            statusLbl->setText(tr("Запуск установщика..."));

            // Запускаем установщик и выходим
            QTimer::singleShot(500, qApp, [tempPath]() {
                ShellExecuteW(nullptr, L"runas",
                              reinterpret_cast<const wchar_t*>(tempPath.utf16()),
                              nullptr, nullptr, SW_SHOW);
                QCoreApplication::quit();
            });

            dlg.accept();
        });
    });

    dlg.exec();
}

MainWindow::~MainWindow() = default;

#include "MainWindow.moc"
