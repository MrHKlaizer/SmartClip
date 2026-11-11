// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  AdminHelper.h — помощник для окон администратора                         ║
// ║                                                                              ║
// ║  Windows не пускает обычные программы в окна, открытые от администратора   ║
// ║  (защита UIPI): туда не дойдёт вставка, а хуки SmartClip не видят клавиш   ║
// ║  и кликов, пока такое окно активно.                                         ║
// ║                                                                              ║
// ║  Раньше SmartClip целиком запускался с правами администратора — и всё, что  ║
// ║  он открывал (файлы из загрузок, меню Проводника), молча получало те же    ║
// ║  права. Теперь SmartClip всегда обычная программа, а права админа — только ║
// ║  у этого помощника (тот же SmartClip.exe с --admin-helper, запуск через    ║
// ║  задачу Планировщика, см. AdminMode.h). Помощник ничего не открывает и не  ║
// ║  читает файлов, он умеет только:                                            ║
// ║   • вставить (Ctrl+V / Shift+Insert) в активное окно администратора;       ║
// ║   • когда активно окно администратора — поймать Win+клавишу SmartClip,     ║
// ║     печать в поиск открытого SmartClip и клик мимо него, и передать их     ║
// ║     SmartClip.                                                              ║
// ║                                                                              ║
// ║  Связь: SmartClip → помощник — именованный канал (только этот              ║
// ║  пользователь, только SmartClip.exe из той же папки); помощник →            ║
// ║  SmartClip — PostMessage в скрытое окно. Канал закрылся (SmartClip         ║
// ║  вышел) — помощник завершается.                                             ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
#pragma once

#include <windows.h>
#include <cstdint>
#include <functional>

namespace AdminHelper {

// ─── Сообщение SmartClip → помощник (фиксированного размера) ─────────────────
enum Type : uint32_t { State = 1, Paste = 2, Quit = 3 };
enum Flags : uint32_t {
    Open        = 1,  // SmartClip на экране: печать — в его поиск, клик мимо — закрыть
    StayOpen    = 2,  // «Не закрывать SmartClip самому»
    ShiftInsert = 4,  // Paste: вставлять Shift+Insert (терминалы), а не Ctrl+V
    DeleteTask  = 8,  // Quit: удалить и задачу Планировщика (режим выключили)
};
constexpr uint32_t MAGIC = 0x48414353;  // "SCAH"
struct Msg {
    uint32_t magic = MAGIC;
    uint32_t type  = 0;
    uint32_t flags = 0;
    uint32_t winVk = 0;   // Клавиша Win+… главного хоткея (0 — хоткей без Win)
    uint64_t sink  = 0;   // Скрытое окно SmartClip для ответов
    uint64_t main  = 0;   // Главное окно SmartClip (его монитор, его процесс)
};
// Сообщение целое и понятное (всё прочее помощник отбрасывает)
bool validMessage(const Msg &m);

// ─── Ответы помощника (PostMessage в скрытое окно SmartClip) ─────────────────
constexpr UINT MSG_HOTKEY  = WM_APP + 0x51;  // Нажат Win+клавиша SmartClip
constexpr UINT MSG_KEY     = WM_APP + 0x52;  // Клавиша для поиска: wParam=vk, lParam=скан | расширенная<<16
constexpr UINT MSG_OUTSIDE = WM_APP + 0x53;  // Клик мимо SmartClip съеден — закрыться

// Окно принадлежит процессу с правами администратора (или процессу, о котором
// мы ничего не можем узнать — тогда считаем, что с правами выше наших)
bool isElevatedWindow(HWND h);
// Не печать: модификаторы, F1–F24, PrtSc, медиа-клавиши и т.п.
bool nonTypingKey(UINT vk);
// Клавиша, пока SmartClip открыт, уходит в его поиск (а не в программу под ним).
// Не трогаем модификаторы, сочетания с Ctrl/Alt/Win и работу на другом мониторе.
bool takesTypingKey(bool open, bool keyDown, UINT vk, bool ctrlAltWinHeld, bool otherMonitor);
// Клик мимо SmartClip только закрывает его и дальше не проходит. Не трогаем:
// свои окна, панель задач (трей), другой монитор, настройку «не закрываться
// самому» и клики, пока зажата другая кнопка (перенос файлов).
bool swallowsOutsideClick(bool stayOpen, bool ownWindow, bool taskbar,
                          bool sameMonitor, bool otherButtonHeld);

// ─── Помощник (процесс с правами администратора) ─────────────────────────────
int runServer();  // Из main() при --admin-helper; возвращает код выхода

// ─── SmartClip (обычный пользователь) ───────────────────────────────────────
struct Callbacks {
    std::function<void()> hotkey;                                   // MSG_HOTKEY
    std::function<void(unsigned vk, unsigned scan, bool ext)> key;  // MSG_KEY
    std::function<void()> outsideClick;                             // MSG_OUTSIDE
};
void setCallbacks(const Callbacks &cb);
void ensureRunning();          // Запустить помощника (задача) и подключиться к нему
void stop(bool deleteTask);    // Выключить режим: помощник выходит (и удаляет задачу)
bool connected();
// Состояние SmartClip — помощник действует по нему (шлётся только при изменении)
void setOpen(bool open, bool stayOpen, HWND mainWindow);
void setWinHotkey(UINT vk);
// Вставить в активное окно администратора. false — помощника нет.
bool paste(bool shiftInsert);

} // namespace AdminHelper
