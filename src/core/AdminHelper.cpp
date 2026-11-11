#include "AdminHelper.h"
#include "AdminMode.h"
#include "AppSettings.h"

#include <QTimer>
#include <QCoreApplication>
#include <sddl.h>
#include <atomic>
#include <string>

namespace AdminHelper {

// ═══ Общее ═══════════════════════════════════════════════════════════════════

bool validMessage(const Msg &m)
{
    return m.magic == MAGIC && m.type >= State && m.type <= Quit
        && (m.flags & ~uint32_t(Open | StayOpen | ShiftInsert | DeleteTask)) == 0
        && m.winVk < 256;
}

bool nonTypingKey(UINT vk)
{
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT
        || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL
        || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU
        || vk == VK_LWIN || vk == VK_RWIN || vk == VK_CAPITAL
        // Служебные клавиши — не печать: F1–F24, PrtSc, громкость,
        // медиа, кнопки браузера/почты, NumLock/ScrollLock, Pause
        || (vk >= VK_F1 && vk <= VK_F24) || vk == VK_SNAPSHOT
        || (vk >= VK_BROWSER_BACK && vk <= VK_LAUNCH_APP2)
        || vk == VK_NUMLOCK || vk == VK_SCROLL || vk == VK_PAUSE || vk == VK_SLEEP;
}

bool takesTypingKey(bool open, bool keyDown, UINT vk, bool ctrlAltWinHeld, bool otherMonitor)
{
    return open && keyDown && !nonTypingKey(vk) && !ctrlAltWinHeld && !otherMonitor;
}

bool swallowsOutsideClick(bool stayOpen, bool ownWindow, bool taskbar,
                          bool sameMonitor, bool otherButtonHeld)
{
    return !stayOpen && !ownWindow && !taskbar && sameMonitor && !otherButtonHeld;
}

bool isElevatedWindow(HWND h)
{
    DWORD pid = 0;
    if (!h || !GetWindowThreadProcessId(h, &pid) || pid == GetCurrentProcessId()) return false;
    // Хуки спрашивают на каждое нажатие — запоминаем ответ для последнего процесса
    static thread_local DWORD lastPid = 0;
    static thread_local bool lastResult = false;
    static thread_local ULONGLONG lastAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (pid == lastPid && now - lastAt < 1000) return lastResult;

    bool elevated = true;  // Не открыть процесс — значит, у него права выше наших
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        HANDLE token = nullptr;
        if (OpenProcessToken(p, TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION el = {};
            DWORD size = 0;
            if (GetTokenInformation(token, TokenElevation, &el, sizeof(el), &size))
                elevated = el.TokenIsElevated != 0;
            CloseHandle(token);
        }
        CloseHandle(p);
    }
    lastPid = pid;
    lastResult = elevated;
    lastAt = now;
    return elevated;
}

namespace {

std::wstring pipeName()
{
    return L"\\\\.\\pipe\\SmartClipAdmin-" + AdminMode::currentUserSid();
}

// Полный путь exe процесса (пусто — не удалось узнать)
std::wstring processImage(DWORD pid)
{
    std::wstring result;
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = DWORD(std::size(buf));
        if (QueryFullProcessImageNameW(p, 0, buf, &n)) result.assign(buf, n);
        CloseHandle(p);
    }
    return result;
}

// Тот же SmartClip.exe, что и мы (из той же папки)
bool sameExe(DWORD pid)
{
    const std::wstring other = processImage(pid);
    return !other.empty() && CompareStringOrdinal(other.c_str(), int(other.size()),
        AdminMode::exePath().c_str(), int(AdminMode::exePath().size()), TRUE) == CSTR_EQUAL;
}

DWORD windowPid(HWND h)
{
    DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    return pid;
}

bool isTaskbar(HWND h)
{
    wchar_t cls[64] = {};
    GetClassNameW(GetAncestor(h, GA_ROOT), cls, 64);
    for (const wchar_t *c : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
                             L"NotifyIconOverflowWindow", L"TopLevelWindowForOverflowXamlIsland"})
        if (wcscmp(cls, c) == 0) return true;
    return false;
}

bool held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

void sendKey(WORD vk, bool up)
{
    INPUT in = {};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = vk;
    in.ki.wScan   = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (vk == VK_INSERT || vk == VK_LWIN || vk == VK_RWIN)
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in, sizeof(INPUT));
}

// ═══ Помощник ════════════════════════════════════════════════════════════════

