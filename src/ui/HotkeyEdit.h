// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  HotkeyEdit.h — Поле захвата горячей клавиши                               ║
// ║                                                                              ║
// ║  Работает как биндинг клавиш в играх:                                       ║
// ║  1. Пользователь кликает на поле — оно переходит в режим ожидания          ║
// ║  2. Нажимает любую комбинацию (Ctrl+Shift+V, Win+C и т.д.)                ║
// ║  3. Поле показывает захваченную комбинацию и выходит из режима ожидания    ║
// ║  4. Нажатие Escape или повторный клик — отмена, возврат прежнего значения  ║
// ║                                                                              ║
// ║  Проблема с Win+key:                                                         ║
// ║  Windows перехватывает Win+C, Win+X и т.д. ДО того как Qt получает их.    ║
// ║  Решение: на время режима ожидания ставим низкоуровневый хук WH_KEYBOARD_LL║
// ║  который перехватывает клавиши на системном уровне — раньше Windows.       ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

// QLineEdit — однострочное текстовое поле Qt. HotkeyEdit наследует его.
#include <QLineEdit>
// QKeyEvent, QMouseEvent, QFocusEvent — события клавиатуры, мыши и фокуса
#include <QKeyEvent>
#include <QMouseEvent>
// windows.h — нужен для HHOOK, KBDLLHOOKSTRUCT и функций хуков
#include <windows.h>

// ─── Класс HotkeyEdit ────────────────────────────────────────────────────────
// Текстовое поле только для чтения. Пользователь не вводит текст — он нажимает
// клавиши, а поле само записывает комбинацию в формате "Ctrl+Shift+V".
class HotkeyEdit : public QLineEdit
{
    Q_OBJECT

    // ─── Низкоуровневый хук клавиатуры (для Win+key) ─────────────────────────
    // inline static — один экземпляр на весь класс (не на каждый объект).
    // Хук должен быть статическим потому что Windows вызывает его как C-функцию.
    inline static HHOOK       s_hook    = nullptr;  // Дескриптор установленного хука
    inline static HotkeyEdit *s_inst    = nullptr;  // Текущий активный экземпляр HotkeyEdit
    inline static bool        s_winDown = false;    // true пока зажата Win-клавиша

    // Функция-хук: вызывается Windows при каждом нажатии клавиши в системе.
    // Срабатывает РАНЬШЕ чем Windows обработает Win+key.
    // code, wParam, lParam — стандартные параметры Windows хука.
    static LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        // HC_ACTION = Windows передал нам реальное нажатие (не служебное)
        if (code == HC_ACTION && s_inst) {
            // Приводим lParam к структуре с информацией о нажатой клавише
            auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            const UINT vk     = kb->vkCode;  // Виртуальный код клавиши ('A' = 0x41)
            const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

            // Отслеживаем состояние Win-клавиши (левой или правой)
            if (vk == VK_LWIN || vk == VK_RWIN) {
                s_winDown = isDown;
                // Передаём Windows — пусть обрабатывает (только для Win без других клавиш)
                return CallNextHookEx(s_hook, code, wParam, lParam);
            }

            // Если зажата Win-клавиша — пытаемся захватить Win+key комбинацию
            if (s_winDown) {
                char ch = 0;
                // A-Z: виртуальные коды совпадают с ASCII
                if (vk >= 'A' && vk <= 'Z') ch = static_cast<char>(vk);
                // 0-9: тоже совпадают с ASCII
                else if (vk >= '0' && vk <= '9') ch = static_cast<char>(vk);

                // F1-F12: VK_F1=0x70, формируем строку "F1", "F2", ...
                QString fkey;
                if (vk >= VK_F1 && vk <= VK_F12)
                    fkey = "F" + QString::number(vk - VK_F1 + 1);

                if (ch != 0 || !fkey.isEmpty()) {
                    if (isDown) {
                        // Формируем строку "Win+C" или "Win+F5"
                        QString combo = "Win+" + (ch != 0 ? QString(QChar(ch)) : fkey);
                        HotkeyEdit *inst = s_inst;
                        // Вызов из хука идёт не из Qt-потока — используем QMetaObject::invokeMethod
                        // чтобы безопасно вызвать метод в Qt-потоке (Qt::QueuedConnection = позже).
                        QMetaObject::invokeMethod(inst, [inst, combo]() {
                            inst->setText(combo);
                            inst->doStopCapture(true);  // принять комбинацию
                        }, Qt::QueuedConnection);
                    }
                    // Возвращаем 1 (не 0) — «съедаем» событие.
                    // Windows НЕ получит Win+C и НЕ откроет историю буфера обмена.
                    return 1;
                }
            }
        }
        // Для всех остальных клавиш — передаём Windows дальше.
        return CallNextHookEx(s_hook, code, wParam, lParam);
    }

