#include "SettingsDialog.h"
#include "HotkeyEdit.h"
#include "core/AppSettings.h"
#include "core/Database.h"
#include "core/BugReporter.h"
#include "core/Version.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QAbstractButton>
#include <QMouseEvent>
#include <QMenu>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QSettings>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

// ─── Автозапуск ───────────────────────────────────────────────────────────────
static void applyAutostart(bool enable)
{
    QSettings reg(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat
    );
    if (enable) {
        QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        reg.setValue("SmartClip", "\"" + exePath + "\"");
    } else {
        reg.remove("SmartClip");
    }
}

// ─── Кнопка-крестик ───────────────────────────────────────────────────────────
class SdCloseBtn : public QAbstractButton {
public:
    explicit SdCloseBtn(QWidget *p) : QAbstractButton(p) {
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        bool h = underMouse();
        p.setPen(h ? QColor("#ff6666") : QColor(255, 255, 255, 150));
        QFont f = font(); f.setPixelSize(15); p.setFont(f);
        p.drawText(rect().adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter, "✕");
    }
    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QAbstractButton::leaveEvent(e); }
};

// ─── Чекбокс с крестиком вместо синего квадрата ──────────────────────────────
class SdCheckBox : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int boxSize = 16;
        const int spacing = 6;
        QRect box(0, (height() - boxSize) / 2, boxSize, boxSize);

        // Рамка и фон индикатора
        p.setPen(QPen(QColor(255, 255, 255, isChecked() ? 130 : 70), 1));
        p.setBrush(QColor(28, 28, 44, 245));
        p.drawRoundedRect(box.adjusted(0, 0, -1, -1), 3, 3);

        // Белый ✕ когда включено
        if (isChecked()) {
            p.setPen(QColor(255, 255, 255, 230));
            QFont f = font();
            f.setPixelSize(11);
            p.setFont(f);
            p.drawText(box, Qt::AlignCenter, "✕");
        }

        // Текст метки
        p.setPen(QColor(0xc0, 0xc0, 0xd0));
        p.setFont(font());
        p.drawText(rect().adjusted(boxSize + spacing, 0, 0, 0),
                   Qt::AlignVCenter | Qt::AlignLeft, text());
    }
    void enterEvent(QEnterEvent *e) override { update(); QCheckBox::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QCheckBox::leaveEvent(e); }
};

// ─── Мини-диалог (замена QMessageBox / QInputDialog) ─────────────────────────
class SdDialog : public QDialog {
public:
    SdDialog(QWidget *parent, const QString &title) : QDialog(parent) {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setMinimumWidth(340);

        auto *master = new QVBoxLayout(this);
        master->setContentsMargins(0, 0, 0, 0);
        master->setSpacing(0);

        m_titleBar = new QWidget(this);
        m_titleBar->setFixedHeight(36);
        m_titleBar->setStyleSheet("background: transparent;");
        auto *tl = new QHBoxLayout(m_titleBar);
        tl->setContentsMargins(14, 0, 2, 0);
        tl->setSpacing(0);
        auto *lbl = new QLabel(title, m_titleBar);
        lbl->setStyleSheet("color:#e0e0e0;font-size:13px;font-weight:bold;background:transparent;");
        tl->addWidget(lbl, 1);
        auto *cb = new SdCloseBtn(m_titleBar);
        connect(cb, &QAbstractButton::clicked, this, &QDialog::reject);
        tl->addWidget(cb, 0, Qt::AlignVCenter);
        master->addWidget(m_titleBar);

        auto *bw = new QWidget(this);
        bw->setStyleSheet("background: transparent;");
        auto *bwl = new QVBoxLayout(bw);
        bwl->setContentsMargins(14, 4, 14, 14);
        m_body = new QWidget(bw);
        m_body->setStyleSheet("background: transparent;");
        bwl->addWidget(m_body, 1);
        master->addWidget(bw, 1);
    }
    QWidget *body() { return m_body; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1), 12, 12);
        p.fillPath(path, QColor(18, 18, 30, 252));
        p.setPen(QPen(QColor(255,255,255,55), 2)); p.drawPath(path);
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
    QWidget *m_titleBar = nullptr;
    QWidget *m_body     = nullptr;
    QPoint   m_drag;
};

static const char *SD_OK_BTN =
    "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
    "border:1px solid rgba(120,160,255,160);border-radius:6px;"
    "padding:5px 18px;font-size:13px;min-width:60px;}"
    "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}";
static const char *SD_CANCEL_BTN =
    "QPushButton{background:rgba(28,28,44,245);color:rgba(255,255,255,200);"
    "border:1px solid rgba(255,255,255,55);border-radius:6px;"
    "padding:5px 18px;font-size:13px;min-width:60px;}"
    "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}";

static void sdInfo(QWidget *p, const QString &title, const QString &text) {
    SdDialog d(p, title);
    auto *vl = new QVBoxLayout(d.body());
    vl->setContentsMargins(0,0,0,0); vl->setSpacing(14);
    auto *lbl = new QLabel(text, d.body());
    lbl->setWordWrap(true);
    lbl->setStyleSheet("color:#d0d0d0;font-size:13px;background:transparent;");
    auto *br = new QHBoxLayout();
    auto *ok = new QPushButton(QObject::tr("ОК"), d.body());
    ok->setStyleSheet(SD_OK_BTN); ok->setCursor(Qt::PointingHandCursor);
    br->addStretch(); br->addWidget(ok);
    vl->addWidget(lbl); vl->addLayout(br);
    QObject::connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    d.exec();
}

static bool sdQuestion(QWidget *p, const QString &title, const QString &text) {
    SdDialog d(p, title);
    auto *vl = new QVBoxLayout(d.body());
    vl->setContentsMargins(0,0,0,0); vl->setSpacing(14);
    auto *lbl = new QLabel(text, d.body());
    lbl->setWordWrap(true);
    lbl->setStyleSheet("color:#d0d0d0;font-size:13px;background:transparent;");
    auto *br = new QHBoxLayout(); br->setSpacing(8);
    auto *no  = new QPushButton(QObject::tr("Нет"), d.body());
    auto *yes = new QPushButton(QObject::tr("Да"),  d.body());
    no->setStyleSheet(SD_CANCEL_BTN); yes->setStyleSheet(SD_OK_BTN);
    no->setCursor(Qt::PointingHandCursor); yes->setCursor(Qt::PointingHandCursor);
    br->addStretch(); br->addWidget(no); br->addWidget(yes);
    vl->addWidget(lbl); vl->addLayout(br);
    QObject::connect(yes, &QPushButton::clicked, &d, &QDialog::accept);
    QObject::connect(no,  &QPushButton::clicked, &d, &QDialog::reject);
    return d.exec() == QDialog::Accepted;
}