std::atomic<uint32_t> s_flags{0};
std::atomic<uint32_t> s_winVk{0};
std::atomic<HWND>     s_sink{nullptr};
std::atomic<HWND>     s_main{nullptr};
std::atomic<DWORD>    s_mainPid{0};
std::atomic<bool>     s_clientOk{false};
DWORD                 s_hookThreadId = 0;
int                   s_eatenUps = 0;  // Только поток хуков: съеденные нажатия (биты Л/П/С)

void post(UINT msg, WPARAM wp = 0, LPARAM lp = 0)
{
    if (HWND sink = s_sink) PostMessageW(sink, msg, wp, lp);
}

// «Нажимаем» несуществующую клавишу 0x88: Win без пары не откроет Пуск, а
// последним вводом становимся мы — и можем разрешить SmartClip взять фокус
void releaseForegroundToMain()
{
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = 0x88;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    if (DWORD pid = s_mainPid) AllowSetForegroundWindow(pid);
}

// Окно администратора, не SmartClip: о нём SmartClip сам не узнает
bool adminWindow(HWND h)
{
    return h && windowPid(h) != s_mainPid && isElevatedWindow(h);
}

LRESULT CALLBACK keyboardProc(int code, WPARAM wp, LPARAM lp)
{
    const auto *kb = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lp);
    if (code != HC_ACTION || !s_clientOk || (kb->flags & LLKHF_INJECTED))
        return CallNextHookEx(nullptr, code, wp, lp);
    const HWND fg = GetForegroundWindow();
    if (!adminWindow(fg)) return CallNextHookEx(nullptr, code, wp, lp);  // Это SmartClip видит сам

    const UINT vk = kb->vkCode;
    const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
    const bool win = held(VK_LWIN) || held(VK_RWIN);
    // Win+клавиша SmartClip: открыть/закрыть, системе не отдаём (Win+V — её буфер)
    if (const UINT winVk = s_winVk; winVk && vk == winVk && win) {
        if (down) {
            releaseForegroundToMain();
            post(MSG_HOTKEY);
        }
        return 1;
    }
    // SmartClip открыт: печать — в его поиск, а не в окно администратора
    const HWND mainWnd = s_main;
    const bool otherMonitor = mainWnd && MonitorFromWindow(fg, MONITOR_DEFAULTTONULL)
                                         != MonitorFromWindow(mainWnd, MONITOR_DEFAULTTONEAREST);
    if (takesTypingKey(s_flags & Open, down, vk, held(VK_CONTROL) || held(VK_MENU) || win, otherMonitor)) {
        releaseForegroundToMain();
        post(MSG_KEY, vk, LPARAM(kb->scanCode & 0xFFFF) | ((kb->flags & LLKHF_EXTENDED) ? 0x10000 : 0));
        return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK mouseProc(int code, WPARAM wp, LPARAM lp)
{
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wp, lp);
    const int bit = (wp == WM_LBUTTONDOWN || wp == WM_LBUTTONUP) ? 1
                  : (wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP) ? 2
                  : (wp == WM_MBUTTONDOWN || wp == WM_MBUTTONUP) ? 4 : 0;
    const bool down = wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN;
    if (bit && !down && (s_eatenUps & bit)) {  // Отпускание съеденного нажатия
        s_eatenUps &= ~bit;
        return 1;
    }
    if (!bit || !down || !s_clientOk || !(s_flags & Open)) return CallNextHookEx(nullptr, code, wp, lp);
    const POINT pt = reinterpret_cast<const MSLLHOOKSTRUCT *>(lp)->pt;
    const HWND root = GetAncestor(WindowFromPoint(pt), GA_ROOT);
    if (!adminWindow(root)) return CallNextHookEx(nullptr, code, wp, lp);  // Это SmartClip видит сам
    bool otherHeld = false;
    for (int vk : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON})
        if (vk != (bit == 1 ? VK_LBUTTON : bit == 2 ? VK_RBUTTON : VK_MBUTTON) && held(vk)) otherHeld = true;
    const HWND mainWnd = s_main;
    const bool sameMonitor = mainWnd && MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)
                                        == MonitorFromWindow(mainWnd, MONITOR_DEFAULTTONEAREST);
    if (!swallowsOutsideClick(s_flags & StayOpen, false, isTaskbar(root), sameMonitor, otherHeld))
        return CallNextHookEx(nullptr, code, wp, lp);
    s_eatenUps |= bit;
    post(MSG_OUTSIDE);
    return 1;
}

