#pragma once

#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <windows.h>

// Поле для захвата горячей клавиши — как биндинг клавиш в играх.
// Клик → ждёт нажатия → отображает комбинацию.
// Ещё один клик → сбрасывает и ждёт снова.
// Escape → отменяет, восстанавливает предыдущее значение.
//
// Для захвата Win+key (Windows перехватывает их до Qt) ставится
// временный WH_KEYBOARD_LL хук на время режима ожидания.
class HotkeyEdit : public QLineEdit
{
    Q_OBJECT

    // ── LL hook для захвата Win+key ───────────────────────────────────────────
    inline static HHOOK       s_hook     = nullptr;
    inline static HotkeyEdit *s_inst     = nullptr;
    inline static bool        s_winDown  = false;

    static LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && s_inst) {
            auto *kb   = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            const UINT vk     = kb->vkCode;
            const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

            // Отслеживаем Win-клавишу
            if (vk == VK_LWIN || vk == VK_RWIN) {
                s_winDown = isDown;
                return CallNextHookEx(s_hook, code, wParam, lParam);
            }

            if (s_winDown) {
                // Win+key: захватываем строку вида "Win+V"
                char ch = 0;
                if (vk >= 'A' && vk <= 'Z') ch = static_cast<char>(vk);
                else if (vk >= '0' && vk <= '9') ch = static_cast<char>(vk);
                // F1-F12
                QString fkey;
                if (vk >= VK_F1 && vk <= VK_F12)
                    fkey = "F" + QString::number(vk - VK_F1 + 1);

                if (ch != 0 || !fkey.isEmpty()) {
                    if (isDown) {
                        QString combo = "Win+" + (ch != 0 ? QString(QChar(ch)) : fkey);
                        HotkeyEdit *inst = s_inst;
                        // Вызов из хука — ставим в очередь через QMetaObject
                        QMetaObject::invokeMethod(inst, [inst, combo]() {
                            inst->setText(combo);
                            inst->doStopCapture(true);
                        }, Qt::QueuedConnection);
                    }
                    // Глотаем и down и up — Windows не открывает Clipboard History
                    return 1;
                }
            }
        }
        return CallNextHookEx(s_hook, code, wParam, lParam);
    }

public:
    explicit HotkeyEdit(QWidget *parent = nullptr)
        : QLineEdit(parent)
    {
        setReadOnly(true);
        setAlignment(Qt::AlignCenter);
        setPlaceholderText(tr("Нажмите сюда, затем введите комбинацию..."));
        applyStyle(false);
    }

    // Вызывается из lambda внутри LL-хука (должен быть публичным)
    void doStopCapture(bool accepted) { stopCapture(accepted); }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) { QLineEdit::mousePressEvent(e); return; }

        if (m_capturing) {
            stopCapture(false);
        } else {
            m_prevValue = text();
            m_capturing = true;
            setText("");
            setPlaceholderText(tr("Нажмите комбинацию... (Esc — отмена)"));
            applyStyle(true);

            // Ставим LL-хук — иначе Win+key перехватывает Windows до Qt
            s_winDown = false;
            s_inst    = this;
            s_hook    = SetWindowsHookEx(WH_KEYBOARD_LL, hookProc, nullptr, 0);
        }
        QLineEdit::mousePressEvent(e);
    }

    void keyPressEvent(QKeyEvent *e) override
    {
        if (!m_capturing) return;

        if (e->key() == Qt::Key_Escape) {
            stopCapture(false);
            return;
        }

        // Одиночные модификаторы — игнорируем
        int key = e->key();
        if (key == Qt::Key_Control || key == Qt::Key_Shift ||
            key == Qt::Key_Alt    || key == Qt::Key_Meta)
            return;

        // Win+key уже обрабатывается LL-хуком выше — сюда не дойдёт
        // Здесь обрабатываем Ctrl/Shift/Alt комбинации

        QString combo;
        Qt::KeyboardModifiers mods = e->modifiers();
        if (mods & Qt::ControlModifier) combo += "Ctrl+";
        if (mods & Qt::ShiftModifier)   combo += "Shift+";
        if (mods & Qt::AltModifier)     combo += "Alt+";
        if (mods & Qt::MetaModifier)    combo += "Win+";

        if (key >= Qt::Key_A && key <= Qt::Key_Z)
            combo += QChar('A' + (key - Qt::Key_A));
        else if (key >= Qt::Key_0 && key <= Qt::Key_9)
            combo += QChar('0' + (key - Qt::Key_0));
        else if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
            combo += "F" + QString::number(key - Qt::Key_F1 + 1);
        else
            return; // неподдерживаемая клавиша — ждём дальше

        setText(combo);
        stopCapture(true);
    }

    void focusOutEvent(QFocusEvent *e) override
    {
        if (m_capturing) stopCapture(false);
        QLineEdit::focusOutEvent(e);
    }

private:
    bool    m_capturing = false;
    QString m_prevValue;

    void stopCapture(bool accepted)
    {
        // Снимаем LL-хук
        if (s_inst == this) {
            if (s_hook) { UnhookWindowsHookEx(s_hook); s_hook = nullptr; }
            s_inst    = nullptr;
            s_winDown = false;
        }

        m_capturing = false;
        if (!accepted)
            setText(m_prevValue);
        setPlaceholderText(tr("Нажмите сюда, затем введите комбинацию..."));
        applyStyle(false);
    }

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
