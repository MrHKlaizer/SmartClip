// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  SettingsDialog.cpp — Диалог настроек SmartClip                            ║
// ║                                                                              ║
// ║  Это полностью кастомный диалог без стандартного заголовка Windows:         ║
// ║  • FramelessWindowHint + WA_TranslucentBackground — убирает рамку          ║
// ║  • Свой тайтл-бар с крестиком (SdCloseBtn) и перетаскиванием               ║
// ║  • QPainterPath для скруглённого тёмного фона                               ║
// ║                                                                              ║
// ║  Структура — 5 вкладок (QTabWidget):                                        ║
// ║  ① Система  — горячая клавиша, автозапуск, язык, масштаб, вид              ║
// ║  ② История  — лимит, поведение, автоочистка, форматы, исключения           ║
// ║  ③ Закрепы  — опции сохранения, лимит «Все»                                ║
// ║  ④ Экспорт / Импорт — JSON-бекап, автобекап                                ║
// ║  ⑤ Специальные возможности — бета, видео, обратная связь, обновления       ║
// ║                                                                              ║
// ║  Вспомогательные классы (внутренние, видны только в этом .cpp):             ║
// ║  • SdCloseBtn  — кнопка-крестик с hover-эффектом                            ║
// ║  • SdCheckBox  — чекбокс с кастомной отрисовкой (крестик вместо флага)     ║
// ║  • SdDialog    — переиспользуемый мини-диалог (информация / вопрос / ввод) ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include "SettingsDialog.h"
#include "core/Backup.h"   // Экспорт/импорт JSON (общий с автобэкапом)
#include "HotkeyEdit.h"             // Виджет захвата горячих клавиш
#include "Anim.h"                   // Плавное появление окон
#include "NavTabs.h"                // Боковое меню разделов с анимацией
#include "SmartButton.h"            // Системный шрифт значков
#include "core/AppSettings.h"       // Синглтон настроек
#include "core/Database.h"          // Чтение/запись в SQLite
#include "core/BugReporter.h"       // Отправка обратной связи в Telegram
#include "core/Version.h"           // APP_VERSION — строка вида "1.0.2"
#include "core/UpdateChecker.h"     // Проверка обновлений через GitHub API
#include "core/AdminMode.h"
#include "core/VideoWatcher.h"     // Папки автозахвата         // Работа в окнах администратора

// Qt-виджеты и инфраструктура:
#include <QPainter>             // Рисование (paintEvent)
#include <QPainterPath>         // Скруглённый прямоугольник
#include <QTimer>               // QTimer::singleShot — отложенный вызов
#include <QTabWidget>           // Виджет с вкладками
#include <QVBoxLayout>          // Вертикальный лейаут (элементы друг под другом)
#include <QHBoxLayout>          // Горизонтальный лейаут (элементы в ряд)
#include <QFormLayout>          // Лейаут «метка : поле ввода»
#include <QGroupBox>            // Группирующая рамка с заголовком
#include <QCheckBox>            // Чекбокс (галочка)
#include <QSpinBox>             // Числовое поле со стрелками ▲▼
#include <QComboBox>            // Выпадающий список
#include <QLineEdit>            // Однострочное текстовое поле
#include <QListWidget>          // Список элементов
#include <QScrollArea>
#include <QIcon>
#include <QSlider>
#include <QScreen>          // Прокручиваемая область
#include <QAbstractButton>      // Базовый класс для SdCloseBtn
#include <QMouseEvent>          // Событие мыши (для перетаскивания диалога)
#include <QMenu>                // Контекстное меню (не используется напрямую)
#include <QFileDialog>          // Диалог выбора файла/папки
#include <QJsonDocument>        // Сериализация/десериализация JSON
#include <QJsonObject>          // JSON-объект ({ключ: значение})
#include <QJsonArray>           // JSON-массив ([элемент, элемент])
#include <QDateTime>            // Текущая дата/время для экспорта
#include <QFile>                // Открытие файла для записи/чтения
#include <QPushButton>          // Обычная кнопка
#include <QLabel>               // Метка (текст, иконка)
#include <QTextEdit>            // Многострочное текстовое поле
#include <QSettings>            // Чтение/запись реестра Windows
#include <QCoreApplication>     // applicationFilePath(), quit()
#include <QFileInfo>            // baseName() — имя файла без пути и расширения
#include <QPointer>
#include <QVariantAnimation>
#include <QEasingCurve>             // Безопасные ссылки на виджеты из асинхронных обработчиков
#include <QDir>                 // toNativeSeparators() — слэши → обратные слэши
#include <QGridLayout>
#include "FilesStrip.h"         // Files:: — папки и типы полоски файлов
#include <QDebug>               // qDebug() — вывод в консоль разработчика

// ─── Автозапуск ───────────────────────────────────────────────────────────────
// Записывает или удаляет SmartClip из реестра Windows автозапуска.
// HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run —
// стандартная ветка реестра: программы из неё стартуют при входе пользователя.
// QSettings::NativeFormat — Qt работает с реестром напрямую (не через .ini-файл).
static void applyAutostart(bool enable)
{
    QSettings reg(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat
    );
    if (enable) {
        // Записываем полный путь к .exe в кавычках (на случай пробелов в пути)
        QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        reg.setValue("SmartClip", "\"" + exePath + "\"");
    } else {
        // Удаляем ключ — программа больше не запускается автоматически
        reg.remove("SmartClip");
    }
}

// Реальное состояние автозапуска — есть ли запись в реестре.
// Раньше галочка бралась из ini (по умолчанию «выкл»), хотя установщик
// включает автозапуск через реестр. В итоге любое «Сохранить» в настройках
// молча выключало автозапуск.
static bool isAutostartEnabled()
{
    QSettings reg(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat
    );
    return reg.contains("SmartClip");
}

// ─── SdCloseBtn — кнопка закрытия диалога ────────────────────────────────────
// Наследует QAbstractButton (базовый класс всех кнопок Qt).
// Рисуется вручную: символ ✕ с hover-эффектом (белый → красный при наведении).
class SdCloseBtn : public QAbstractButton {
public:
    explicit SdCloseBtn(QWidget *p) : QAbstractButton(p) {
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
    }
protected:
    // paintEvent — вызывается Qt каждый раз когда нужно нарисовать кнопку.
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        bool h = underMouse();  // true если курсор над кнопкой прямо сейчас
        p.setPen(h ? QColor("#ff6666") : QColor(255, 255, 255, 150));
        QFont f = font(); f.setPixelSize(15); p.setFont(f);
        p.drawText(rect().adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter, "✕");
    }
    // Сигнализируем Qt что нужно перерисовать кнопку при смене hover-состояния.
    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QAbstractButton::leaveEvent(e); }
};

// ─── SdCheckBox — чекбокс с кастомным оформлением ────────────────────────────
// Стандартный QCheckBox показывает синий квадрат с галочкой — это не вписывается
// в тёмный дизайн. Рисуем свой: тёмный квадратик с белым ✕ когда включено.
class SdCheckBox : public QCheckBox {
public:
    // «using QCheckBox::QCheckBox» — наследуем все конструкторы родителя без изменений.
    using QCheckBox::QCheckBox;

    // Дизайн 2.0: тумблер вместо квадратика с «✕» (который читался как
    // «выключено»). Кружок плавно переезжает, включённый тумблер — голубой.
    QSize sizeHint() const override {
        const QSize t = fontMetrics().size(Qt::TextSingleLine, text());
        return QSize(TRACK_W + SPACING + t.width() + 4, qMax(TRACK_H + 4, t.height() + 6));
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override {
        // Первый показ: подписываемся на переключение и ставим кружок на место
        if (!m_ready) {
            m_ready = true;
            m_knob = isChecked() ? 1.0 : 0.0;
            m_anim.setEasingCurve(QEasingCurve::OutBack);
            QObject::connect(&m_anim, &QVariantAnimation::valueChanged, this,
                             [this](const QVariant &v) { m_knob = v.toReal(); update(); });
            QObject::connect(this, &QCheckBox::toggled, this, [this](bool on) {
                m_anim.stop();
                if (Anim::enabled(Anim::Kind::Tabs) && isVisible()) {
                    m_anim.setDuration(Anim::ms(Anim::Kind::Tabs, 220));
                    m_anim.setStartValue(m_knob);
                    m_anim.setEndValue(on ? 1.0 : 0.0);
                    m_anim.start();
                } else {
                    m_knob = on ? 1.0 : 0.0;
                    update();
                }
            });
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal k = qBound(0.0, m_knob, 1.0);
        const bool hover = underMouse() && isEnabled();
        const qreal op = isEnabled() ? 1.0 : 0.45;
        p.setOpacity(op);

        // Дорожка: цвет плавно переходит от серого к голубому
        const QRectF track(1, (height() - TRACK_H) / 2.0, TRACK_W, TRACK_H);
        auto mix = [k](int a, int b) { return int(a + (b - a) * k); };
        const QColor offFill(44, 44, 64), onFill(96, 140, 210);
        p.setPen(QPen(k > 0.5 ? QColor(160, 200, 255, 160)
                              : QColor(255, 255, 255, hover ? 110 : 70), 1));
        p.setBrush(QColor(mix(offFill.red(), onFill.red()), mix(offFill.green(), onFill.green()),
                          mix(offFill.blue(), onFill.blue())));
        p.drawRoundedRect(track, TRACK_H / 2.0, TRACK_H / 2.0);

        // Кружок
        const qreal d = TRACK_H - 6;
        const qreal x = track.left() + 3 + (TRACK_W - 6 - d) * m_knob;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, int(200 + 55 * k)));
        p.drawEllipse(QRectF(x, track.top() + 3, d, d));

        // Подпись справа от тумблера
        p.setPen(QColor(0xd0, 0xd0, 0xdc));
        p.setFont(font());
        p.drawText(rect().adjusted(TRACK_W + SPACING, 0, 0, 0),
                   Qt::AlignVCenter | Qt::AlignLeft, text());
    }
    bool hitButton(const QPoint &pos) const override { return rect().contains(pos); }
    void enterEvent(QEnterEvent *e) override { update(); QCheckBox::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QCheckBox::leaveEvent(e); }

private:
    static constexpr int TRACK_W = 36;   // Ширина тумблера
    static constexpr int TRACK_H = 20;   // Высота тумблера
    static constexpr int SPACING = 10;   // Отступ до подписи
    bool              m_ready = false;
    qreal             m_knob  = 0.0;     // 0 = выключено, 1 = включено (между — анимация)
    QVariantAnimation m_anim;
};