// Ctrl+V / Shift+Insert «как рукой» — как sendPaste в MainWindow.cpp
void pasteNow(bool shiftInsert)
{
    const WORD mod = shiftInsert ? VK_SHIFT : VK_CONTROL;
    const WORD key = shiftInsert ? VK_INSERT : 'V';
    sendKey(mod, false);
    for (WORD vk : {WORD(VK_CONTROL), WORD(VK_SHIFT), WORD(VK_MENU), WORD(VK_LWIN), WORD(VK_RWIN)})
        if (vk != mod && held(vk)) sendKey(vk, true);
    Sleep(25);
    sendKey(key, false);
    Sleep(45);
    sendKey(key, true);
    Sleep(25);
    sendKey(mod, true);
}

void handle(const Msg &m, DWORD clientPid)
{
    switch (m.type) {
    case State: {
        // Окна для ответов — только окна самого SmartClip
        const HWND sink = reinterpret_cast<HWND>(m.sink), mainWnd = reinterpret_cast<HWND>(m.main);
        if (windowPid(sink) != clientPid || (mainWnd && windowPid(mainWnd) != clientPid)) return;
        s_sink = sink;
        s_main = mainWnd;
        s_mainPid = clientPid;
        s_winVk = m.winVk;
        s_flags = m.flags & (Open | StayOpen);
        s_clientOk = true;
        break;
    }
    case Paste: {
        // Только в активное окно администратора и не чаще 5 раз в секунду
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - last < 200 || !adminWindow(GetForegroundWindow())) return;
        last = now;
        pasteNow(m.flags & ShiftInsert);
        break;
    }
    case Quit:
        if (m.flags & DeleteTask) AdminMode::deleteTask();
        PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
        break;
    }
}

// Канал: ждём SmartClip, читаем его сообщения. Закрылся — помощник выходит.
DWORD WINAPI pipeThread(LPVOID pipePtr)
{
    HANDLE pipe = pipePtr;
    for (;;) {
        if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) break;
        ULONG clientPid = 0;
        // Подключиться может любая программа этого пользователя — слушаем только SmartClip
        if (!GetNamedPipeClientProcessId(pipe, &clientPid) || !sameExe(clientPid)) {
            DisconnectNamedPipe(pipe);
            continue;
        }
        Msg m;
        DWORD read = 0;
        while (ReadFile(pipe, &m, sizeof(m), &read, nullptr) && read == sizeof(m))
            if (validMessage(m)) handle(m, clientPid);
        break;  // SmartClip вышел (или прислал мусор)
    }
    PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
    return 0;
}

} // namespace

int runServer()
{
    if (!AdminMode::isElevated()) return 1;
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\SmartClipAdminHelper");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // Помощник уже работает

    // Канал: только этот пользователь; метка «средний уровень» — иначе обычный
    // SmartClip не смог бы писать в канал процесса с правами администратора
    const std::wstring sddl = L"D:P(A;;GA;;;" + AdminMode::currentUserSid() + L")S:(ML;;NW;;;ME)";
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &sa.lpSecurityDescriptor, nullptr))
        return 1;
    HANDLE pipe = CreateNamedPipeW(pipeName().c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                   1, 0, sizeof(Msg) * 8, 0, &sa);
    LocalFree(sa.lpSecurityDescriptor);
    if (pipe == INVALID_HANDLE_VALUE) return 1;

    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);  // Очередь — до запуска канала
    s_hookThreadId = GetCurrentThreadId();
    HHOOK kb    = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
    if (HANDLE t = CreateThread(nullptr, 0, pipeThread, pipe, 0, nullptr)) CloseHandle(t);
    // SmartClip так и не пришёл за 30 с — не висим зря
    SetTimer(nullptr, 0, 30000, nullptr);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_TIMER && !s_clientOk) break;
        DispatchMessageW(&msg);
    }
    if (kb)    UnhookWindowsHookEx(kb);
    if (mouse) UnhookWindowsHookEx(mouse);
    if (single) CloseHandle(single);
    ExitProcess(0);  // Поток канала может ещё ждать в ConnectNamedPipe
}

// ═══ SmartClip ═══════════════════════════════════════════════════════════════