public:
    // Конструктор — настраивает вид поля.
    explicit HotkeyEdit(QWidget *parent = nullptr)
        : QLineEdit(parent)
    {
        setReadOnly(true);   // Нельзя вводить текст вручную — только через нажатие клавиш
        setAlignment(Qt::AlignCenter);
        setPlaceholderText(tr("Нажмите сюда, затем введите комбинацию..."));
        applyStyle(false);   // Обычный стиль (не в режиме ожидания)
    }

    // Публичный метод-обёртка для вызова из лямбды внутри LL-хука.
    // Прямой вызов stopCapture() был бы private — недоступен.
    void doStopCapture(bool accepted) { stopCapture(accepted); }

protected:
    // Клик мышью — переключаем режим захвата.
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) { QLineEdit::mousePressEvent(e); return; }

        if (m_capturing) {
            // Повторный клик во время ожидания — отменяем (восстанавливаем предыдущее значение)
            stopCapture(false);
        } else {
            // Начинаем режим захвата
            m_prevValue = text();   // Запоминаем текущее значение для отмены
            m_capturing = true;
            setText("");
            setPlaceholderText(tr("Нажмите комбинацию... (Esc — отмена)"));
            applyStyle(true);  // Зелёный стиль — «жду нажатия»

            // Устанавливаем низкоуровневый хук для Win+key.
            // WH_KEYBOARD_LL = перехватываем ВСЕ клавиши до Windows.
            // nullptr, 0 — хук глобальный (для всей системы, не только нашего потока).
            s_winDown = false;
            s_inst    = this;
            s_hook    = SetWindowsHookEx(WH_KEYBOARD_LL, hookProc, nullptr, 0);
        }
        QLineEdit::mousePressEvent(e);
    }

    // Нажатие клавиши — обрабатываем Ctrl/Shift/Alt комбинации (не Win+key).
    void keyPressEvent(QKeyEvent *e) override
    {
        if (!m_capturing) return;  // Не в режиме захвата — игнорируем

        // Escape — отменить, восстановить предыдущее значение
        if (e->key() == Qt::Key_Escape) {
            stopCapture(false);
            return;
        }

        // Одиночные модификаторы (Ctrl, Shift, Alt, Win сами по себе) — ждём дальше
        int key = e->key();
        if (key == Qt::Key_Control || key == Qt::Key_Shift ||
            key == Qt::Key_Alt    || key == Qt::Key_Meta)
            return;

        // Win+key перехватывается хуком выше — сюда такие комбинации не доходят.
        // Здесь обрабатываем Ctrl+X, Shift+F5, Alt+Ctrl+V и т.д.

        // Собираем строку модификаторов: "Ctrl+", "Ctrl+Shift+", и т.д.
        QString combo;
        Qt::KeyboardModifiers mods = e->modifiers();
        if (mods & Qt::ControlModifier) combo += "Ctrl+";
        if (mods & Qt::ShiftModifier)   combo += "Shift+";
        if (mods & Qt::AltModifier)     combo += "Alt+";
        if (mods & Qt::MetaModifier)    combo += "Win+";

        // Добавляем основную клавишу
        if (key >= Qt::Key_A && key <= Qt::Key_Z)
            combo += QChar('A' + (key - Qt::Key_A));       // A-Z
        else if (key >= Qt::Key_0 && key <= Qt::Key_9)
            combo += QChar('0' + (key - Qt::Key_0));       // 0-9
        else if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
            combo += "F" + QString::number(key - Qt::Key_F1 + 1);  // F1-F12
        else
            return;  // Неподдерживаемая клавиша — продолжаем ждать

        setText(combo);
        stopCapture(true);  // Принять комбинацию
    }

    // Потеря фокуса — автоматически отменяем захват (не оставляем поле «зависшим»).
    void focusOutEvent(QFocusEvent *e) override
    {
        if (m_capturing) stopCapture(false);
        QLineEdit::focusOutEvent(e);
    }

private:
    bool    m_capturing = false;  // true = ждём нажатия клавиши
    QString m_prevValue;          // Значение до начала захвата (для отмены)

    // Завершить режим захвата.
    // accepted = true  → принять новое значение (setText уже вызван)
    // accepted = false → восстановить m_prevValue
    void stopCapture(bool accepted)
    {
        // Снимаем низкоуровневый хук (важно! иначе он висит в системе)
        if (s_inst == this) {
            if (s_hook) { UnhookWindowsHookEx(s_hook); s_hook = nullptr; }
            s_inst    = nullptr;
            s_winDown = false;
        }

        m_capturing = false;
        if (!accepted)
            setText(m_prevValue);  // Откатываем к предыдущему значению
        setPlaceholderText(tr("Нажмите сюда, затем введите комбинацию..."));
        applyStyle(false);  // Возвращаем обычный серый стиль
    }

    // Переключить визуальный стиль поля.
    // capturing = true  → зелёный (режим ожидания нажатия)
    // capturing = false → серый (обычный)
    void applyStyle(bool capturing)
    {
        if (capturing) {
            setStyleSheet(
                "background: #1a2a1a; color: #88ff88; border: 1px solid #44aa44;"
                "border-radius: 5px; padding: 5px 8px; font-weight: bold;"
            );
        } else {
            setStyleSheet(
                "background: #1e1e1e; color: #ddd; border: 1px solid #555;"
                "border-radius: 5px; padding: 5px 8px;"
            );
        }
    }
};