// ─── SdDialog — переиспользуемый мини-диалог ─────────────────────────────────
// Заменяет стандартные QMessageBox и QInputDialog которые выглядят «не в стиле».
// Состоит из:
//   - тайтл-бара с заголовком и кнопкой закрытия
//   - тела (m_body) — туда вставляем содержимое конкретного диалога
// Перетаскивание реализовано через mousePressEvent/mouseMoveEvent/mouseReleaseEvent.
class SdDialog : public QDialog {
public:
    SdDialog(QWidget *parent, const QString &title) : QDialog(parent) {
        // Qt::FramelessWindowHint — убираем стандартную рамку Windows
        // Qt::WA_TranslucentBackground — фон окна прозрачный (мы рисуем свой)
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setMinimumWidth(340);

        auto *master = new QVBoxLayout(this);
        master->setContentsMargins(0, 0, 0, 0);
        master->setSpacing(0);

        // Тайтл-бар: заголовок слева, кнопка закрытия справа
        m_titleBar = new QWidget(this);
        m_titleBar->setFixedHeight(36);
        m_titleBar->setStyleSheet("background: transparent;");
        auto *tl = new QHBoxLayout(m_titleBar);
        tl->setContentsMargins(14, 0, 2, 0);
        tl->setSpacing(0);
        auto *lbl = new QLabel(title, m_titleBar);
        lbl->setStyleSheet("color:#e0e0e0;font-size:13px;font-weight:bold;background:transparent;");
        tl->addWidget(lbl, 1);  // stretch=1 — метка занимает всё свободное место
        auto *cb = new SdCloseBtn(m_titleBar);
        connect(cb, &QAbstractButton::clicked, this, &QDialog::reject);
        tl->addWidget(cb, 0, Qt::AlignVCenter);
        master->addWidget(m_titleBar);

        // Тело диалога — сюда вставляется контент (метки, поля ввода, кнопки)
        auto *bw = new QWidget(this);
        bw->setStyleSheet("background: transparent;");
        auto *bwl = new QVBoxLayout(bw);
        bwl->setContentsMargins(14, 4, 14, 14);
        m_body = new QWidget(bw);
        m_body->setStyleSheet("background: transparent;");
        bwl->addWidget(m_body, 1);
        master->addWidget(bw, 1);
    }
    // body() — возвращает виджет-контейнер куда вставляется содержимое.
    QWidget *body() { return m_body; }
protected:
    // «Выпрыгивает» из центра и уходит обратно (см. Anim.h)
    void showEvent(QShowEvent *e) override {
        QDialog::showEvent(e);
        m_anim->onShow();
    }
    void done(int r) override {
        if (!m_anim->handleDone([self = QPointer<SdDialog>(this), r]() {
                if (self) self->QDialog::done(r);
            }))
            QDialog::done(r);
    }
    // Рисуем скруглённый тёмный фон диалога (как у SettingsDialog).
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1), 12, 12);
        p.fillPath(path, QColor(18, 18, 30, 252));
        p.setPen(QPen(QColor(255,255,255,55), 2)); p.drawPath(path);
    }
    // Перетаскивание: запоминаем смещение курсора от угла окна при нажатии.
    void mousePressEvent(QMouseEvent *e) override {
        if (m_titleBar->geometry().contains(e->pos()) && e->button() == Qt::LeftButton)
            m_drag = e->globalPosition().toPoint() - frameGeometry().topLeft();
        else m_drag = {};  // QPoint() — «нулевой» (пустой) вектор
        QDialog::mousePressEvent(e);
    }
    // При движении мыши — перемещаем окно используя сохранённое смещение.
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!m_drag.isNull() && (e->buttons() & Qt::LeftButton))
            move(e->globalPosition().toPoint() - m_drag);
    }
    void mouseReleaseEvent(QMouseEvent *e) override { m_drag = {}; QDialog::mouseReleaseEvent(e); }
private:
    QWidget *m_titleBar = nullptr;
    QWidget *m_body     = nullptr;
    QPoint   m_drag;   // Смещение курсора относительно угла окна (для перетаскивания)
    Anim::DialogAnimator *m_anim = new Anim::DialogAnimator(this);
};

// ── Стили кнопок для мини-диалогов ───────────────────────────────────────────
// const char* — C-строка (не QString). Используется как QSS-стиль.
static const char *SD_OK_BTN =
    "QPushButton{background:rgba(28,28,44,245);color:rgba(160,200,255,240);"
    "border:1px solid rgba(120,160,255,160);border-radius:6px;"
    "padding:5px 18px;font-size:13px;min-width:60px;}"
    "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}"
    // Неактивная кнопка должна ВЫГЛЯДЕТЬ неактивной — раньше она была
    // неотличима от обычной, и казалось, что «Отправить» просто сломана
    "QPushButton:disabled{color:rgba(160,200,255,80);border-color:rgba(120,160,255,50);}";
static const char *SD_CANCEL_BTN =
    "QPushButton{background:rgba(28,28,44,245);color:rgba(255,255,255,200);"
    "border:1px solid rgba(255,255,255,55);border-radius:6px;"
    "padding:5px 18px;font-size:13px;min-width:60px;}"
    "QPushButton:hover{background:rgba(40,40,62,245);color:#fff;}";

// ── Вспомогательные функции для показа мини-диалогов ─────────────────────────

// sdInfo — показывает диалог с сообщением и единственной кнопкой «ОК».
static void sdInfo(QWidget *p, const QString &title, const QString &text) {
    SdDialog d(p, title);
    auto *vl = new QVBoxLayout(d.body());
    vl->setContentsMargins(0,0,0,0); vl->setSpacing(14);
    auto *lbl = new QLabel(text, d.body());
    lbl->setWordWrap(true);   // Автоперенос длинных строк
    lbl->setStyleSheet("color:#d0d0d0;font-size:13px;background:transparent;");
    auto *br = new QHBoxLayout();
    auto *ok = new QPushButton(QObject::tr("ОК"), d.body());
    ok->setStyleSheet(SD_OK_BTN); ok->setCursor(Qt::PointingHandCursor);
    br->addStretch(); br->addWidget(ok);
    vl->addWidget(lbl); vl->addLayout(br);
    QObject::connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    d.exec();  // exec() — блокирующий показ: ждём пока пользователь закроет диалог
}

// sdQuestion — показывает диалог «Да / Нет». Возвращает true если нажали «Да».
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
    // exec() == QDialog::Accepted — вернуть true только если нажали «Да»
    return d.exec() == QDialog::Accepted;
}

// sdInputText — диалог с полем ввода строки. Возвращает введённый текст или "".
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
    // returnPressed — нажатие Enter подтверждает ввод (удобство)
    QObject::connect(edit, &QLineEdit::returnPressed, &d, &QDialog::accept);
    return d.exec() == QDialog::Accepted ? edit->text().trimmed() : QString{};
}