namespace {

Callbacks          s_cb;
HANDLE             s_pipe = INVALID_HANDLE_VALUE;
std::atomic<bool>  s_connected{false};
HWND               s_sinkWnd = nullptr;
Msg                s_state;            // Последнее состояние для помощника
bool               s_quitPending = false;
uint32_t           s_quitFlags = 0;
QTimer            *s_retry = nullptr;
int                s_tries = 0;

LRESULT CALLBACK sinkProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case MSG_HOTKEY:  if (s_cb.hotkey) s_cb.hotkey(); return 0;
    case MSG_KEY:     if (s_cb.key) s_cb.key(unsigned(wp), unsigned(lp & 0xFFFF), (lp & 0x10000) != 0); return 0;
    case MSG_OUTSIDE: if (s_cb.outsideClick) s_cb.outsideClick(); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND sinkWindow()
{
    if (!s_sinkWnd) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = sinkProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"SmartClipAdminSink";
        RegisterClassW(&wc);
        s_sinkWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                    nullptr, wc.hInstance, nullptr);
    }
    return s_sinkWnd;
}

void disconnect()
{
    if (s_pipe != INVALID_HANDLE_VALUE) CloseHandle(s_pipe);
    s_pipe = INVALID_HANDLE_VALUE;
    s_connected = false;
}

bool write(const Msg &m)
{
    DWORD written = 0;
    if (s_pipe != INVALID_HANDLE_VALUE && WriteFile(s_pipe, &m, sizeof(m), &written, nullptr)
        && written == sizeof(m))
        return true;
    // Помощник пропал — запускаем заново (если режим ещё включён)
    disconnect();
    if (AppSettings::get().runAsAdmin()) QTimer::singleShot(2000, qApp, []() { ensureRunning(); });
    return false;
}

bool tryConnect()
{
    HANDLE pipe = CreateFileW(pipeName().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;
    // На том конце должен быть наш помощник (тот же exe, с правами администратора)
    ULONG serverPid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &serverPid) || !sameExe(serverPid)) {
        CloseHandle(pipe);
        return false;
    }
    s_pipe = pipe;
    s_connected = true;
    if (s_quitPending) {
        Msg q;
        q.type = Quit;
        q.flags = s_quitFlags;
        write(q);
        disconnect();
        s_quitPending = false;
        return true;
    }
    s_state.sink = reinterpret_cast<uint64_t>(sinkWindow());
    write(s_state);
    return true;
}

void sendState()
{
    s_state.type = State;
    s_state.sink = reinterpret_cast<uint64_t>(sinkWindow());
    if (s_connected) write(s_state);
}

} // namespace

void setCallbacks(const Callbacks &cb) { s_cb = cb; }

bool connected() { return s_connected; }

void ensureRunning()
{
    if (s_connected) return;
    if (tryConnect()) return;
    AdminMode::runTask();
    // Помощник стартует за доли секунды — пробуем подключиться ещё 10 с
    if (!s_retry) {
        s_retry = new QTimer(qApp);
        s_retry->setInterval(250);
        QObject::connect(s_retry, &QTimer::timeout, []() {
            if (tryConnect() || ++s_tries >= 40) s_retry->stop();
        });
    }
    s_tries = 0;
    s_retry->start();
}

void stop(bool deleteTask)
{
    if (s_retry) s_retry->stop();
    const uint32_t flags = deleteTask ? uint32_t(DeleteTask) : 0;
    if (s_connected) {
        Msg q;
        q.type = Quit;
        q.flags = flags;
        write(q);
        disconnect();
    } else if (deleteTask) {
        // Задачу удалить может только помощник (права администратора): запускаем его на миг
        s_quitPending = true;
        s_quitFlags = flags;
        ensureRunning();
    }
}

void setOpen(bool open, bool stayOpen, HWND mainWindow)
{
    const uint32_t flags = (open ? uint32_t(Open) : 0) | (stayOpen ? uint32_t(StayOpen) : 0);
    const uint64_t mainWnd = reinterpret_cast<uint64_t>(mainWindow);
    if (flags == s_state.flags && mainWnd == s_state.main) return;
    s_state.flags = flags;
    s_state.main = mainWnd;
    sendState();
}

void setWinHotkey(UINT vk)
{
    if (vk == s_state.winVk) return;
    s_state.winVk = vk;
    sendState();
}

bool paste(bool shiftInsert)
{
    if (!s_connected) return false;
    Msg p;
    p.type = Paste;
    p.flags = shiftInsert ? uint32_t(ShiftInsert) : 0;
    return write(p);
}

} // namespace AdminHelper