static QString sdInputText(QWidget *p, const QString &title, const QString &label) {
    SdDialog d(p, title); d.setMinimumWidth(360);
    auto *vl = new QVBoxLayout(d.body());
    vl->setContentsMargins(0,0,0,0); vl->setSpacing(10);
    auto *lbl  = new QLabel(label, d.body());
    lbl->setStyleSheet("color:#c0c0d0;font-size:13px;background:transparent;");
    auto *edit = new QLineEdit(d.body());
    edit->setStyleSheet("background:rgba(28,28,44,245);color:#f0f0f0;"
        "border:1px solid rgba(255,255,255,55);border-radius:6px;padding:5px 10px;font-size:13px;");
    auto *br = new QHBoxLayout(); br->setSpacing(8);
    auto *cancel = new QPushButton(QObject::tr("Отмена"), d.body());
    auto *ok     = new QPushButton(QObject::tr("ОК"),     d.body());
    cancel->setStyleSheet(SD_CANCEL_BTN); ok->setStyleSheet(SD_OK_BTN);
    cancel->setCursor(Qt::PointingHandCursor); ok->setCursor(Qt::PointingHandCursor);
    br->addStretch(); br->addWidget(cancel); br->addWidget(ok);
    vl->addWidget(lbl); vl->addWidget(edit); vl->addStretch(); vl->addLayout(br);
    QObject::connect(ok,     &QPushButton::clicked, &d, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    QObject::connect(edit, &QLineEdit::returnPressed, &d, &QDialog::accept);
    return d.exec() == QDialog::Accepted ? edit->text().trimmed() : QString{};
}

static QString sdInputItem(QWidget *p, const QString &title, const QString &label,
                           const QStringList &items) {
    SdDialog d(p, title); d.setMinimumWidth(360);
    auto *vl = new QVBoxLayout(d.body());
    vl->setContentsMargins(0,0,0,0); vl->setSpacing(10);
    auto *lbl   = new QLabel(label, d.body());
    lbl->setStyleSheet("color:#c0c0d0;font-size:13px;background:transparent;");
    auto *combo = new QComboBox(d.body());
    combo->addItems(items);
    combo->setStyleSheet(
        "QComboBox{background:rgba(28,28,44,245);color:#f0f0f0;"
        "border:1px solid rgba(255,255,255,55);border-radius:6px;padding:5px 10px;font-size:13px;}"
        "QComboBox QAbstractItemView{background:rgba(22,22,36,252);color:#e0e0e0;"
        "border:1px solid rgba(255,255,255,55);selection-background-color:rgba(255,255,255,18);"
        "outline:none;padding:2px;}");
    auto *br = new QHBoxLayout(); br->setSpacing(8);
    auto *cancel = new QPushButton(QObject::tr("Отмена"), d.body());
    auto *ok     = new QPushButton(QObject::tr("ОК"),     d.body());
    cancel->setStyleSheet(SD_CANCEL_BTN); ok->setStyleSheet(SD_OK_BTN);
    cancel->setCursor(Qt::PointingHandCursor); ok->setCursor(Qt::PointingHandCursor);
    br->addStretch(); br->addWidget(cancel); br->addWidget(ok);
    vl->addWidget(lbl); vl->addWidget(combo); vl->addStretch(); vl->addLayout(br);
    QObject::connect(ok,     &QPushButton::clicked, &d, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    return d.exec() == QDialog::Accepted ? combo->currentText() : QString{};
}

// ─── Диалог обратной связи ───────────────────────────────────────────────────
static const char *SD_COMBO_STYLE =
    "QComboBox{background:rgba(28,28,44,245);color:#f0f0f0;"
    "border:1px solid rgba(255,255,255,55);border-radius:6px;padding:5px 10px;font-size:13px;}"
    "QComboBox QAbstractItemView{background:rgba(22,22,36,252);color:#e0e0e0;"
    "border:1px solid rgba(255,255,255,55);selection-background-color:rgba(255,255,255,18);"
    "outline:none;padding:2px;}";
static const char *SD_TEXTEDIT_STYLE =
    "QTextEdit{background:rgba(28,28,44,245);color:#f0f0f0;"
    "border:1px solid rgba(255,255,255,55);border-radius:6px;"
    "padding:6px 8px;font-size:13px;}";

static void showFeedbackDialog(QWidget *parent)
{
#define FTR(s) QCoreApplication::translate("SettingsDialog", s)

    SdDialog dlg(parent, FTR("Обратная связь"));
    dlg.setMinimumWidth(500);

    auto *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    // Тип: жалоба / пожелание
    auto *typeRow = new QHBoxLayout;
    typeRow->setSpacing(10);
    auto *typeLbl = new QLabel(FTR("Тип:"), dlg.body());
    typeLbl->setStyleSheet("color:#c0c0d0;font-size:13px;background:transparent;");
    auto *typeBox = new QComboBox(dlg.body());
    typeBox->addItem(FTR("🔴  Жалоба"));
    typeBox->addItem(FTR("💡  Пожелание"));
    typeBox->setStyleSheet(SD_COMBO_STYLE);
    typeBox->setCursor(Qt::PointingHandCursor);
    typeRow->addWidget(typeLbl);
    typeRow->addWidget(typeBox, 1);
    vl->addLayout(typeRow);

    // Описание
    auto *descLbl = new QLabel(FTR("Описание:"), dlg.body());
    descLbl->setStyleSheet("color:#c0c0d0;font-size:13px;background:transparent;");
    vl->addWidget(descLbl);

    auto *descEdit = new QTextEdit(dlg.body());
    descEdit->setPlaceholderText(FTR("Опишите проблему или пожелание подробнее..."));
    descEdit->setMinimumHeight(100);
    descEdit->setMaximumHeight(140);
    descEdit->setStyleSheet(SD_TEXTEDIT_STYLE);
    vl->addWidget(descEdit);

    // ── Вложения ─────────────────────────────────────────────────────────────
    auto *attachHeaderRow = new QHBoxLayout;
    attachHeaderRow->setSpacing(8);

    auto *attachLbl = new QLabel(FTR("Вложения:"), dlg.body());
    attachLbl->setStyleSheet("color:#c0c0d0;font-size:13px;background:transparent;");

    auto *statsLbl = new QLabel(dlg.body());
    statsLbl->setStyleSheet("color:rgba(180,180,200,180);font-size:11px;background:transparent;");

    auto *attachBtn = new QPushButton(FTR("📎  Прикрепить"), dlg.body());
    attachBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(200,200,255,220);"
        "border:1px solid rgba(160,160,255,120);border-radius:6px;"
        "padding:4px 12px;font-size:12px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}");
    attachBtn->setCursor(Qt::PointingHandCursor);

    attachHeaderRow->addWidget(attachLbl);
    attachHeaderRow->addWidget(statsLbl, 1);
    attachHeaderRow->addWidget(attachBtn);
    vl->addLayout(attachHeaderRow);

    auto *attachList = new QListWidget(dlg.body());
    attachList->setFixedHeight(80);
    attachList->setStyleSheet(
        "QListWidget{background:rgba(22,22,36,200);color:#e0e0e0;"
        "border:1px solid rgba(255,255,255,35);border-radius:6px;"
        "font-size:12px;outline:none;}"
        "QListWidget::item{padding:3px 6px;}"
        "QListWidget::item:selected{background:rgba(255,255,255,18);color:#fff;}");
    attachList->setContextMenuPolicy(Qt::CustomContextMenu);
    vl->addWidget(attachList);

    auto *attachHint = new QLabel(
        FTR("ПКМ по файлу — удалить  ·  до 4 мин. для видео  ·  макс. 50 МБ"),
        dlg.body());
    attachHint->setStyleSheet("color:rgba(160,160,180,150);font-size:11px;background:transparent;");
    vl->addWidget(attachHint);

    QList<QString> attached;

    auto updateStats = [&]() {
        int photos = 0, videos = 0, audios = 0;
        for (const QString &p : attached) {
            switch (BugReporter::kindOfFile(p)) {
                case BugReporter::FileKind::Photo: ++photos; break;
                case BugReporter::FileKind::Video: ++videos; break;
                case BugReporter::FileKind::Audio: ++audios; break;
            }
        }
        statsLbl->setText(
            QString::fromUtf8("📷 %1/%2   🎬 %3/%4   🎵 %5/%6")
                .arg(photos).arg(BugReporter::MAX_PHOTOS)
                .arg(videos).arg(BugReporter::MAX_VIDEOS)
                .arg(audios).arg(BugReporter::MAX_AUDIOS));
    };
    updateStats();

    QObject::connect(attachBtn, &QPushButton::clicked, [&]() {
        const QString filter =
            FTR("Медиафайлы (*.png *.jpg *.jpeg *.gif *.webp "
                "*.mp4 *.mov *.avi *.mkv *.webm "
                "*.mp3 *.ogg *.wav *.m4a *.flac *.aac);;"
                "Все файлы (*.*)");
        QStringList files = QFileDialog::getOpenFileNames(
            &dlg, FTR("Прикрепить файлы"), QString(), filter);

        for (const QString &path : files) {
            if (attached.contains(path)) continue;
            const auto kind = BugReporter::kindOfFile(path);
            int photos = 0, videos = 0, audios = 0;
            for (const QString &p : attached) {
                switch (BugReporter::kindOfFile(p)) {
                    case BugReporter::FileKind::Photo: ++photos; break;
                    case BugReporter::FileKind::Video: ++videos; break;
                    case BugReporter::FileKind::Audio: ++audios; break;
                }
            }
            if (kind == BugReporter::FileKind::Photo && photos >= BugReporter::MAX_PHOTOS) continue;
            if (kind == BugReporter::FileKind::Video && videos >= BugReporter::MAX_VIDEOS) continue;
            if (kind == BugReporter::FileKind::Audio && audios >= BugReporter::MAX_AUDIOS) continue;
            QString icon;
            switch (kind) {
                case BugReporter::FileKind::Photo: icon = QString::fromUtf8("📷"); break;
                case BugReporter::FileKind::Video: icon = QString::fromUtf8("🎬"); break;
                case BugReporter::FileKind::Audio: icon = QString::fromUtf8("🎵"); break;
            }
            attached.append(path);
            attachList->addItem(icon + "  " + QFileInfo(path).fileName());
        }
        updateStats();
    });

    QObject::connect(attachList, &QListWidget::customContextMenuRequested,
                     [&](const QPoint &pos) {
        QListWidgetItem *item = attachList->itemAt(pos);
        if (!item) return;
        attached.removeAt(attachList->row(item));
        delete item;
        updateStats();
    });

    const bool canSend = BugReporter::canSend();
    if (!canSend) {
        const int hoursLeft = BugReporter::COOLDOWN_HOURS - BugReporter::hoursSinceLast();
        auto *infoLbl = new QLabel(
            FTR("⏳ Следующий репорт доступен через %1 ч.").arg(hoursLeft),
            dlg.body());
        infoLbl->setStyleSheet("color:#ffaa44;font-size:12px;background:transparent;");
        vl->addWidget(infoLbl);
    }

    auto *btnRow    = new QHBoxLayout;
    auto *cancelBtn = new QPushButton(FTR("Отмена"), dlg.body());
    auto *sendBtn   = new QPushButton(FTR("📩  Отправить"), dlg.body());
    cancelBtn->setStyleSheet(SD_CANCEL_BTN);
    sendBtn->setStyleSheet(SD_OK_BTN);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    sendBtn->setCursor(Qt::PointingHandCursor);
    sendBtn->setEnabled(false);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(sendBtn);
    vl->addLayout(btnRow);

    QObject::connect(descEdit, &QTextEdit::textChanged, [&]() {
        sendBtn->setEnabled(canSend &&
                            descEdit->toPlainText().trimmed().length() >= 10);
    });

    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    QObject::connect(sendBtn, &QPushButton::clicked, [&, parent]() {
        const BugReporter::Type selType = (typeBox->currentIndex() == 0)
            ? BugReporter::Type::Bug : BugReporter::Type::Feature;
        sendBtn->setEnabled(false);
        sendBtn->setText(FTR("Отправка..."));

        auto *reporter = new BugReporter(parent);
        reporter->send(selType, descEdit->toPlainText(), attached);

        QObject::connect(reporter, &BugReporter::sent, [&, reporter, parent]() {
            reporter->deleteLater();
            dlg.accept();
            sdInfo(parent, FTR("Спасибо!"),
                   FTR("Сообщение отправлено. Мы обязательно его рассмотрим!"));
        });
        QObject::connect(reporter, &BugReporter::failed, [&, reporter, parent](const QString &err) {
            reporter->deleteLater();
            sendBtn->setEnabled(true);
            sendBtn->setText(FTR("📩  Отправить"));
            sdInfo(parent, FTR("Ошибка отправки"),
                   FTR("Не удалось отправить: %1").arg(err));
        });
    });

    dlg.exec();
#undef FTR
}

// ─── Конструктор ─────────────────────────────────────────────────────────────
SettingsDialog::SettingsDialog(Database *db, QWidget *parent)
    : QDialog(parent)
{
    Q_UNUSED(db)

    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(tr("Настройки SmartClip"));
    setMinimumSize(520, 560);

    // ── Глобальный стиль ──────────────────────────────────────────────────
    setStyleSheet(
        "QWidget { background: transparent; }"
        // Вкладки
        "QTabWidget::pane {"
        "  background: rgba(20,20,32,210);"
        "  border: 1px solid rgba(255,255,255,35);"
        "  border-radius: 8px; padding: 2px; }"
        "QTabBar::tab {"
        "  background: rgba(20,20,32,220); color: rgba(255,255,255,130);"
        "  padding: 5px 14px;"
        "  border: 1px solid rgba(255,255,255,50);"
        "  border-radius: 6px; margin-right: 3px; font-size: 12px; }"
        "QTabBar::tab:selected {"
        "  background: rgba(40,40,58,245); color: #fff;"
        "  border: 1px solid rgba(255,255,255,80); }"
        "QTabBar::tab:hover:!selected {"
        "  border: 1px solid rgba(255,255,255,70);"
        "  color: rgba(255,255,255,200); }"
        // Группы
        "QGroupBox {"
        "  color: rgba(255,255,255,180);"
        "  border: 1px solid rgba(255,255,255,30);"
        "  border-radius: 8px; margin-top: 10px; padding-top: 10px; font-size: 12px; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 10px; padding: 0 6px; }"
        // Метки
        "QLabel { color: #c0c0d0; font-size: 12px; background: transparent; }"
        // Чекбоксы
        "QCheckBox { color: #c0c0d0; font-size: 12px; spacing: 6px; }"
        // SpinBox
        "QSpinBox {"
        "  background: rgba(28,28,44,245); color: #f0f0f0;"
        "  border: 1px solid rgba(255,255,255,55);"
        "  border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QSpinBox::up-button, QSpinBox::down-button {"
        "  background: rgba(40,40,60,200); border: none; width: 18px; }"
        // ComboBox
        "QComboBox {"
        "  background: rgba(28,28,44,245); color: #f0f0f0;"
        "  border: 1px solid rgba(255,255,255,55);"
        "  border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView {"
        "  background: rgba(22,22,36,252); color: #e0e0e0;"
        "  border: 1px solid rgba(255,255,255,55);"
        "  selection-background-color: rgba(255,255,255,18); outline: none; padding: 2px; }"
        // LineEdit
        "QLineEdit {"
        "  background: rgba(28,28,44,245); color: #f0f0f0;"
        "  border: 1px solid rgba(255,255,255,55);"
        "  border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QLineEdit:focus { border: 1px solid rgba(255,255,255,90); }"
        // ListWidget
        "QListWidget {"
        "  background: rgba(22,22,36,200); color: #e0e0e0;"
        "  border: 1px solid rgba(255,255,255,40);"
        "  border-radius: 6px; font-size: 12px; outline: none; }"
        "QListWidget::item { padding: 3px 6px; }"
        "QListWidget::item:selected { background: rgba(255,255,255,18); color: #fff; }"
        // Кнопки (по умолчанию)
        "QPushButton {"
        "  background: rgba(28,28,44,245); color: rgba(255,255,255,200);"
        "  border: 1px solid rgba(255,255,255,55);"
        "  border-radius: 5px; padding: 6px 16px; font-size: 12px; }"
        "QPushButton:hover {"
        "  background: rgba(40,40,62,245);"
        "  border: 1px solid rgba(255,255,255,90); color: #fff; }"
        // Кнопка «Сохранить»
        "QPushButton#saveBtn {"
        "  background: rgba(30,80,40,220); color: #aaffaa;"
        "  border: 1px solid rgba(100,200,100,80); }"
        "QPushButton#saveBtn:hover { background: rgba(40,120,55,240); color: #fff; }"
        // ScrollBar
        "QScrollBar:vertical { background: transparent; width: 4px; margin: 4px 0; }"
        "QScrollBar::handle:vertical {"
        "  background: rgba(255,255,255,45); border-radius: 2px; min-height: 28px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollArea { background: transparent; border: none; }"
    );

    // ── Главный лейаут ────────────────────────────────────────────────────
    auto *master = new QVBoxLayout(this);
    master->setContentsMargins(0, 0, 0, 0);
    master->setSpacing(0);

    // Шапка
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(36);
    m_titleBar->setStyleSheet("background: transparent;");
    auto *tl = new QHBoxLayout(m_titleBar);
    tl->setContentsMargins(14, 0, 2, 0);
    tl->setSpacing(0);
    auto *titleLabel = new QLabel(tr("Настройки SmartClip"), m_titleBar);
    titleLabel->setStyleSheet("color:#e0e0e0;font-size:13px;font-weight:bold;background:transparent;");
    tl->addWidget(titleLabel, 1);
    auto *closeBtn = new SdCloseBtn(m_titleBar);
    connect(closeBtn, &SdCloseBtn::clicked, this, &QDialog::reject);
    tl->addWidget(closeBtn, 0, Qt::AlignVCenter);
    master->addWidget(m_titleBar);

    // Тело
    auto *body = new QWidget(this);
    body->setStyleSheet("background: transparent;");
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 4, 12, 12);
    bodyLayout->setSpacing(10);
    master->addWidget(body, 1);

    auto *tabs = new QTabWidget(body);

    // ═══════════════════════════════════════════════════════════════════════
    // Вкладка 1: Система
    // ═══════════════════════════════════════════════════════════════════════
    auto *sysTab = new QWidget();
    auto *sysLayout = new QVBoxLayout(sysTab);
    sysLayout->setContentsMargins(14, 14, 14, 14);
    sysLayout->setSpacing(12);

    // Горячая клавиша
    auto *hkGroup  = new QGroupBox(tr("Горячая клавиша"), sysTab);
    auto *hkLayout = new QVBoxLayout(hkGroup);
    hkLayout->setSpacing(8);
    auto *hkHint = new QLabel(
        tr("Комбинация для открытия SmartClip.\n"
           "Кликни по полю ниже, затем нажми нужную комбинацию."), hkGroup);
    hkHint->setWordWrap(true);
    hkHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    auto *hotkeyEdit = new HotkeyEdit(hkGroup);
    hotkeyEdit->setText(AppSettings::get().mainHotkey());
    hkLayout->addWidget(hkHint);
    hkLayout->addWidget(hotkeyEdit);

    // Автозапуск
    auto *startGroup  = new QGroupBox(tr("Запуск системы"), sysTab);
    auto *startLayout = new QVBoxLayout(startGroup);
    auto *autostartCheck = new SdCheckBox(tr("Запускать SmartClip вместе с Windows"), startGroup);
    autostartCheck->setChecked(AppSettings::get().autostart());
    auto *startHint = new QLabel(
        tr("SmartClip будет автоматически стартовать при входе в систему."), startGroup);
    startHint->setWordWrap(true);
    startHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    startLayout->addWidget(autostartCheck);
    startLayout->addWidget(startHint);

    // Язык
    auto *langGroup  = new QGroupBox(tr("Язык интерфейса"), sysTab);
    auto *langLayout = new QVBoxLayout(langGroup);
    langLayout->setSpacing(8);
    auto *langCombo = new QComboBox(langGroup);
    langCombo->addItem("Русский",    "ru");
    langCombo->addItem("Українська", "ua");
    langCombo->addItem("English",    "en");
    langCombo->addItem("Deutsch",    "de");
    QString curLang = AppSettings::get().language();
    for (int i = 0; i < langCombo->count(); ++i)
        if (langCombo->itemData(i).toString() == curLang) { langCombo->setCurrentIndex(i); break; }
    auto *langHint = new QLabel(
        tr("Изменение языка требует перезапуска приложения."), langGroup);
    langHint->setWordWrap(true);
    langHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    langLayout->addWidget(langCombo);
    langLayout->addWidget(langHint);

    // Внешний вид
    auto *uiGroup  = new QGroupBox(tr("Внешний вид"), sysTab);
    auto *uiLayout = new QVBoxLayout(uiGroup);
    auto *blurCheck = new SdCheckBox(tr("Тёмный фон за колонками"), uiGroup);
    blurCheck->setChecked(AppSettings::get().solidPanels());
    auto *blurHint = new QLabel(
        tr("Добавляет тёмную полупрозрачную подложку за левой, правой колонками и панелями.\nДля тех, кто предпочитает непрозрачный интерфейс."), uiGroup);
    blurHint->setWordWrap(true);
    blurHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    uiLayout->addWidget(blurCheck);
    uiLayout->addWidget(blurHint);

    sysLayout->addWidget(hkGroup);
    sysLayout->addWidget(startGroup);
    sysLayout->addWidget(langGroup);
    sysLayout->addWidget(uiGroup);
    sysLayout->addStretch();

    tabs->addTab(sysTab, tr("⚙️  Система"));

    // ═══════════════════════════════════════════════════════════════════════
    // Вкладка 2: История (с прокруткой — много контента)
    // ═══════════════════════════════════════════════════════════════════════
    auto *histScroll = new QScrollArea();
    histScroll->setWidgetResizable(true);
    histScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *histTab    = new QWidget();
    auto *histLayout = new QVBoxLayout(histTab);
    histLayout->setContentsMargins(14, 14, 14, 14);
    histLayout->setSpacing(12);

    // Лимит записей
    auto *limitGroup  = new QGroupBox(tr("Размер буфера"), histTab);
    auto *limitLayout = new QFormLayout(limitGroup);
    limitLayout->setSpacing(8);
    auto *maxRecordsSpin = new QSpinBox(limitGroup);
    maxRecordsSpin->setRange(10, 2000);
    maxRecordsSpin->setSingleStep(50);
    maxRecordsSpin->setValue(AppSettings::get().maxHistoryRecords());
    auto *limitHint = new QLabel(
        tr("Сколько записей хранить в истории. При превышении\nстарые записи удаляются автоматически."), limitGroup);
    limitHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    limitLayout->addRow(tr("Максимум записей:"), maxRecordsSpin);
    limitLayout->addRow(limitHint);

    // Поведение
    auto *behavGroup  = new QGroupBox(tr("Поведение"), histTab);
    auto *behavLayout = new QVBoxLayout(behavGroup);
    behavLayout->setSpacing(10);
    auto *saveImagesCheck = new SdCheckBox(tr("Сохранять изображения из буфера"), behavGroup);
    saveImagesCheck->setChecked(AppSettings::get().saveImages());
    auto *dedupCheck = new SdCheckBox(
        tr("Дедупликация — не сохранять если такой текст уже есть"), behavGroup);
    dedupCheck->setChecked(AppSettings::get().deduplication());
    auto *minLenLayout = new QHBoxLayout();
    auto *minLenLabel  = new QLabel(tr("Минимальная длина текста:"), behavGroup);
    auto *minLenSpin   = new QSpinBox(behavGroup);
    minLenSpin->setRange(1, 500);
    minLenSpin->setValue(AppSettings::get().minTextLength());
    minLenSpin->setFixedWidth(80);
    auto *minLenHint = new QLabel(tr("симв."), behavGroup);
    minLenHint->setStyleSheet("color: rgba(255,255,255,100);");
    minLenLayout->addWidget(minLenLabel);
    minLenLayout->addWidget(minLenSpin);
    minLenLayout->addWidget(minLenHint);
    minLenLayout->addStretch();
    behavLayout->addWidget(saveImagesCheck);
    behavLayout->addWidget(dedupCheck);
    behavLayout->addLayout(minLenLayout);

    // Автоочистка
    auto *cleanGroup  = new QGroupBox(tr("Автоочистка"), histTab);
    auto *cleanLayout = new QHBoxLayout(cleanGroup);
    cleanLayout->setSpacing(8);
    auto *cleanLabel = new QLabel(tr("Удалять записи старше"), cleanGroup);
    auto *cleanSpin  = new QSpinBox(cleanGroup);
    cleanSpin->setRange(0, 365);
    cleanSpin->setValue(AppSettings::get().autocleanDays());
    cleanSpin->setSpecialValueText(tr("Выкл"));
    cleanSpin->setFixedWidth(90);
    auto *cleanDaysLabel = new QLabel(tr("дней"), cleanGroup);
    cleanDaysLabel->setStyleSheet("color: rgba(255,255,255,100);");
    cleanLayout->addWidget(cleanLabel);
    cleanLayout->addWidget(cleanSpin);
    cleanLayout->addWidget(cleanDaysLabel);
    cleanLayout->addStretch();

    // Исключения приложений
    auto *exclGroup  = new QGroupBox(tr("Исключения приложений"), histTab);
    auto *exclLayout = new QVBoxLayout(exclGroup);
    exclLayout->setSpacing(6);
    auto *exclHint = new QLabel(
        tr("Из этих приложений скопированное не сохраняется в историю.\n"
           "Например: KeePass, 1Password и другие менеджеры паролей."), exclGroup);
    exclHint->setWordWrap(true);
    exclHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    auto *exclList = new QListWidget(exclGroup);
    exclList->setFixedHeight(100);
    for (const QString &app : AppSettings::get().excludedApps())
        exclList->addItem(app);
    auto *exclBtnRow    = new QHBoxLayout();
    auto *addFromHistBtn = new QPushButton(tr("+ Из истории"), exclGroup);
    auto *addManualBtn   = new QPushButton(tr("+ Вручную"),    exclGroup);
    auto *exclDelBtn     = new QPushButton(tr("✕ Удалить"),    exclGroup);
    addFromHistBtn->setStyleSheet(
        "QPushButton{background:rgba(20,50,25,220);color:#aaddaa;"
        "border:1px solid rgba(80,160,80,80);border-radius:5px;padding:4px 12px;font-size:11px;}"
        "QPushButton:hover{background:rgba(30,80,38,240);color:#fff;}");
    addManualBtn->setStyleSheet(
        "QPushButton{background:rgba(20,25,55,220);color:#aaaadd;"
        "border:1px solid rgba(80,80,160,80);border-radius:5px;padding:4px 12px;font-size:11px;}"
        "QPushButton:hover{background:rgba(28,32,80,240);color:#fff;}");
    exclDelBtn->setStyleSheet(
        "QPushButton{background:rgba(55,18,18,220);color:#ddaaaa;"
        "border:1px solid rgba(160,60,60,80);border-radius:5px;padding:4px 12px;font-size:11px;}"
        "QPushButton:hover{background:rgba(90,25,25,240);color:#fff;}");
    addFromHistBtn->setCursor(Qt::PointingHandCursor);
    addManualBtn->setCursor(Qt::PointingHandCursor);
    exclDelBtn->setCursor(Qt::PointingHandCursor);
    exclBtnRow->addWidget(addFromHistBtn);
    exclBtnRow->addWidget(addManualBtn);
    exclBtnRow->addStretch();
    exclBtnRow->addWidget(exclDelBtn);
    exclLayout->addWidget(exclHint);
    exclLayout->addWidget(exclList);
    exclLayout->addLayout(exclBtnRow);

    connect(addFromHistBtn, &QPushButton::clicked, [=]() {
        QStringList apps = db->getAppNames();
        if (apps.isEmpty()) {
            sdInfo(this, tr("История пуста"),
                   tr("В истории пока нет записей с именами приложений."));
            return;
        }
        for (int i = 0; i < exclList->count(); ++i)
            apps.removeAll(exclList->item(i)->text());
        if (apps.isEmpty()) {
            sdInfo(this, tr("Все добавлены"),
                   tr("Все приложения из истории уже в списке исключений."));
            return;
        }
        QString chosen = sdInputItem(this, tr("Добавить исключение"),
                                     tr("Выберите приложение:"), apps);
        if (!chosen.isEmpty()) exclList->addItem(chosen);
    });

    connect(addManualBtn, &QPushButton::clicked, [=]() {
        QString name = sdInputText(this, tr("Добавить исключение"),
                                   tr("Имя .exe файла (без расширения):"));
        if (!name.isEmpty()) exclList->addItem(name);
    });

    connect(exclDelBtn, &QPushButton::clicked, [=]() {
        for (auto *item : exclList->selectedItems()) delete item;
    });

    // Формат изображений
    auto *imgGroup  = new QGroupBox(tr("Формат изображений"), histTab);
    auto *imgLayout = new QVBoxLayout(imgGroup);
    imgLayout->setSpacing(8);
    auto *fmtRow   = new QHBoxLayout();
    auto *fmtLabel = new QLabel(tr("Формат:"), imgGroup);
    auto *fmtCombo = new QComboBox(imgGroup);
    fmtCombo->addItem(tr("PNG  (без потерь)"),   "PNG");
    fmtCombo->addItem(tr("JPEG (меньше места)"), "JPEG");
    fmtCombo->setCurrentIndex(AppSettings::get().imageFormat() == "JPEG" ? 1 : 0);
    fmtRow->addWidget(fmtLabel); fmtRow->addWidget(fmtCombo); fmtRow->addStretch();
    auto *qualRow   = new QHBoxLayout();
    auto *qualLabel = new QLabel(tr("Качество JPEG:"), imgGroup);
    auto *qualSpin  = new QSpinBox(imgGroup);
    qualSpin->setRange(1, 100);
    qualSpin->setValue(AppSettings::get().imageQuality());
    qualSpin->setSuffix("%");
    qualSpin->setFixedWidth(75);
    qualRow->addWidget(qualLabel); qualRow->addWidget(qualSpin); qualRow->addStretch();
    qualSpin->setEnabled(fmtCombo->currentIndex() == 1);
    connect(fmtCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [=](int idx){ qualSpin->setEnabled(idx == 1); });
    imgLayout->addLayout(fmtRow);
    imgLayout->addLayout(qualRow);

    histLayout->addWidget(limitGroup);
    histLayout->addWidget(behavGroup);
    histLayout->addWidget(cleanGroup);
    histLayout->addWidget(imgGroup);
    histLayout->addWidget(exclGroup);
    histLayout->addStretch();

    histScroll->setWidget(histTab);
    tabs->addTab(histScroll, tr("📋  История"));

    // ═══════════════════════════════════════════════════════════════════════
    // Вкладка 3: Закрепы
    // ═══════════════════════════════════════════════════════════════════════
    auto *pinsTab    = new QWidget();
    auto *pinsLayout = new QVBoxLayout(pinsTab);
    pinsLayout->setContentsMargins(14, 14, 14, 14);
    pinsLayout->setSpacing(12);

    auto *pinsGroup  = new QGroupBox(tr("Поведение"), pinsTab);
    auto *pinsGrpLay = new QVBoxLayout(pinsGroup);
    pinsGrpLay->setSpacing(10);
    auto *noNameCheck = new SdCheckBox(tr("Не спрашивать название при закреплении"), pinsGroup);
    noNameCheck->setChecked(AppSettings::get().pinsNoName());
    auto *noNameHint = new QLabel(
        tr("Закреп сохраняется без имени, диалог названия не появляется."), pinsGroup);
    noNameHint->setWordWrap(true);
    noNameHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px; margin-bottom: 6px;");
    auto *noFolderCheck = new SdCheckBox(tr("Не спрашивать папку при закреплении"), pinsGroup);
    noFolderCheck->setChecked(AppSettings::get().pinsNoFolder());
    auto *noFolderHint = new QLabel(
        tr("Закреп сохраняется без папки (в «Все»), диалог выбора папки не появляется."), pinsGroup);
    noFolderHint->setWordWrap(true);
    noFolderHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    pinsGrpLay->addWidget(noNameCheck);
    pinsGrpLay->addWidget(noNameHint);
    pinsGrpLay->addWidget(noFolderCheck);
    pinsGrpLay->addWidget(noFolderHint);
    pinsLayout->addWidget(pinsGroup);

    auto *recentGroup  = new QGroupBox(tr("Вкладка «Все»"), pinsTab);
    auto *recentLayout = new QFormLayout(recentGroup);
    recentLayout->setSpacing(8);
    auto *recentLimitSpin = new QSpinBox(recentGroup);
    recentLimitSpin->setRange(5, 500);
    recentLimitSpin->setSingleStep(10);
    recentLimitSpin->setValue(AppSettings::get().recentPinsLimit());
    auto *recentHint = new QLabel(
        tr("Сколько недавно использованных закрепов из папок\nотображается во вкладке «Все»."), recentGroup);
    recentHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    recentLayout->addRow(tr("Лимит недавних:"), recentLimitSpin);
    recentLayout->addRow(recentHint);
    pinsLayout->addWidget(recentGroup);
    pinsLayout->addStretch();

    tabs->addTab(pinsTab, tr("📌  Закрепы"));

    // ═══════════════════════════════════════════════════════════════════════
    // Вкладка 4: Экспорт / Импорт
    // ═══════════════════════════════════════════════════════════════════════
    auto *ioTab    = new QWidget();
    auto *ioLayout = new QVBoxLayout(ioTab);
    ioLayout->setContentsMargins(14, 14, 14, 14);
    ioLayout->setSpacing(12);

    auto *exportGroup  = new QGroupBox(tr("Экспорт"), ioTab);
    auto *exportLayout = new QVBoxLayout(exportGroup);
    exportLayout->setSpacing(8);
    auto *exportHint = new QLabel(
        tr("Сохранить закрепы, профили и настройки в JSON-файл.\n"
           "История не экспортируется (только ваши закрепы и профили)."), exportGroup);
    exportHint->setWordWrap(true);
    exportHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    auto *exportBtn = new QPushButton(tr("💾  Экспортировать..."), exportGroup);
    exportBtn->setStyleSheet(
        "QPushButton{background:rgba(20,50,25,220);color:#aaddaa;"
        "border:1px solid rgba(80,160,80,80);border-radius:5px;padding:7px 18px;font-size:12px;}"
        "QPushButton:hover{background:rgba(30,80,38,240);color:#fff;}");
    exportBtn->setCursor(Qt::PointingHandCursor);
    exportLayout->addWidget(exportHint);
    exportLayout->addWidget(exportBtn, 0, Qt::AlignLeft);

    auto *importGroup  = new QGroupBox(tr("Импорт"), ioTab);
    auto *importLayout = new QVBoxLayout(importGroup);
    importLayout->setSpacing(8);
    auto *importHint = new QLabel(
        tr("Загрузить закрепы, профили и настройки из файла бекапа.\n"
           "Существующие данные не удаляются — новые добавляются к ним."), importGroup);
    importHint->setWordWrap(true);
    importHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    auto *importBtn = new QPushButton(tr("📂  Импортировать..."), importGroup);
    importBtn->setStyleSheet(
        "QPushButton{background:rgba(20,25,55,220);color:#aaaadd;"
        "border:1px solid rgba(80,80,160,80);border-radius:5px;padding:7px 18px;font-size:12px;}"
        "QPushButton:hover{background:rgba(28,32,80,240);color:#fff;}");
    importBtn->setCursor(Qt::PointingHandCursor);
    importLayout->addWidget(importHint);
    importLayout->addWidget(importBtn, 0, Qt::AlignLeft);

    auto *backupGroup  = new QGroupBox(tr("Автобекап"), ioTab);
    auto *backupLayout = new QVBoxLayout(backupGroup);
    backupLayout->setSpacing(8);
    auto *backupCheck = new SdCheckBox(tr("Автоматически сохранять бекап"), backupGroup);
    backupCheck->setChecked(AppSettings::get().autoBackup());
    auto *backupDaysRow   = new QHBoxLayout();
    auto *backupDaysLabel = new QLabel(tr("Каждые"), backupGroup);
    auto *backupDaysSpin  = new QSpinBox(backupGroup);
    backupDaysSpin->setRange(1, 30);
    backupDaysSpin->setValue(AppSettings::get().autoBackupDays());
    backupDaysSpin->setSuffix(tr(" дн."));
    backupDaysSpin->setFixedWidth(80);
    backupDaysRow->addWidget(backupDaysLabel);
    backupDaysRow->addWidget(backupDaysSpin);
    backupDaysRow->addStretch();
    auto *backupPathRow   = new QHBoxLayout();
    auto *backupPathEdit  = new QLineEdit(backupGroup);
    backupPathEdit->setText(AppSettings::get().autoBackupPath());
    backupPathEdit->setPlaceholderText(tr("Папка для бекапов..."));
    auto *backupBrowseBtn = new QPushButton("📁", backupGroup);
    backupBrowseBtn->setFixedWidth(32);
    backupBrowseBtn->setToolTip(tr("Выбрать папку"));
    backupPathRow->addWidget(backupPathEdit);
    backupPathRow->addWidget(backupBrowseBtn);
    connect(backupBrowseBtn, &QPushButton::clicked, [=]() {
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("Папка для автобекапов"), backupPathEdit->text());
        if (!dir.isEmpty()) backupPathEdit->setText(dir);
    });
    auto toggleBackup = [=](bool on) {
        backupDaysSpin->setEnabled(on);
        backupPathEdit->setEnabled(on);
        backupBrowseBtn->setEnabled(on);
    };
    toggleBackup(backupCheck->isChecked());
    connect(backupCheck, &QCheckBox::toggled, toggleBackup);
    backupLayout->addWidget(backupCheck);
    backupLayout->addLayout(backupDaysRow);
    backupLayout->addLayout(backupPathRow);

    ioLayout->addWidget(exportGroup);
    ioLayout->addWidget(importGroup);
    ioLayout->addWidget(backupGroup);
    ioLayout->addStretch();

    tabs->addTab(ioTab, tr("💾  Экспорт / Импорт"));

    // ═══════════════════════════════════════════════════════════════════════
    // Вкладка 5: Бета
    // ═══════════════════════════════════════════════════════════════════════
    auto *betaTab    = new QWidget();
    auto *betaLayout = new QVBoxLayout(betaTab);
    betaLayout->setContentsMargins(14, 14, 14, 14);
    betaLayout->setSpacing(12);

    auto *betaHintLabel = new QLabel(
        tr("⚡ Бета-функции — могут работать нестабильно.\n"
           "Присылай баги, будем фиксить!"), betaTab);
    betaHintLabel->setWordWrap(true);
    betaHintLabel->setStyleSheet(
        "color:#ffcc44;font-size:11px;"
        "background:rgba(50,40,10,120);border:1px solid rgba(200,160,0,60);"
        "border-radius:6px;padding:7px 10px;");

    auto *videoGroup  = new QGroupBox(tr("Автозахват записей экрана"), betaTab);
    auto *videoLayout = new QVBoxLayout(videoGroup);
    videoLayout->setSpacing(8);
    auto *videoCheck = new SdCheckBox(
        tr("Автоматически добавлять новые записи в историю"), videoGroup);
    videoCheck->setChecked(AppSettings::get().videoMonitorEnabled());
    auto *videoHint = new QLabel(
        tr("Следит за папкой видеозаписей. Новое видео (Xbox Game Bar,\n"
           "Snipping Tool) появится в истории SmartClip автоматически.\n"
           "Клик по карточке — вставить файл в Telegram, Discord и т.д."),
        videoGroup);
    videoHint->setWordWrap(true);
    videoHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    auto *videoPathRow  = new QHBoxLayout();
    auto *videoPathEdit = new QLineEdit(videoGroup);
    videoPathEdit->setText(AppSettings::get().videoMonitorPath());
    auto *videoBrowseBtn = new QPushButton("📁", videoGroup);
    videoBrowseBtn->setFixedWidth(32);
    videoBrowseBtn->setToolTip(tr("Выбрать папку"));
    videoPathRow->addWidget(videoPathEdit);
    videoPathRow->addWidget(videoBrowseBtn);
    connect(videoBrowseBtn, &QPushButton::clicked, [=]() {
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("Папка с видеозаписями"), videoPathEdit->text());
        if (!dir.isEmpty()) videoPathEdit->setText(dir);
    });
    auto toggleVideo = [=](bool on) {
        videoPathEdit->setEnabled(on);
        videoBrowseBtn->setEnabled(on);
    };
    toggleVideo(videoCheck->isChecked());
    connect(videoCheck, &QCheckBox::toggled, toggleVideo);
    videoLayout->addWidget(videoCheck);
    videoLayout->addWidget(videoHint);
    videoLayout->addLayout(videoPathRow);

    // Кнопка обратной связи
    auto *feedbackBtn = new QPushButton(
        tr("📩  Сообщить об ошибке / пожелании"), betaTab);
    feedbackBtn->setStyleSheet(
        "QPushButton{background:rgba(28,28,44,245);color:rgba(180,220,255,230);"
        "border:1px solid rgba(120,160,255,140);border-radius:8px;"
        "padding:8px 18px;font-size:13px;}"
        "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;"
        "border-color:rgba(160,200,255,200);}");
    feedbackBtn->setCursor(Qt::PointingHandCursor);
    connect(feedbackBtn, &QPushButton::clicked, this, [this]() {
        showFeedbackDialog(this);
    });

    auto *versionLbl = new QLabel(
        tr("SmartClip v%1").arg(QLatin1String(APP_VERSION)), betaTab);
    versionLbl->setStyleSheet(
        "color:rgba(255,255,255,80);font-size:11px;background:transparent;");
    versionLbl->setAlignment(Qt::AlignRight);

    betaLayout->addWidget(betaHintLabel);
    betaLayout->addWidget(videoGroup);
    betaLayout->addWidget(feedbackBtn);
    betaLayout->addStretch();
    betaLayout->addWidget(versionLbl);
    betaLayout->addStretch();

    tabs->addTab(betaTab, tr("⚡  Специальные возможности"));

    // ── Логика экспорта ───────────────────────────────────────────────────
    connect(exportBtn, &QPushButton::clicked, [=]() {
        QString path = QFileDialog::getSaveFileName(
            this, tr("Экспорт SmartClip"), "smartclip_backup.json",
            tr("JSON файл (*.json)"));
        if (path.isEmpty()) return;

        QJsonObject root;
        root["version"]     = "1.0";
        root["exported_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

        QJsonArray allPins;
        auto addPinsFromFolder = [&](const QString &folder) {
            for (const PinItem &p : db->getPins(folder)) {
                QJsonObject obj;
                obj["folder"]   = p.folder;
                obj["name"]     = p.name;
                obj["type"]     = p.type;
                obj["content"]  = p.content;
                obj["filepath"] = p.filepath;
                allPins.append(obj);
            }
        };
        addPinsFromFolder("");
        for (const FolderItem &f : db->getAllFolders())
            addPinsFromFolder(f.name);
        root["pins"] = allPins;

        QJsonArray profArr;
        for (const ProfileItem &p : db->getProfiles()) {
            QJsonObject obj;
            obj["name"]   = p.name;
            obj["hotkey"] = p.hotkey;
            obj["text"]   = p.text;
            profArr.append(obj);
        }
        root["profiles"] = profArr;

        QJsonObject cfg;
        cfg["mainHotkey"]    = AppSettings::get().mainHotkey();
        cfg["maxRecords"]    = AppSettings::get().maxHistoryRecords();
        cfg["autocleanDays"] = AppSettings::get().autocleanDays();
        cfg["saveImages"]    = AppSettings::get().saveImages();
        cfg["deduplication"] = AppSettings::get().deduplication();
        cfg["minTextLength"] = AppSettings::get().minTextLength();
        cfg["excludedApps"]  = QJsonArray::fromStringList(AppSettings::get().excludedApps());
        cfg["pinsNoName"]    = AppSettings::get().pinsNoName();
        root["settings"]     = cfg;

        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
            sdInfo(this, tr("Экспорт завершён"),
                   tr("Данные сохранены в:\n%1").arg(path));
        } else {
            sdInfo(this, tr("Ошибка"), tr("Не удалось сохранить файл."));
        }
    });

    // ── Логика импорта ────────────────────────────────────────────────────
    connect(importBtn, &QPushButton::clicked, [=]() {
        QString path = QFileDialog::getOpenFileName(
            this, tr("Импорт SmartClip"), "",
            tr("JSON файл (*.json)"));
        if (path.isEmpty()) return;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            sdInfo(this, tr("Ошибка"), tr("Не удалось открыть файл."));
            return;
        }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        file.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            sdInfo(this, tr("Ошибка"),
                   tr("Файл повреждён или не является бекапом SmartClip."));
            return;
        }

        QJsonObject root = doc.object();
        int pinCount = 0, profCount = 0;
        for (const QJsonValue &v : root["pins"].toArray()) {
            QJsonObject o = v.toObject();
            db->addPin(o["folder"].toString(), o["name"].toString(),
                       o["type"].toString(),   o["content"].toString(),
                       o["filepath"].toString());
            ++pinCount;
        }
        for (const QJsonValue &v : root["profiles"].toArray()) {
            QJsonObject o = v.toObject();
            db->addProfile(o["name"].toString(), o["hotkey"].toString(), o["text"].toString());
            ++profCount;
        }
        if (root.contains("settings")) {
            QJsonObject cfg = root["settings"].toObject();
            if (cfg.contains("maxRecords"))    AppSettings::get().setMaxHistoryRecords(cfg["maxRecords"].toInt());
            if (cfg.contains("autocleanDays")) AppSettings::get().setAutocleanDays(cfg["autocleanDays"].toInt());
            if (cfg.contains("saveImages"))    AppSettings::get().setSaveImages(cfg["saveImages"].toBool());
            if (cfg.contains("deduplication")) AppSettings::get().setDeduplication(cfg["deduplication"].toBool());
            if (cfg.contains("minTextLength")) AppSettings::get().setMinTextLength(cfg["minTextLength"].toInt());
            if (cfg.contains("pinsNoName"))    AppSettings::get().setPinsNoName(cfg["pinsNoName"].toBool());
            if (cfg.contains("excludedApps")) {
                QStringList excl;
                for (const QJsonValue &a : cfg["excludedApps"].toArray())
                    excl << a.toString();
                AppSettings::get().setExcludedApps(excl);
            }
        }
        emit settingsChanged();
        sdInfo(this, tr("Импорт завершён"),
               tr("Импортировано: закрепов — %1, профилей — %2.\n"
                  "Перезагрузите историю чтобы увидеть изменения.").arg(pinCount).arg(profCount));
    });

    // ── Кнопки внизу ─────────────────────────────────────────────────────
    auto *btnBox   = new QHBoxLayout();
    auto *cancelBtn = new QPushButton(tr("Отмена"), body);
    auto *saveBtn   = new QPushButton(tr("Сохранить"), body);
    saveBtn->setObjectName("saveBtn");
    cancelBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setCursor(Qt::PointingHandCursor);
    btnBox->addStretch();
    btnBox->addWidget(cancelBtn);
    btnBox->addWidget(saveBtn);

    bodyLayout->addWidget(tabs, 1);
    bodyLayout->addLayout(btnBox);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(saveBtn, &QPushButton::clicked, this, [=]() {
        // Горячая клавиша
        QString newHotkey = hotkeyEdit->text().trimmed();
        if (newHotkey.isEmpty()) newHotkey = "Ctrl+Shift+V";
        bool hotkeyChanged = (newHotkey != AppSettings::get().mainHotkey());
        AppSettings::get().setMainHotkey(newHotkey);

        // Автозапуск
        bool autostartOn = autostartCheck->isChecked();
        AppSettings::get().setAutostart(autostartOn);
        applyAutostart(autostartOn);

        // Внешний вид
        AppSettings::get().setSolidPanels(blurCheck->isChecked());

        // История
        AppSettings::get().setMaxHistoryRecords(maxRecordsSpin->value());
        AppSettings::get().setSaveImages(saveImagesCheck->isChecked());
        AppSettings::get().setDeduplication(dedupCheck->isChecked());
        AppSettings::get().setMinTextLength(minLenSpin->value());
        AppSettings::get().setAutocleanDays(cleanSpin->value());
        AppSettings::get().setImageFormat(fmtCombo->currentData().toString());
        AppSettings::get().setImageQuality(qualSpin->value());

        // Исключения
        QStringList excluded;
        for (int i = 0; i < exclList->count(); ++i)
            excluded << exclList->item(i)->text();
        AppSettings::get().setExcludedApps(excluded);

        // Закрепы
        AppSettings::get().setPinsNoName(noNameCheck->isChecked());
        AppSettings::get().setPinsNoFolder(noFolderCheck->isChecked());
        AppSettings::get().setRecentPinsLimit(recentLimitSpin->value());

        // Автобекап
        AppSettings::get().setAutoBackup(backupCheck->isChecked());
        AppSettings::get().setAutoBackupDays(backupDaysSpin->value());
        AppSettings::get().setAutoBackupPath(backupPathEdit->text().trimmed());

        // Бета: видео
        AppSettings::get().setVideoMonitorEnabled(videoCheck->isChecked());
        AppSettings::get().setVideoMonitorPath(videoPathEdit->text().trimmed());

        if (hotkeyChanged) emit mainHotkeyChanged(newHotkey);
        emit settingsChanged();

        // Язык
        QString newLang  = langCombo->currentData().toString();
        bool langChanged = (newLang != AppSettings::get().language());
        AppSettings::get().setLanguage(newLang);

        accept();

        if (langChanged) {
            if (sdQuestion(parentWidget(),
                           tr("Смена языка"),
                           tr("Для применения языка необходимо перезапустить приложение.\n"
                              "Перезапустить сейчас?"))) {
                AppSettings::get().setRestartRequested(true);
                QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            }
        }
    });
}

// ─── Отрисовка ───────────────────────────────────────────────────────────────
void SettingsDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1), 12, 12);
    p.fillPath(path, QColor(18, 18, 30, 252));
    p.setPen(QPen(QColor(255,255,255,55), 2));
    p.drawPath(path);
}

void SettingsDialog::mousePressEvent(QMouseEvent *e)
{
    if (m_titleBar && m_titleBar->geometry().contains(e->pos()) && e->button() == Qt::LeftButton)
        m_drag = e->globalPosition().toPoint() - frameGeometry().topLeft();
    else
        m_drag = {};
    QDialog::mousePressEvent(e);
}

void SettingsDialog::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_drag.isNull() && (e->buttons() & Qt::LeftButton))
        move(e->globalPosition().toPoint() - m_drag);
}

void SettingsDialog::mouseReleaseEvent(QMouseEvent *e)
{
    m_drag = {};
    QDialog::mouseReleaseEvent(e);
}