// sdInputItem — диалог с выпадающим списком. Возвращает выбранный элемент или "".
static QString sdInputItem(QWidget *p, const QString &title, const QString &label,
                           const QStringList &items) {
    SdDialog d(p, title); d.setMinimumWidth(360);
    auto *vl = new QVBoxLayout(d.body());
    vl->setContentsMargins(0,0,0,0); vl->setSpacing(10);
    auto *lbl   = new QLabel(label, d.body());
    lbl->setStyleSheet("color:#c0c0d0;font-size:13px;background:transparent;");
    auto *combo = new QComboBox(d.body());
    combo->addItems(items);  // Заполняем список переданными элементами
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
// Показывает форму отправки жалобы/пожелания через Telegram Bot API.
// SD_COMBO_STYLE / SD_TEXTEDIT_STYLE — QSS-стили для элементов диалога.
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
    // FTR — макрос для перевода строк в этой функции.
    // QCoreApplication::translate("SettingsDialog", s) ищет перевод в файле SettingsDialog.
    // Макрос нужен чтобы не писать длинное имя функции каждый раз.
#define FTR(s) QCoreApplication::translate("SettingsDialog", s)

    SdDialog dlg(parent, FTR("Обратная связь"));
    dlg.setMinimumWidth(500);

    auto *vl = new QVBoxLayout(dlg.body());
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(10);

    // ── Тип сообщения ─────────────────────────────────────────────────────────
    // Пользователь выбирает: жалоба (🔴) или пожелание (💡).
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

    // ── Поле описания ─────────────────────────────────────────────────────────
    auto *descLbl = new QLabel(FTR("Описание:"), dlg.body());
    descLbl->setStyleSheet("color:#c0c0d0;font-size:13px;background:transparent;");
    vl->addWidget(descLbl);

    auto *descEdit = new QTextEdit(dlg.body());
    descEdit->setPlaceholderText(FTR("Опишите проблему или пожелание подробнее..."));
    descEdit->setMinimumHeight(100);
    descEdit->setMaximumHeight(140);
    descEdit->setStyleSheet(SD_TEXTEDIT_STYLE);
    vl->addWidget(descEdit);

    // ── Вложения (медиафайлы) ─────────────────────────────────────────────────
    // BugReporter ограничивает: MAX_PHOTOS фото, MAX_VIDEOS видео, MAX_AUDIOS аудио.
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

    // Список прикреплённых файлов (ПКМ → удалить)
    auto *attachList = new QListWidget(dlg.body());
    attachList->setFixedHeight(80);
    attachList->setStyleSheet(
        "QListWidget{background:rgba(22,22,36,200);color:#e0e0e0;"
        "border:1px solid rgba(255,255,255,35);border-radius:6px;"
        "font-size:12px;outline:none;}"
        "QListWidget::item{padding:3px 6px;}"
        "QListWidget::item:selected{background:rgba(255,255,255,18);color:#fff;}");
    attachList->setContextMenuPolicy(Qt::CustomContextMenu);  // ПКМ → сигнал customContextMenuRequested
    vl->addWidget(attachList);

    auto *attachHint = new QLabel(
        FTR("ПКМ по файлу — удалить  ·  до 4 мин. для видео  ·  макс. 50 МБ"),
        dlg.body());
    attachHint->setStyleSheet("color:rgba(160,160,180,150);font-size:11px;background:transparent;");
    vl->addWidget(attachHint);

    // attached — список путей к прикреплённым файлам
    QList<QString> attached;

    // Лямбда обновляет счётчики фото/видео/аудио в строке statsLbl.
    // [&] — захватываем по ссылке все переменные из внешнего контекста.
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
    updateStats();  // Показываем начальные счётчики (0/4, 0/2, 0/5)

    // Кнопка «Прикрепить»: открываем стандартный диалог выбора файлов
    QObject::connect(attachBtn, &QPushButton::clicked, [&]() {
        const QString filter =
            FTR("Медиафайлы (*.png *.jpg *.jpeg *.gif *.webp "
                "*.mp4 *.mov *.avi *.mkv *.webm "
                "*.mp3 *.ogg *.wav *.m4a *.flac *.aac);;"
                "Все файлы (*.*)");
        // getOpenFileNames — возвращает список файлов (можно выбрать несколько)
        QStringList files = QFileDialog::getOpenFileNames(
            &dlg, FTR("Прикрепить файлы"), QString(), filter);

        for (const QString &path : files) {
            if (attached.contains(path)) continue;  // Не добавлять дубликаты
            // Telegram не принимает файлы больше 50 МБ — раньше подсказка про лимит
            // была, а проверки не было, и отправка просто падала с ошибкой
            if (QFileInfo(path).size() > 50LL * 1024 * 1024) {
                sdInfo(&dlg, FTR("Прикрепить файлы"),
                       FTR("Файл «%1» больше 50 МБ и не будет отправлен.")
                           .arg(QFileInfo(path).fileName()));
                continue;
            }
            const auto kind = BugReporter::kindOfFile(path);
            // Считаем текущее количество каждого типа
            int photos = 0, videos = 0, audios = 0;
            for (const QString &p : attached) {
                switch (BugReporter::kindOfFile(p)) {
                    case BugReporter::FileKind::Photo: ++photos; break;
                    case BugReporter::FileKind::Video: ++videos; break;
                    case BugReporter::FileKind::Audio: ++audios; break;
                }
            }
            // Пропускаем если превышен лимит для данного типа
            if (kind == BugReporter::FileKind::Photo && photos >= BugReporter::MAX_PHOTOS) continue;
            if (kind == BugReporter::FileKind::Video && videos >= BugReporter::MAX_VIDEOS) continue;
            if (kind == BugReporter::FileKind::Audio && audios >= BugReporter::MAX_AUDIOS) continue;
            // Иконка для типа файла
            QString icon;
            switch (kind) {
                case BugReporter::FileKind::Photo: icon = QString::fromUtf8("📷"); break;
                case BugReporter::FileKind::Video: icon = QString::fromUtf8("🎬"); break;
                case BugReporter::FileKind::Audio: icon = QString::fromUtf8("🎵"); break;
            }
            attached.append(path);
            // Показываем только имя файла (без полного пути)
            attachList->addItem(icon + "  " + QFileInfo(path).fileName());
        }
        updateStats();
    });

    // ПКМ по файлу в списке → удалить его
    QObject::connect(attachList, &QListWidget::customContextMenuRequested,
                     [&](const QPoint &pos) {
        QListWidgetItem *item = attachList->itemAt(pos);
        if (!item) return;
        attached.removeAt(attachList->row(item));  // Удаляем путь из списка
        delete item;   // Удаляем строку из QListWidget (delete виджет!)
        updateStats();
    });

    // ── Ограничение по cooldown ───────────────────────────────────────────────
    // BugReporter позволяет отправлять не чаще раза в COOLDOWN_HOURS часов.
    const bool canSend = BugReporter::canSend();
    if (!canSend) {
        const int hoursLeft = BugReporter::COOLDOWN_HOURS - BugReporter::hoursSinceLast();
        auto *infoLbl = new QLabel(
            FTR("⏳ Следующий репорт доступен через %1 ч.").arg(hoursLeft),
            dlg.body());
        infoLbl->setStyleSheet("color:#ffaa44;font-size:12px;background:transparent;");
        vl->addWidget(infoLbl);
    }

    // ── Кнопки «Отмена» и «Отправить» ────────────────────────────────────────
    auto *btnRow    = new QHBoxLayout;
    auto *cancelBtn = new QPushButton(FTR("Отмена"), dlg.body());
    auto *sendBtn   = new QPushButton(FTR("📩  Отправить"), dlg.body());
    cancelBtn->setStyleSheet(SD_CANCEL_BTN);
    sendBtn->setStyleSheet(SD_OK_BTN);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    sendBtn->setCursor(Qt::PointingHandCursor);
    sendBtn->setEnabled(false);  // Активируется когда введено >= 10 символов
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(sendBtn);
    vl->addLayout(btnRow);

    // Активируем кнопку «Отправить» как только введено >= 10 символов.
    // Подсказка со счётчиком — раньше условие нигде не показывалось, и при
    // коротком тексте кнопка выглядела просто «сломанной».
    constexpr int MIN_LEN = 10;
    auto *lenHint = new QLabel(dlg.body());
    lenHint->setStyleSheet("color:rgba(160,160,180,150);font-size:11px;background:transparent;");
    vl->insertWidget(vl->indexOf(descEdit) + 1, lenHint);
    auto updateSend = [&, lenHint]() {
        const int len = descEdit->toPlainText().trimmed().length();
        sendBtn->setEnabled(canSend && len >= MIN_LEN);
        lenHint->setVisible(canSend && len < MIN_LEN);
        lenHint->setText(FTR("Опишите подробнее — ещё минимум %1 симв.").arg(MIN_LEN - len));
    };
    QObject::connect(descEdit, &QTextEdit::textChanged, updateSend);
    updateSend();

    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    // Кнопка «Отправить»: создаём BugReporter и отправляем.
    QObject::connect(sendBtn, &QPushButton::clicked, [&, parent]() {
        // Тип: индекс 0 = Bug (жалоба), 1 = Feature (пожелание)
        const BugReporter::Type selType = (typeBox->currentIndex() == 0)
            ? BugReporter::Type::Bug : BugReporter::Type::Feature;
        sendBtn->setEnabled(false);
        sendBtn->setText(FTR("Отправка..."));

        // Создаём BugReporter с parent как родителем (не dlg — он исчезнет)
        auto *reporter = new BugReporter(parent);
        reporter->send(selType, descEdit->toPlainText(), attached);

        // QPointer обнуляется сам, если диалог закрыли до ответа сервера.
        // Раньше обработчики ниже держали ссылки на уже уничтоженные dlg/sendBtn —
        // закрыл окно во время отправки → вылет программы.
        QPointer<QDialog>     dlgGuard(&dlg);
        QPointer<QPushButton> btnGuard(sendBtn);

        // По сигналу sent — закрываем диалог и показываем «Спасибо!»
        QObject::connect(reporter, &BugReporter::sent, reporter, [reporter, parent, dlgGuard]() {
            reporter->deleteLater();  // Удаляем reporter когда управление вернётся в Qt
            if (dlgGuard) dlgGuard->accept();
            sdInfo(parent, FTR("Спасибо!"),
                   FTR("Сообщение отправлено. Мы обязательно его рассмотрим!"));
        });
        // По сигналу failed — показываем ошибку, разблокируем кнопку
        QObject::connect(reporter, &BugReporter::failed, reporter,
                         [reporter, parent, btnGuard](const QString &err) {
            reporter->deleteLater();
            if (btnGuard) {
                btnGuard->setEnabled(true);
                btnGuard->setText(FTR("📩  Отправить"));
            }
            sdInfo(parent, FTR("Ошибка отправки"),
                   FTR("Не удалось отправить: %1").arg(err));
        });
    });

    dlg.exec();
#undef FTR   // Убираем макрос — он больше не нужен за пределами функции
}

// ════════════════════════════════════════════════════════════════════════════════
//  КОНСТРУКТОР SettingsDialog
//  Собирает все 5 вкладок и подключает логику кнопки «Сохранить».
// ════════════════════════════════════════════════════════════════════════════════
SettingsDialog::SettingsDialog(Database *db, QWidget *parent)
    : QDialog(parent)
{
    Q_UNUSED(db)  // db не нужен в этом конструкторе напрямую (передаётся в лямбды)

    // Убираем стандартный заголовок Windows, делаем фон прозрачным
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(tr("Настройки SmartClip"));
    m_anim = new Anim::DialogAnimator(this);  // Появление/уход с анимацией
    // Разделы прокручиваются — окну хватает и маленького экрана
    // (ноутбук 1366×768 при масштабе 125% — около 600 px по высоте)
    setMinimumSize(700, 460);

    // ── Глобальный QSS-стиль для всего диалога ───────────────────────────────
    // QSS (Qt Style Sheet) — это CSS для Qt-виджетов.
    // Здесь мы задаём стили для QTabWidget, QGroupBox, QLabel, QCheckBox,
    // QSpinBox, QComboBox, QLineEdit, QListWidget, QPushButton, QScrollBar.
    // Дизайн 2.0: разделы — карточки со скруглением (заголовок НАД карточкой,
    // а не врезанный в рамку), поля со стрелками, единый мягко-голубой акцент.
    setStyleSheet(
        "QWidget { background: transparent; }"
        // Карточка раздела
        "QGroupBox {"
        "  background: rgba(255,255,255,7);"
        "  border: 1px solid rgba(255,255,255,24);"
        "  border-radius: 12px; margin-top: 28px; padding: 2px 12px 10px 12px;"
        "  color: #eaeaf6; font-size: 13px; font-weight: 600; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; subcontrol-position: top left;"
        "  left: 4px; top: 4px; padding: 0; }"
        "QLabel { color: #c0c0d0; font-size: 12px; font-weight: normal; background: transparent; }"
        "QCheckBox { color: #d0d0dc; font-size: 12px; font-weight: normal; spacing: 8px; }"
        // Числовое поле: аккуратные кнопки ▲▼ со стрелками
        "QSpinBox {"
        "  background: rgba(28,28,44,245); color: #f0f0f0;"
        "  border: 1px solid rgba(255,255,255,50);"
        "  border-radius: 7px; padding: 4px 26px 4px 10px; font-size: 12px; font-weight: normal; }"
        "QSpinBox:hover, QComboBox:hover, QLineEdit:hover { border: 1px solid rgba(255,255,255,85); }"
        "QSpinBox:focus, QLineEdit:focus { border: 1px solid rgba(160,200,255,170); }"
        "QSpinBox::up-button, QSpinBox::down-button {"
        "  subcontrol-origin: border; width: 22px; border: none; background: transparent; }"
        "QSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 7px; }"
        "QSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 7px; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: rgba(255,255,255,18); }"
        "QSpinBox::up-arrow { image: url(:/icons/chevron_up.png); width: 10px; height: 10px; }"
        "QSpinBox::down-arrow { image: url(:/icons/chevron_down.png); width: 10px; height: 10px; }"
        // Выпадающий список со стрелкой
        "QComboBox {"
        "  background: rgba(28,28,44,245); color: #f0f0f0;"
        "  border: 1px solid rgba(255,255,255,50);"
        "  border-radius: 7px; padding: 5px 30px 5px 10px; font-size: 12px; font-weight: normal; }"
        "QComboBox::drop-down { subcontrol-origin: border; subcontrol-position: center right;"
        "  width: 26px; border: none; }"
        "QComboBox::down-arrow { image: url(:/icons/chevron_down.png); width: 10px; height: 10px; }"
        "QComboBox QAbstractItemView {"
        "  background: rgba(22,22,36,252); color: #e0e0e0;"
        "  border: 1px solid rgba(255,255,255,55); border-radius: 6px;"
        "  selection-background-color: rgba(160,200,255,40); outline: none; padding: 4px; }"
        // Текстовое поле
        "QLineEdit {"
        "  background: rgba(28,28,44,245); color: #f0f0f0;"
        "  border: 1px solid rgba(255,255,255,50);"
        "  border-radius: 7px; padding: 5px 10px; font-size: 12px; font-weight: normal; }"
        // Список
        "QListWidget {"
        "  background: rgba(22,22,36,200); color: #e0e0e0;"
        "  border: 1px solid rgba(255,255,255,40);"
        "  border-radius: 8px; font-size: 12px; font-weight: normal; outline: none; padding: 2px; }"
        "QListWidget::item { padding: 4px 8px; border-radius: 5px; }"
        "QListWidget::item:selected { background: rgba(160,200,255,40); color: #fff; }"
        // Обычные кнопки
        "QPushButton {"
        "  background: rgba(28,28,44,245); color: rgba(255,255,255,210);"
        "  border: 1px solid rgba(255,255,255,50);"
        "  border-radius: 7px; padding: 6px 16px; font-size: 12px; font-weight: normal; }"
        "QPushButton:hover {"
        "  background: rgba(40,40,62,245);"
        "  border: 1px solid rgba(255,255,255,90); color: #fff; }"
        // Кнопка «Сохранить» — акцентная
        "QPushButton#saveBtn {"
        "  background: rgba(70,110,180,200); color: #ffffff;"
        "  border: 1px solid rgba(160,200,255,140); }"
        "QPushButton#saveBtn:hover { background: rgba(90,135,215,230); }"
        // Полоса прокрутки — тонкая, без кнопок
        "QScrollBar:vertical { background: transparent; width: 4px; margin: 4px 0; }"
        "QScrollBar::handle:vertical {"
        "  background: rgba(255,255,255,40); border-radius: 2px; min-height: 28px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollArea { background: transparent; border: none; }"
    );

    // ── Главный вертикальный лейаут ───────────────────────────────────────────
    auto *master = new QVBoxLayout(this);
    master->setContentsMargins(0, 0, 0, 0);
    master->setSpacing(0);

    // ── Шапка с заголовком и кнопкой закрытия ────────────────────────────────
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

    // ── Тело диалога — содержит вкладки и кнопки внизу ───────────────────────
    auto *body = new QWidget(this);
    body->setStyleSheet("background: transparent;");
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 4, 12, 12);
    bodyLayout->setSpacing(10);
    master->addWidget(body, 1);

    // Боковое меню разделов с анимацией (см. NavTabs.h) вместо вкладок сверху
    auto *tabs = new NavTabs(body, QColor(18, 18, 30));

    // ═══════════════════════════════════════════════════════════════════════════
    //  Вкладка 1: Система
    //  Горячая клавиша, автозапуск, язык, масштаб, внешний вид
    // ═══════════════════════════════════════════════════════════════════════════
    auto *sysTab = new QWidget();
    auto *sysLayout = new QVBoxLayout(sysTab);
    sysLayout->setContentsMargins(14, 14, 14, 14);
    sysLayout->setSpacing(12);

    // ── Горячая клавиша ───────────────────────────────────────────────────────
    // HotkeyEdit — наш виджет для захвата горячих клавиш (см. HotkeyEdit.h/cpp).
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

    // ── Автозапуск ────────────────────────────────────────────────────────────
    auto *startGroup  = new QGroupBox(tr("Запуск системы"), sysTab);
    auto *startLayout = new QVBoxLayout(startGroup);
    auto *autostartCheck = new SdCheckBox(tr("Запускать SmartClip вместе с Windows"), startGroup);
    autostartCheck->setChecked(isAutostartEnabled());
    auto *startHint = new QLabel(
        tr("SmartClip будет автоматически стартовать при входе в систему."), startGroup);
    startHint->setWordWrap(true);
    startHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    startLayout->addWidget(autostartCheck);
    startLayout->addWidget(startHint);

    // Окна администратора: Windows не пускает обычные программы в окна,
    // открытые «от имени администратора» (терминал и т.п.) — там Win+V
    // открывал бы системный буфер. С галочкой SmartClip стартует с правами админа.
    auto *adminCheck = new SdCheckBox(tr("Работать в окнах администратора"), startGroup);
    adminCheck->setChecked(AppSettings::get().runAsAdmin());
    const bool canElevate = AdminMode::canElevate();
    adminCheck->setEnabled(canElevate || adminCheck->isChecked());
    auto *adminHint = new QLabel(canElevate
        ? tr("SmartClip будет запускаться с правами администратора, чтобы горячая клавиша "
             "и вставка работали в программах, открытых от имени администратора "
             "(например, в терминале). Windows один раз спросит разрешение.")
        : tr("Нужна учётная запись администратора Windows."), startGroup);
    adminHint->setWordWrap(true);
    adminHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    startLayout->addSpacing(6);
    startLayout->addWidget(adminCheck);
    startLayout->addWidget(adminHint);

    // ── Язык ──────────────────────────────────────────────────────────────────
    // itemData() хранит код языка ("ru", "ua", "en", "de") — используется при сохранении.
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

    // ── Масштаб интерфейса ────────────────────────────────────────────────────
    // itemData() хранит числовое значение масштаба (75, 90, 100, 110, 125).
    auto *scaleGroup  = new QGroupBox(tr("Масштаб интерфейса"), sysTab);
    auto *scaleLayout = new QVBoxLayout(scaleGroup);
    scaleLayout->setSpacing(8);
    auto *scaleCombo = new QComboBox(scaleGroup);
    scaleCombo->addItem(tr("75% — компактный"),    75);
    scaleCombo->addItem(tr("90%"),                 90);
    scaleCombo->addItem(tr("100% — стандартный"), 100);
    scaleCombo->addItem(tr("110%"),               110);
    scaleCombo->addItem(tr("125% — крупный"),     125);
    int curScale = AppSettings::get().uiScale();
    for (int i = 0; i < scaleCombo->count(); ++i)
        if (scaleCombo->itemData(i).toInt() == curScale) { scaleCombo->setCurrentIndex(i); break; }
    auto *scaleHint = new QLabel(
        tr("Изменение масштаба требует перезапуска приложения."), scaleGroup);
    scaleHint->setWordWrap(true);
    scaleHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    scaleLayout->addWidget(scaleCombo);
    scaleLayout->addWidget(scaleHint);

    // Добавляем группы на вкладку и растягиваем (addStretch → свободное место снизу)
    sysLayout->addWidget(hkGroup);
    sysLayout->addWidget(startGroup);
    sysLayout->addWidget(langGroup);
    sysLayout->addWidget(scaleGroup);
    sysLayout->addStretch();

    tabs->addTab(sysTab, tr("Система"), QChar(0xE770));

    // ═══════════════════════════════════════════════════════════════════════════
    //  Вкладка 2: Оформление — анимации, карточки, затемнение
    //  (раньше — отдельное окно по кнопке 🖌)
    // ═══════════════════════════════════════════════════════════════════════════
    // Названия анимаций и часть подписей уже переведены в контексте MainWindow
    auto mw = [](const char *s) { return QCoreApplication::translate("MainWindow", s); };
    const QString hintCss = "color: rgba(255,255,255,100); font-size: 11px;";
    auto *lookTab    = new QWidget();
    auto *lookLayout = new QVBoxLayout(lookTab);
    lookLayout->setContentsMargins(14, 14, 14, 14);
    lookLayout->setSpacing(12);

    // ── Анимации: у каждой своя скорость, у прокрутки — ещё и вид ────────────
    auto *animGroup  = new QGroupBox(tr("Анимации"), lookTab);
    auto *animLayout = new QVBoxLayout(animGroup);
    animLayout->setSpacing(4);
    auto *animIntro = new QLabel(mw("Скорость каждой анимации настраивается отдельно: "
                                    "левее — медленнее, правее — быстрее."), animGroup);
    animIntro->setWordWrap(true);
    animIntro->setStyleSheet(hintCss);
    animLayout->addWidget(animIntro);

    struct AnimRow { QString key; QComboBox *style; QSlider *speed; };  // style — только у прокрутки
    QList<AnimRow> animRows;
    const QString sliderCss =
        "QSlider::groove:horizontal{height:4px;background:rgba(255,255,255,30);border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:rgba(160,200,255,170);border-radius:2px;}"
        "QSlider::handle:horizontal{width:14px;height:14px;margin:-5px 0;border-radius:7px;"
        "background:#e0e8ff;}";
    for (const Anim::KindInfo &info : Anim::kinds()) {
        const QString key = QString::fromLatin1(info.key);
        auto *title = new QLabel(mw(info.title), animGroup);
        title->setStyleSheet("color:#e0e0f0;font-size:13px;font-weight:600;");
        auto *hint = new QLabel(mw(info.hint), animGroup);
        hint->setWordWrap(true);
        hint->setStyleSheet(hintCss);

        QComboBox *style = nullptr;
        if (info.kind == Anim::Kind::Scroll) {
            style = new QComboBox(animGroup);
            style->addItem(mw("Дуга"), QStringLiteral("arc"));
            style->addItem(mw("Плавная"), QStringLiteral("smooth"));
            style->setCurrentIndex(qMax(0, style->findData(AppSettings::get().scrollStyle())));
            style->setFixedWidth(130);
        }
        auto *speed = new QSlider(Qt::Horizontal, animGroup);
        speed->setRange(50, 200);
        speed->setSingleStep(10);
        speed->setPageStep(25);
        speed->setValue(AppSettings::get().animSpeed(key));
        speed->setStyleSheet(sliderCss);
        speed->setToolTip(mw("Скорость: левее — медленнее, правее — быстрее"));
        auto *speedLbl = new QLabel(animGroup);
        speedLbl->setStyleSheet(hintCss);
        speedLbl->setFixedWidth(42);
        speedLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto showSpeed = [speedLbl](int v) { speedLbl->setText(QString("%1%").arg(v)); };
        showSpeed(speed->value());
        connect(speed, &QSlider::valueChanged, this, showSpeed);

        auto *row = new QHBoxLayout();
        row->setSpacing(12);
        if (style) row->addWidget(style);
        row->addWidget(speed, 1);
        row->addWidget(speedLbl);
        animLayout->addSpacing(8);
        animLayout->addWidget(title);
        animLayout->addWidget(hint);
        animLayout->addLayout(row);
        animRows << AnimRow{ key, style, speed };
    }
    // Анимации запрещены самой Windows — честно об этом говорим
    if (!Anim::systemAllows()) {
        auto *sysLbl = new QLabel(
            mw("⚠ В Windows выключена анимация интерфейса, поэтому SmartClip тоже "
               "работает без неё.\nВключить: Параметры → Специальные возможности → "
               "Визуальные эффекты → Эффекты анимации."), animGroup);
        sysLbl->setWordWrap(true);
        sysLbl->setStyleSheet("color:#ffcc66;font-size:11px;");
        animLayout->addWidget(sysLbl);
    }
    // «По умолчанию» — прокрутка «Дуга» и везде скорость 100%
    auto *animResetBtn = new QPushButton(mw("По умолчанию"), animGroup);
    animResetBtn->setCursor(Qt::PointingHandCursor);
    connect(animResetBtn, &QPushButton::clicked, this, [animRows]() {
        for (const AnimRow &r : animRows) {
            if (r.style) r.style->setCurrentIndex(0);
            r.speed->setValue(100);
        }
    });
    auto *resetRow = new QHBoxLayout();
    resetRow->addStretch();
    resetRow->addWidget(animResetBtn);
    animLayout->addSpacing(8);
    animLayout->addLayout(resetRow);

    // ── Карточки ──────────────────────────────────────────────────────────────
    auto *cardGroup  = new QGroupBox(tr("Карточки"), lookTab);
    auto *cardLayout = new QVBoxLayout(cardGroup);
    auto *hoverCheck = new SdCheckBox(tr("Карточка выезжает и увеличивается при наведении"), cardGroup);
    hoverCheck->setChecked(AppSettings::get().cardHover());
    auto *hoverHint = new QLabel(
        tr("Карточка под мышью плавно выдвигается к центру экрана и чуть увеличивается, "
           "раздвигая соседние."), cardGroup);
    hoverHint->setWordWrap(true);
    hoverHint->setStyleSheet(hintCss);
    cardLayout->addWidget(hoverCheck);
    cardLayout->addWidget(hoverHint);

    // Шкала дат сбоку колонки при прокрутке
    auto *rulerRow = new QHBoxLayout();
    auto *rulerLbl = new QLabel(tr("Шкала дат при прокрутке:"), cardGroup);
    auto *rulerCombo = new QComboBox(cardGroup);
    rulerCombo->addItem(tr("У края экрана"), QStringLiteral("outer"));
    rulerCombo->addItem(tr("Со стороны центра"), QStringLiteral("inner"));
    rulerCombo->addItem(tr("Выключена"), QStringLiteral("off"));
    rulerCombo->setCurrentIndex(qMax(0, rulerCombo->findData(AppSettings::get().dateRuler())));
    rulerRow->addWidget(rulerLbl);
    rulerRow->addWidget(rulerCombo);
    rulerRow->addStretch();
    auto *rulerHint = new QLabel(
        tr("Пока листаешь историю, сбоку колонки видно, к какому дню относятся карточки."), cardGroup);
    rulerHint->setWordWrap(true);
    rulerHint->setStyleSheet(hintCss);
    cardLayout->addSpacing(8);
    cardLayout->addLayout(rulerRow);
    cardLayout->addWidget(rulerHint);

    // ── Затемнение (solidPanels — тёмный фон за колонками) ──────────────────
    auto *uiGroup  = new QGroupBox(tr("Внешний вид"), lookTab);
    auto *uiLayout = new QVBoxLayout(uiGroup);
    auto *blurCheck = new SdCheckBox(tr("Тёмный фон за колонками"), uiGroup);
    blurCheck->setChecked(AppSettings::get().solidPanels());
    auto *blurHint = new QLabel(
        tr("Добавляет тёмную полупрозрачную подложку за левой, правой колонками и панелями.\nДля тех, кто предпочитает непрозрачный интерфейс."), uiGroup);
    blurHint->setWordWrap(true);
    blurHint->setStyleSheet(hintCss);
    uiLayout->addWidget(blurCheck);
    uiLayout->addWidget(blurHint);

    auto *soonLbl = new QLabel(mw("Скоро здесь: темы оформления."), lookTab);
    soonLbl->setStyleSheet(hintCss);

    lookLayout->addWidget(animGroup);
    lookLayout->addWidget(cardGroup);
    lookLayout->addWidget(uiGroup);
    lookLayout->addWidget(soonLbl);
    lookLayout->addStretch();
    tabs->addTab(lookTab, tr("Оформление"), QChar(0xE771));  // Кисточка

    // ═══════════════════════════════════════════════════════════════════════════
    //  Вкладка 2: История
    //  Завёрнута в QScrollArea — содержимого много, не всегда помещается
    // ═══════════════════════════════════════════════════════════════════════════
    auto *histScroll = new QScrollArea();
    histScroll->setWidgetResizable(true);      // Виджет внутри подстраивается по ширине
    histScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *histTab    = new QWidget();
    auto *histLayout = new QVBoxLayout(histTab);
    histLayout->setContentsMargins(14, 14, 14, 14);
    histLayout->setSpacing(12);

    // ── Лимит записей ─────────────────────────────────────────────────────────
    // QFormLayout — удобный лейаут для формы: «метка» | «поле ввода».
    auto *limitGroup  = new QGroupBox(tr("Размер буфера"), histTab);
    auto *limitLayout = new QFormLayout(limitGroup);
    limitLayout->setSpacing(8);
    // Раздельные лимиты: тексты и картинки (видео — вместе с картинками).
    // Справа от поля — сколько занято сейчас.
    const int usedTexts  = db->countHistory("text") + db->countHistory("audio");
    const int usedImages = db->countHistory("image") + db->countHistory("video");
    auto makeLimitRow = [&](int value, int used) {
        auto *spin = new QSpinBox(limitGroup);
        spin->setRange(5, 10000);
        spin->setSingleStep(10);
        spin->setValue(value);
        spin->setFixedWidth(130);
        auto *usedLbl = new QLabel(tr("сейчас: %1").arg(used), limitGroup);
        usedLbl->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
        auto *row = new QHBoxLayout();
        row->addWidget(spin);
        row->addWidget(usedLbl);
        row->addStretch();
        return std::make_pair(spin, row);
    };
    auto textsLimit  = makeLimitRow(AppSettings::get().maxTexts(),  usedTexts);
    auto imagesLimit = makeLimitRow(AppSettings::get().maxImages(), usedImages);
    QSpinBox *maxTextsSpin  = textsLimit.first;
    QSpinBox *maxImagesSpin = imagesLimit.first;
    auto *limitHint = new QLabel(
        tr("Тексты и картинки хранятся раздельно: одни больше не вытесняют другие.\n"
           "При превышении удаляются самые старые записи этого вида."), limitGroup);
    limitHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    limitLayout->addRow(tr("Максимум текстов:"), textsLimit.second);
    limitLayout->addRow(tr("Максимум картинок:"), imagesLimit.second);
    limitLayout->addRow(limitHint);

    // ── Поведение при копировании ─────────────────────────────────────────────
    auto *behavGroup  = new QGroupBox(tr("Поведение"), histTab);
    auto *behavLayout = new QVBoxLayout(behavGroup);
    behavLayout->setSpacing(10);
    auto *saveImagesCheck = new SdCheckBox(tr("Сохранять изображения из буфера"), behavGroup);
    saveImagesCheck->setChecked(AppSettings::get().saveImages());
    // Дедупликация: не сохранять если такой текст уже есть в истории
    auto *dedupCheck = new SdCheckBox(
        tr("Без повторов — повторно скопированное поднимается наверх"), behavGroup);
    dedupCheck->setChecked(AppSettings::get().deduplication());
    // Минимальная длина текста — игнорировать слишком короткие копирования
    auto *minLenLayout = new QHBoxLayout();
    auto *minLenLabel  = new QLabel(tr("Минимальная длина текста:"), behavGroup);
    auto *minLenSpin   = new QSpinBox(behavGroup);
    minLenSpin->setRange(1, 500);
    minLenSpin->setValue(AppSettings::get().minTextLength());
    minLenSpin->setFixedWidth(110);  // Шире: справа кнопки со стрелками
    auto *minLenHint = new QLabel(tr("симв."), behavGroup);
    minLenHint->setStyleSheet("color: rgba(255,255,255,100);");
    minLenLayout->addWidget(minLenLabel);
    minLenLayout->addWidget(minLenSpin);
    minLenLayout->addWidget(minLenHint);
    minLenLayout->addStretch();
    behavLayout->addWidget(saveImagesCheck);
    behavLayout->addWidget(dedupCheck);
    behavLayout->addLayout(minLenLayout);

    // ── Автоочистка ───────────────────────────────────────────────────────────
    // setSpecialValueText("Выкл") — когда значение = minimum (0), показывает "Выкл".
    auto *cleanGroup  = new QGroupBox(tr("Автоочистка"), histTab);
    auto *cleanLayout = new QHBoxLayout(cleanGroup);
    cleanLayout->setSpacing(8);
    auto *cleanLabel = new QLabel(tr("Удалять записи старше"), cleanGroup);
    auto *cleanSpin  = new QSpinBox(cleanGroup);
    cleanSpin->setRange(0, 365);
    cleanSpin->setValue(AppSettings::get().autocleanDays());
    cleanSpin->setSpecialValueText(tr("Выкл"));  // 0 → "Выкл"
    cleanSpin->setFixedWidth(120);  // Шире: справа кнопки со стрелками
    auto *cleanDaysLabel = new QLabel(tr("дней"), cleanGroup);
    cleanDaysLabel->setStyleSheet("color: rgba(255,255,255,100);");
    cleanLayout->addWidget(cleanLabel);
    cleanLayout->addWidget(cleanSpin);
    cleanLayout->addWidget(cleanDaysLabel);
    cleanLayout->addStretch();

    // ── Исключения приложений ─────────────────────────────────────────────────
    // Список программ из которых НЕ сохранять в историю (например: KeePass).
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
    // Заполняем список сохранёнными исключениями
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

    // «+ Из истории» — показывает список приложений из БД
    connect(addFromHistBtn, &QPushButton::clicked, [=]() {
        QStringList apps = db->getAppNames();
        if (apps.isEmpty()) {
            sdInfo(this, tr("История пуста"),
                   tr("В истории пока нет записей с именами приложений."));
            return;
        }
        // Убираем приложения которые уже в списке исключений
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

    // «+ Вручную» — пользователь вводит имя .exe сам
    connect(addManualBtn, &QPushButton::clicked, [=]() {
        QString name = sdInputText(this, tr("Добавить исключение"),
                                   tr("Имя .exe файла (без расширения):"));
        if (!name.isEmpty()) exclList->addItem(name);
    });

    // «✕ Удалить» — удаляет выделенные строки из списка
    connect(exclDelBtn, &QPushButton::clicked, [=]() {
        for (auto *item : exclList->selectedItems()) delete item;
    });

    // ── Формат изображений ────────────────────────────────────────────────────
    // PNG — без потерь (больше места), JPEG — меньше места (с артефактами).
    // qualSpin активен только при выборе JPEG (для PNG качество не настраивается).
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
    qualSpin->setSuffix("%");  // Добавляет «%» после числа: «85%»
    qualSpin->setFixedWidth(105);  // Шире: справа кнопки со стрелками
    qualRow->addWidget(qualLabel); qualRow->addWidget(qualSpin); qualRow->addStretch();
    qualSpin->setEnabled(fmtCombo->currentIndex() == 1);  // Активно только для JPEG
    // QOverload<int>::of — уточняем какой именно сигнал currentIndexChanged (с int)
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

    histScroll->setWidget(histTab);  // Вставляем виджет в прокручиваемую область
    tabs->addTab(histScroll, tr("История"), QChar(0xE81C));

    // ═══════════════════════════════════════════════════════════════════════════
    //  Вкладка 3: Закрепы
    //  Опции поведения при закреплении и настройка вкладки «Все»
    // ═══════════════════════════════════════════════════════════════════════════
    auto *pinsTab    = new QWidget();
    auto *pinsLayout = new QVBoxLayout(pinsTab);
    pinsLayout->setContentsMargins(14, 14, 14, 14);
    pinsLayout->setSpacing(12);

    auto *pinsGroup  = new QGroupBox(tr("Поведение"), pinsTab);
    auto *pinsGrpLay = new QVBoxLayout(pinsGroup);
    pinsGrpLay->setSpacing(10);
    // noName — закреп без имени (не спрашивать название)
    auto *noNameCheck = new SdCheckBox(tr("Не спрашивать название при закреплении"), pinsGroup);
    noNameCheck->setChecked(AppSettings::get().pinsNoName());
    auto *noNameHint = new QLabel(
        tr("Закреп сохраняется без имени, диалог названия не появляется."), pinsGroup);
    noNameHint->setWordWrap(true);
    noNameHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px; margin-bottom: 6px;");
    // noFolder — закреп без папки (не спрашивать куда сохранить)
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

    // Лимит «недавних» во вкладке «Все»
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

    tabs->addTab(pinsTab, tr("Закрепы"), QChar(0xE718));

    // ═══════════════════════════════════════════════════════════════════════════
    //  Вкладка 4: Экспорт / Импорт
    //  JSON-бекап закрепов, профилей, настроек; автобекап по расписанию
    // ═══════════════════════════════════════════════════════════════════════════
    auto *ioTab    = new QWidget();
    auto *ioLayout = new QVBoxLayout(ioTab);
    ioLayout->setContentsMargins(14, 14, 14, 14);
    ioLayout->setSpacing(12);

    // ── Экспорт ───────────────────────────────────────────────────────────────
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

    // ── Импорт ────────────────────────────────────────────────────────────────
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

    // ── Автобекап ─────────────────────────────────────────────────────────────
    // Автоматически сохраняет бекап в заданную папку раз в N дней.
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
    backupDaysSpin->setFixedWidth(120);  // Шире: справа кнопки со стрелками
    backupDaysRow->addWidget(backupDaysLabel);
    backupDaysRow->addWidget(backupDaysSpin);
    backupDaysRow->addStretch();
    auto *backupPathRow   = new QHBoxLayout();
    auto *backupPathEdit  = new QLineEdit(backupGroup);
    backupPathEdit->setText(AppSettings::get().autoBackupPath());
    backupPathEdit->setPlaceholderText(tr("Папка для бекапов..."));
    // Значок папки из системного шрифта (эмодзи «📁» здесь не отображался)
    auto *backupBrowseBtn = new QPushButton(QString(QChar(0xE8B7)), backupGroup);
    backupBrowseBtn->setStyleSheet(QString("QPushButton{font-family:'%1';font-size:14px;padding:0;min-height:26px;}")
                              .arg(SmartButton::iconFontFamily()));
    backupBrowseBtn->setFixedWidth(32);
    backupBrowseBtn->setToolTip(tr("Выбрать папку"));
    backupPathRow->addWidget(backupPathEdit);
    backupPathRow->addWidget(backupBrowseBtn);
    // Кнопка 📁 — открывает диалог выбора папки
    connect(backupBrowseBtn, &QPushButton::clicked, [=]() {
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("Папка для автобекапов"), backupPathEdit->text());
        if (!dir.isEmpty()) backupPathEdit->setText(dir);
    });
    // Блокируем/разблокируем поля в зависимости от состояния чекбокса
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


    // ═══════════════════════════════════════════════════════════════════════════
    //  Вкладка 5: Специальные возможности (Бета)
    //  Экспериментальные функции + обратная связь + версия + обновления
    // ═══════════════════════════════════════════════════════════════════════════
    auto *betaTab    = new QWidget();
    auto *betaLayout = new QVBoxLayout(betaTab);
    betaLayout->setContentsMargins(14, 14, 14, 14);
    betaLayout->setSpacing(12);

    // Предупреждение что функции экспериментальные
    auto *betaHintLabel = new QLabel(
        tr("⚡ Бета-функции — могут работать нестабильно.\n"
           "Присылай баги, будем фиксить!"), betaTab);
    betaHintLabel->setWordWrap(true);
    betaHintLabel->setStyleSheet(
        "color:#ffcc44;font-size:11px;"
        "background:rgba(50,40,10,120);border:1px solid rgba(200,160,0,60);"
        "border-radius:6px;padding:7px 10px;");

    // ── Автозахват записей экрана ─────────────────────────────────────────────
    // VideoWatcher следит за папкой и добавляет новые видео в историю.
    auto *videoGroup  = new QGroupBox(tr("Автозахват записей и аудио"), betaTab);
    auto *videoLayout = new QVBoxLayout(videoGroup);
    videoLayout->setSpacing(8);
    auto *videoCheck = new SdCheckBox(
        tr("Автоматически добавлять новые записи в историю"), videoGroup);
    videoCheck->setChecked(AppSettings::get().videoMonitorEnabled());
    auto *videoHint = new QLabel(
        tr("Следит за папками: новое видео (Xbox Game Bar, «Ножницы») появится в колонке картинок, "
           "аудио (Звукозапись и т.п.) — в колонке текстов. Клик по карточке — вставить файл "
           "в Telegram, Discord и т.д. Папок может быть несколько — у каждого они свои."),
        videoGroup);
    videoHint->setWordWrap(true);
    videoHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");

    // Папки: список, «+ Папка», «Стандартные», «Удалить»
    auto *videoList = new QListWidget(videoGroup);
    videoList->setFixedHeight(96);
    for (const QString &f : VideoWatcher::configuredFolders())
        videoList->addItem(QDir::toNativeSeparators(f));
    auto *videoBtnRow  = new QHBoxLayout();
    auto *videoAddBtn  = new QPushButton(tr("+ Папка"), videoGroup);
    auto *videoStdBtn  = new QPushButton(tr("Стандартные"), videoGroup);
    auto *videoDelBtn  = new QPushButton(tr("✕ Удалить"), videoGroup);
    videoAddBtn->setStyleSheet(
        "QPushButton{background:rgba(20,25,55,220);color:#aaaadd;"
        "border:1px solid rgba(80,80,160,80);border-radius:5px;padding:4px 12px;font-size:11px;}"
        "QPushButton:hover{background:rgba(28,32,80,240);color:#fff;}");
    videoStdBtn->setStyleSheet(videoAddBtn->styleSheet());
    videoDelBtn->setStyleSheet(
        "QPushButton{background:rgba(55,18,18,220);color:#ddaaaa;"
        "border:1px solid rgba(160,60,60,80);border-radius:5px;padding:4px 12px;font-size:11px;}"
        "QPushButton:hover{background:rgba(90,25,25,240);color:#fff;}");
    for (QPushButton *btn : {videoAddBtn, videoStdBtn, videoDelBtn}) btn->setCursor(Qt::PointingHandCursor);
    videoAddBtn->setToolTip(tr("Добавить папку, за которой следить"));
    videoStdBtn->setToolTip(tr("Добавить стандартные папки: клипы Xbox Game Bar, «Записи экрана», «Звукозаписи»"));
    videoBtnRow->addWidget(videoAddBtn);
    videoBtnRow->addWidget(videoStdBtn);
    videoBtnRow->addStretch();
    videoBtnRow->addWidget(videoDelBtn);
    auto listHas = [videoList](const QString &path) {
        const QString n = QDir::cleanPath(QDir::fromNativeSeparators(path));
        for (int i = 0; i < videoList->count(); ++i)
            if (QDir::cleanPath(QDir::fromNativeSeparators(videoList->item(i)->text()))
                    .compare(n, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    connect(videoAddBtn, &QPushButton::clicked, this, [=]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Папка с записями"));
        if (!dir.isEmpty() && !listHas(dir)) videoList->addItem(QDir::toNativeSeparators(dir));
    });
    connect(videoStdBtn, &QPushButton::clicked, this, [=]() {
        for (const QString &f : VideoWatcher::defaultFolders())
            if (!listHas(f)) videoList->addItem(QDir::toNativeSeparators(f));
    });
    connect(videoDelBtn, &QPushButton::clicked, this, [videoList]() {
        delete videoList->takeItem(videoList->currentRow());
    });
    // Блокируем список, если автозахват выключен
    auto toggleVideo = [=](bool on) {
        for (QWidget *wgt : std::initializer_list<QWidget *>{videoList, videoAddBtn, videoStdBtn, videoDelBtn})
            wgt->setEnabled(on);
    };
    toggleVideo(videoCheck->isChecked());
    connect(videoCheck, &QCheckBox::toggled, toggleVideo);
    videoLayout->addWidget(videoCheck);
    videoLayout->addWidget(videoHint);
    videoLayout->addWidget(videoList);
    videoLayout->addLayout(videoBtnRow);

    // ── Кнопка «Сообщить об ошибке» ──────────────────────────────────────────
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
        showFeedbackDialog(this);  // Показываем диалог обратной связи
    });

    // ── Версия ────────────────────────────────────────────────────────────────
    // APP_VERSION — константа из Version.h, например "1.0.2".
    // QLatin1String — быстрое создание QString из ASCII-строки.
    auto *versionLbl = new QLabel(
        tr("SmartClip v%1").arg(QLatin1String(APP_VERSION)), betaTab);
    versionLbl->setStyleSheet(
        "color:#ffffff;font-size:15px;font-weight:600;background:transparent;");

    // ── Ручная проверка обновлений ────────────────────────────────────────────
    // При нажатии создаём UpdateChecker и запускаем check(false) (false = не показывать
    // диалог при актуальной версии, только если есть обновление).
    auto *checkUpdBtn = new QPushButton(tr("🔍  Проверить наличие обновлений"), betaTab);
    checkUpdBtn->setStyleSheet(
        "QPushButton{background:rgba(255,255,255,12);color:rgba(255,255,255,180);"
        "border:1px solid rgba(255,255,255,40);border-radius:6px;padding:6px 14px;font-size:12px;}"
        "QPushButton:hover{background:rgba(255,255,255,22);color:#fff;}");
    checkUpdBtn->setCursor(Qt::PointingHandCursor);

    connect(checkUpdBtn, &QPushButton::clicked, this, [this, checkUpdBtn]() {
        checkUpdBtn->setEnabled(false);
        checkUpdBtn->setText(tr("⏳  Проверка..."));

        // UpdateChecker — объект одноразового запроса к GitHub API.
        // Родитель this — если диалог закроется, Qt удалит uc автоматически.
        auto *uc = new UpdateChecker(this);

        // Нашли новую версию — обновляем плашку и текст кнопки
        connect(uc, &UpdateChecker::updateAvailable, this,
                [this, checkUpdBtn](const QString &ver, const QString &url, const QString &notes) {
            setPendingUpdate(ver, url, notes);
            checkUpdBtn->setText(tr("🆙  Найдена версия %1").arg(ver));
            checkUpdBtn->setEnabled(true);
        });
        // Версия актуальная — сообщаем пользователю
        connect(uc, &UpdateChecker::upToDate, this, [checkUpdBtn]() {
            checkUpdBtn->setText(tr("✓  Уже актуальная версия"));
            checkUpdBtn->setEnabled(true);
        });
        // Ошибка соединения — сообщаем пользователю (временно показываем текст
        // реальной ошибки в тултипе, чтобы понять причину сбоя на конкретной машине)
        connect(uc, &UpdateChecker::checkFailed, this, [this, checkUpdBtn](const QString &err) {
            checkUpdBtn->setText(tr("✕  Ошибка соединения"));
            checkUpdBtn->setToolTip(err);
            checkUpdBtn->setEnabled(true);
            sdInfo(this, tr("Ошибка соединения"), err);
        });

        // check(false) — false означает «не форсировать показ диалога при upToDate»
        uc->check(false);
    });

    // ── Плашка обновления (скрыта до setPendingUpdate) ────────────────────────
    // Показывается когда UpdateChecker нашёл новую версию.
    // m_updateBanner — хранится в h-файле чтобы setPendingUpdate() мог к ней обратиться.
    m_updateBanner = new QWidget(this);
    m_updateBanner->setObjectName("updateBanner");
    m_updateBanner->setStyleSheet(
        "#updateBanner{background:rgba(120,190,255,22);border:1px solid rgba(120,190,255,90);"
        "border-radius:10px;}");
    m_updateBanner->hide();  // Скрыта до получения информации об обновлении

    auto *bannerRow  = new QHBoxLayout(m_updateBanner);
    bannerRow->setContentsMargins(14, 10, 14, 10);
    bannerRow->setSpacing(10);

    auto *bannerIcon = new QLabel(QString(QChar(0xE896)), m_updateBanner);  // Стрелка «скачать»
    bannerIcon->setFont(QFont(SmartButton::iconFontFamily()));
    bannerIcon->setStyleSheet("background:transparent; color:#cfe6ff; font-size:16px;");

    // bannerText имеет objectName — по нему найдём этот QLabel в setPendingUpdate()
    // через findChild<QLabel*>("bannerText")
    auto *bannerText = new QLabel(m_updateBanner);
    bannerText->setObjectName("bannerText");
    bannerText->setStyleSheet(
        "background:transparent;color:rgba(220,235,255,235);font-size:12px;");
    bannerText->setWordWrap(true);

    auto *bannerBtn = new QPushButton(tr("Что нового?"), m_updateBanner);
    bannerBtn->setCursor(Qt::PointingHandCursor);
    bannerBtn->setStyleSheet(
        "QPushButton{background:rgba(120,190,255,220);color:#0e1a2c;"
        "border:none;border-radius:6px;padding:6px 14px;font-weight:600;font-size:12px;}"
        "QPushButton:hover{background:rgba(150,205,255,240);}");

    bannerRow->addWidget(bannerIcon);
    bannerRow->addWidget(bannerText, 1);
    bannerRow->addWidget(bannerBtn);

    // Кнопка «Обновить» — отправляет сигнал updateRequested и закрывает диалог.
    connect(bannerBtn, &QPushButton::clicked, this, [this]() {
        // Сначала закрываем настройки, и только потом MainWindow открывает окно
        // обновления. Раньше оно открывалось ПОВЕРХ ещё открытых настроек
        // (два вложенных модальных окна).
        emit updateRequested(m_pendingVersion, m_pendingUrl, m_pendingNotes);
        reject();
    });

    // Блок «О программе»: версия, проверка обновлений и плашка обновления
    auto *aboutGroup  = new QGroupBox(tr("О программе"), betaTab);
    auto *aboutLayout = new QVBoxLayout(aboutGroup);
    aboutLayout->setSpacing(8);
    auto *aboutRow = new QHBoxLayout();
    aboutRow->addWidget(versionLbl);
    aboutRow->addStretch();
    aboutRow->addWidget(checkUpdBtn);
    aboutLayout->addLayout(aboutRow);
    // Открытые библиотеки (лицензия LGPL требует об этом сказать)
    auto *creditsLbl = new QLabel(
        tr("Видео и аудио воспроизводятся с помощью <a style='color:#9cc8ff' href='https://ffmpeg.org'>FFmpeg</a> "
           "(LGPL), интерфейс — <a style='color:#9cc8ff' href='https://www.qt.io'>Qt</a> (LGPL). "
           "Подробнее — в файле THIRD_PARTY_NOTICES.txt в папке программы."), aboutGroup);
    creditsLbl->setWordWrap(true);
    creditsLbl->setOpenExternalLinks(true);
    creditsLbl->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");
    aboutLayout->addWidget(creditsLbl);

    betaLayout->addWidget(betaHintLabel);
    betaLayout->addWidget(feedbackBtn);
    betaLayout->addWidget(aboutGroup);
    betaLayout->addStretch();

    // ═══════════════════════════════════════════════════════════════════════════
    //  Вкладка: Файлы — полоска свежих файлов внизу окна и автозахват записей
    // ═══════════════════════════════════════════════════════════════════════════
    auto *filesScroll = new QScrollArea();
    filesScroll->setWidgetResizable(true);
    filesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *filesTab    = new QWidget();
    auto *filesLayout = new QVBoxLayout(filesTab);
    filesLayout->setContentsMargins(14, 14, 14, 14);
    filesLayout->setSpacing(12);

    auto *stripGroup  = new QGroupBox(tr("Полоска файлов"), filesTab);
    auto *stripLayout = new QVBoxLayout(stripGroup);
    stripLayout->setSpacing(8);
    auto *stripCheck = new SdCheckBox(tr("Показывать свежие файлы внизу окна"), stripGroup);
    stripCheck->setChecked(AppSettings::get().filesStrip());
    auto *stripHint = new QLabel(
        tr("Полоска над поиском: наведи мышь — раскроется. Клик вставляет файл, "
           "его можно и перетащить мышкой в Telegram, Discord и т.д."), stripGroup);
    stripHint->setWordWrap(true);
    stripHint->setStyleSheet("color: rgba(255,255,255,100); font-size: 11px;");

    // Папки полоски: список, «+ Папка», «Стандартные», «Удалить»
    auto *stripList = new QListWidget(stripGroup);
    stripList->setFixedHeight(72);
    for (const QString &f : Files::configuredFolders())
        stripList->addItem(QDir::toNativeSeparators(f));
    auto *stripBtnRow = new QHBoxLayout();
    auto *stripAddBtn = new QPushButton(tr("+ Папка"), stripGroup);
    auto *stripStdBtn = new QPushButton(tr("Стандартные"), stripGroup);
    auto *stripDelBtn = new QPushButton(tr("✕ Удалить"), stripGroup);
    stripAddBtn->setStyleSheet(videoAddBtn->styleSheet());
    stripStdBtn->setStyleSheet(videoAddBtn->styleSheet());
    stripDelBtn->setStyleSheet(videoDelBtn->styleSheet());
    for (QPushButton *btn : {stripAddBtn, stripStdBtn, stripDelBtn}) btn->setCursor(Qt::PointingHandCursor);
    stripAddBtn->setToolTip(tr("Добавить папку, файлы из которой показывать"));
    stripStdBtn->setToolTip(tr("Добавить папку «Загрузки»"));
    stripBtnRow->addWidget(stripAddBtn);
    stripBtnRow->addWidget(stripStdBtn);
    stripBtnRow->addStretch();
    stripBtnRow->addWidget(stripDelBtn);
    auto stripHas = [stripList](const QString &path) {
        const QString n = QDir::cleanPath(QDir::fromNativeSeparators(path));
        for (int i = 0; i < stripList->count(); ++i)
            if (QDir::cleanPath(QDir::fromNativeSeparators(stripList->item(i)->text()))
                    .compare(n, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    connect(stripAddBtn, &QPushButton::clicked, this, [=]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Папка с файлами"));
        if (!dir.isEmpty() && !stripHas(dir)) stripList->addItem(QDir::toNativeSeparators(dir));
    });
    connect(stripStdBtn, &QPushButton::clicked, this, [=]() {
        for (const QString &f : Files::defaultFolders())
            if (!stripHas(f)) stripList->addItem(QDir::toNativeSeparators(f));
    });
    connect(stripDelBtn, &QPushButton::clicked, this, [stripList]() {
        delete stripList->takeItem(stripList->currentRow());
    });

    // Срок, количество, вид
    auto *stripForm = new QFormLayout();
    stripForm->setSpacing(8);
    auto *stripDaysSpin = new QSpinBox(stripGroup);
    stripDaysSpin->setRange(1, 90);
    stripDaysSpin->setValue(AppSettings::get().filesDays());
    stripDaysSpin->setSuffix(tr(" дн."));
    stripDaysSpin->setFixedWidth(120);
    auto *stripMaxSpin = new QSpinBox(stripGroup);
    stripMaxSpin->setRange(5, 100);
    stripMaxSpin->setSingleStep(5);
    stripMaxSpin->setValue(AppSettings::get().filesMax());
    stripMaxSpin->setFixedWidth(120);
    auto *stripViewCombo = new QComboBox(stripGroup);
    stripViewCombo->addItem(tr("Плитки"), "tiles");
    stripViewCombo->addItem(tr("Список"), "list");
    stripViewCombo->setCurrentIndex(qMax(0, stripViewCombo->findData(AppSettings::get().filesView())));
    stripViewCombo->setFixedWidth(120);
    stripForm->addRow(tr("За последние:"), stripDaysSpin);
    stripForm->addRow(tr("Сколько файлов:"), stripMaxSpin);
    stripForm->addRow(tr("Вид:"), stripViewCombo);

    stripLayout->addWidget(stripCheck);
    stripLayout->addWidget(stripHint);
    stripLayout->addWidget(stripList);
    stripLayout->addLayout(stripBtnRow);
    stripLayout->addLayout(stripForm);

    // Какие типы файлов попадают в полоску
    auto *typesGroup = new QGroupBox(tr("Какие файлы показывать"), filesTab);
    auto *typesGrid  = new QGridLayout(typesGroup);
    typesGrid->setHorizontalSpacing(16);
    typesGrid->setVerticalSpacing(8);
    const QStringList enabledTypes = AppSettings::get().filesTypes();
    const QList<QPair<QString, QString>> typeNames = {
        {"image",    tr("Картинки")},
        {"video",    tr("Видео")},
        {"audio",    tr("Аудио")},
        {"docs",     tr("Документы (PDF, Word, Excel…)")},
        {"archives", tr("Архивы (ZIP, RAR, 7z…)")},
        {"programs", tr("Программы и установщики")},
        {"other",    tr("Остальные")},
    };
    QList<QPair<QString, SdCheckBox *>> typeChecks;
    for (int i = 0; i < typeNames.size(); ++i) {
        auto *cb = new SdCheckBox(typeNames[i].second, typesGroup);
        cb->setChecked(enabledTypes.contains(typeNames[i].first));
        typesGrid->addWidget(cb, i / 2, i % 2);
        typeChecks.append({typeNames[i].first, cb});
    }

    // Полоска выключена — её настройки неактивны
    auto toggleStrip = [=](bool on) {
        for (QWidget *wgt : std::initializer_list<QWidget *>{stripList, stripAddBtn, stripStdBtn, stripDelBtn,
                                                             stripDaysSpin, stripMaxSpin, stripViewCombo, typesGroup})
            wgt->setEnabled(on);
    };
    toggleStrip(stripCheck->isChecked());
    connect(stripCheck, &QCheckBox::toggled, toggleStrip);

    filesLayout->addWidget(stripGroup);
    filesLayout->addWidget(typesGroup);
    filesLayout->addWidget(videoGroup);  // Автозахват записей и аудио — тоже про файлы
    filesLayout->addStretch();
    filesScroll->setWidget(filesTab);

    tabs->addTab(filesScroll, tr("Файлы"), QChar(0xE8A5));
    tabs->addTab(ioTab, tr("Экспорт и импорт"), QChar(0xE8B7));
    tabs->addTab(betaTab, tr("Дополнительно"), QChar(0xE946));

    // ═══════════════════════════════════════════════════════════════════════════
    //  Логика экспорта (JSON-файл с закрепами, профилями, настройками)
    // ═══════════════════════════════════════════════════════════════════════════
    connect(exportBtn, &QPushButton::clicked, [=]() {
        // getSaveFileName — стандартный диалог «Сохранить как»
        QString path = QFileDialog::getSaveFileName(
            this, tr("Экспорт SmartClip"), "smartclip_backup.json",
            tr("JSON файл (*.json)"));
        if (path.isEmpty()) return;

        // Вся логика — в core/Backup.cpp (общая с автобэкапом)
        QString error;
        if (Backup::exportToFile(db, path, &error))
            sdInfo(this, tr("Экспорт завершён"),
                   tr("Данные сохранены в:\n%1").arg(path));
        else
            sdInfo(this, tr("Ошибка"), tr("Не удалось сохранить файл.") + "\n" + error);
    });

    // ═══════════════════════════════════════════════════════════════════════════
    //  Логика импорта (читаем JSON и добавляем данные в БД)
    // ═══════════════════════════════════════════════════════════════════════════
    connect(importBtn, &QPushButton::clicked, [=]() {
        QString path = QFileDialog::getOpenFileName(
            this, tr("Импорт SmartClip"), "",
            tr("JSON файл (*.json)"));
        if (path.isEmpty()) return;

        // Уже существующие закрепы/профили не дублируются — повторный импорт
        // того же файла ничего не меняет.
        const Backup::ImportResult r = Backup::importFromFile(db, path);
        if (!r.ok) {
            sdInfo(this, tr("Ошибка"),
                   tr("Файл повреждён или не является бекапом SmartClip."));
            return;
        }

        if (r.mainHotkeyChanged) emit mainHotkeyChanged(AppSettings::get().mainHotkey());
        emit settingsChanged();
        emit dataImported();  // MainWindow перезагрузит закрепы и хоткеи профилей

        QString msg = tr("Добавлено: закрепов — %1, профилей — %2.").arg(r.pinsAdded).arg(r.profilesAdded);
        if (r.pinsSkipped || r.profilesSkipped)
            msg += "\n" + tr("Пропущено (уже есть): закрепов — %1, профилей — %2.")
                              .arg(r.pinsSkipped).arg(r.profilesSkipped);
        if (r.settingsApplied)
            msg += "\n\n" + tr("Настройки из файла применены. Окно настроек будет закрыто, "
                                "чтобы старые значения не перезаписали импортированные.");
        sdInfo(this, tr("Импорт завершён"), msg);

        // Раньше после импорта окно оставалось открытым, и кнопка «Сохранить»
        // записывала СТАРЫЕ значения из полей поверх импортированных —
        // поэтому казалось, что настройки «не импортировались».
        if (r.settingsApplied) reject();
    });

    // ═══════════════════════════════════════════════════════════════════════════
    //  Кнопки «Отмена» и «Сохранить» внизу диалога
    // ═══════════════════════════════════════════════════════════════════════════
    auto *btnBox   = new QHBoxLayout();
    auto *cancelBtn = new QPushButton(tr("Отмена"), body);
    auto *saveBtn   = new QPushButton(tr("Сохранить"), body);
    saveBtn->setObjectName("saveBtn");  // По этому имени QSS применяет зелёный стиль
    cancelBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setCursor(Qt::PointingHandCursor);
    btnBox->addStretch();
    btnBox->addWidget(cancelBtn);
    btnBox->addWidget(saveBtn);

    // Плашка «Доступно обновление» — вверху окна, видна из любого раздела
    bodyLayout->addWidget(m_updateBanner);
    bodyLayout->addWidget(tabs, 1);
    bodyLayout->addLayout(btnBox);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // ── Кнопка «Сохранить» — применяет все настройки ─────────────────────────
    connect(saveBtn, &QPushButton::clicked, this, [=]() {

        // Горячая клавиша
        QString newHotkey = hotkeyEdit->text().trimmed();
        if (newHotkey.isEmpty()) newHotkey = "Ctrl+Shift+V";
        bool hotkeyChanged = (newHotkey != AppSettings::get().mainHotkey());
        AppSettings::get().setMainHotkey(newHotkey);

        // Автозапуск (записывает/удаляет ключ реестра)
        bool autostartOn = autostartCheck->isChecked();
        AppSettings::get().setAutostart(autostartOn);
        applyAutostart(autostartOn);

        // Окна администратора. Включение — создаём задачу Планировщика
        // (Windows покажет окно UAC). Отказали — галочку не сохраняем.
        bool adminRestart = false;
        const bool adminWanted = adminCheck->isChecked();
        if (adminWanted != AppSettings::get().runAsAdmin()) {
            if (adminWanted) {
                if (AdminMode::requestCreateTask()) {
                    AppSettings::get().setRunAsAdmin(true);
                    adminRestart = !AdminMode::isElevated();
                } else {
                    adminCheck->setChecked(false);
                    sdInfo(this, tr("Окна администратора"),
                           tr("Не удалось включить: Windows не дала разрешение."));
                }
            } else {
                AppSettings::get().setRunAsAdmin(false);
                AdminMode::deleteTask();  // Получится, только если мы уже с правами админа
                adminRestart = AdminMode::isElevated();
            }
        }

        // Оформление
        AppSettings::get().setSolidPanels(blurCheck->isChecked());
        AppSettings::get().setCardHover(hoverCheck->isChecked());
        AppSettings::get().setDateRuler(rulerCombo->currentData().toString());
        for (const AnimRow &r : animRows) {
            if (r.style) AppSettings::get().setScrollStyle(r.style->currentData().toString());
            AppSettings::get().setAnimSpeed(r.key, r.speed->value());
        }

        // История
        AppSettings::get().setMaxTexts(maxTextsSpin->value());
        AppSettings::get().setMaxImages(maxImagesSpin->value());
        AppSettings::get().setSaveImages(saveImagesCheck->isChecked());
        AppSettings::get().setDeduplication(dedupCheck->isChecked());
        AppSettings::get().setMinTextLength(minLenSpin->value());
        AppSettings::get().setAutocleanDays(cleanSpin->value());
        AppSettings::get().setImageFormat(fmtCombo->currentData().toString());
        AppSettings::get().setImageQuality(qualSpin->value());

        // Исключения: собираем строки из QListWidget в QStringList
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

        // Файлы: полоска внизу
        AppSettings::get().setFilesStrip(stripCheck->isChecked());
        QStringList stripFolders;
        for (int i = 0; i < stripList->count(); ++i)
            stripFolders << QDir::fromNativeSeparators(stripList->item(i)->text());
        AppSettings::get().setFilesFolders(stripFolders);
        AppSettings::get().setFilesDays(stripDaysSpin->value());
        AppSettings::get().setFilesMax(stripMaxSpin->value());
        AppSettings::get().setFilesView(stripViewCombo->currentData().toString());
        QStringList types;
        for (const auto &tc : typeChecks)
            if (tc.second->isChecked()) types << tc.first;
        AppSettings::get().setFilesTypes(types);

        // Файлы: автозахват записей и аудио
        AppSettings::get().setVideoMonitorEnabled(videoCheck->isChecked());
        QStringList folders;
        for (int i = 0; i < videoList->count(); ++i)
            folders << QDir::fromNativeSeparators(videoList->item(i)->text());
        AppSettings::get().setMediaFolders(folders);

        // Отправляем сигналы об изменениях
        if (hotkeyChanged) emit mainHotkeyChanged(newHotkey);
        emit settingsChanged();

        // Язык — проверяем изменился ли (нужен перезапуск)
        QString newLang  = langCombo->currentData().toString();
        bool langChanged = (newLang != AppSettings::get().language());
        AppSettings::get().setLanguage(newLang);

        // Масштаб — тоже требует перезапуска
        int newScale   = scaleCombo->currentData().toInt();
        bool scaleChanged = (newScale != AppSettings::get().uiScale());
        AppSettings::get().setUiScale(newScale);

        accept();  // Закрываем диалог с результатом «принято»

        // Если сменился язык, масштаб или права — предлагаем перезапуск
        if (langChanged || scaleChanged || adminRestart) {
            if (sdQuestion(parentWidget(),
                           tr("Перезапуск"),
                           tr("Для применения изменений необходимо перезапустить приложение.\n"
                              "Перезапустить сейчас?"))) {
                // setRestartRequested → main.cpp перезапустит SmartClip через QProcess::startDetached
                AppSettings::get().setRestartRequested(true);
                // singleShot(0) — выход из цикла событий после завершения текущего обработчика
                QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            }
        }
    });
}

// ─── setPendingUpdate — показать/обновить плашку об обновлении ───────────────
// Вызывается из UpdateChecker::updateAvailable или при ручной проверке.
// Обновляет текст плашки и делает её видимой.
void SettingsDialog::setPendingUpdate(const QString &version, const QString &url,
                                      const QString &notes)
{
    if (version.isEmpty() || url.isEmpty() || !m_updateBanner)
        return;

    m_pendingVersion = version;
    m_pendingUrl     = url;
    m_pendingNotes   = notes;

    // findChild — ищем QLabel с objectName == "bannerText" среди дочерних виджетов
    auto *lbl = m_updateBanner->findChild<QLabel*>("bannerText");
    if (lbl)
        lbl->setText(UpdateChecker::isMajorUpdate(version)
            ? tr("Доступно большое обновление — SmartClip %1").arg(version)
            : tr("Доступно обновление %1 — исправления и улучшения").arg(version));

    m_updateBanner->show();  // Показываем ранее скрытую плашку
}

// ─── showEvent — плавное появление окна настроек ─────────────────────────────
void SettingsDialog::showEvent(QShowEvent *e)
{
    // Размер — по экрану (разделы прокручиваются, окно не должно вылезать
    // за край), и ровно по центру экрана, где открыт SmartClip
    QScreen *scr = parentWidget() ? parentWidget()->screen() : screen();
    if (scr) {
        const QRect av = scr->availableGeometry();
        const QSize sz(qMin(qMax(minimumWidth(), 900), av.width() - 40),
                       qMin(qMax(minimumHeight(), 820), av.height() - 40));
        resize(sz);
        move(av.center() - QPoint(sz.width() / 2, sz.height() / 2));
    }
    QDialog::showEvent(e);
    m_anim->onShow();
}

// ─── done — закрытие (крестик, «Отмена», «Сохранить», Esc) с анимацией ухода ─
void SettingsDialog::done(int r)
{
    if (!m_anim->handleDone([self = QPointer<SettingsDialog>(this), r]() {
            if (self) self->QDialog::done(r);
        }))
        QDialog::done(r);
}

// ─── paintEvent — рисуем скруглённый тёмный фон диалога ─────────────────────
void SettingsDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    // adjusted(1,1,-1,-1) — уменьшаем на 1px чтобы рамка не обрезалась
    path.addRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1), 12, 12);
    p.fillPath(path, QColor(18, 18, 30, 252));          // Тёмно-синий фон
    p.setPen(QPen(QColor(255,255,255,55), 2));           // Полупрозрачная рамка
    p.drawPath(path);
}

// ─── Перетаскивание диалога за тайтл-бар ─────────────────────────────────────
// Та же логика что и в SdDialog::mousePressEvent.

void SettingsDialog::mousePressEvent(QMouseEvent *e)
{
    // Начинаем перетаскивание только если нажали в области тайтл-бара
    if (m_titleBar && m_titleBar->geometry().contains(e->pos()) && e->button() == Qt::LeftButton)
        m_drag = e->globalPosition().toPoint() - frameGeometry().topLeft();
    else
        m_drag = {};
    QDialog::mousePressEvent(e);
}

void SettingsDialog::mouseMoveEvent(QMouseEvent *e)
{
    // !m_drag.isNull() — перетаскивание активно только если нажали в тайтл-баре
    if (!m_drag.isNull() && (e->buttons() & Qt::LeftButton))
        move(e->globalPosition().toPoint() - m_drag);
}

void SettingsDialog::mouseReleaseEvent(QMouseEvent *e)
{
    m_drag = {};  // Сбрасываем — перетаскивание завершено
    QDialog::mouseReleaseEvent(e);
}
