#pragma once
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  ShellMenu.h — настоящее меню Проводника по правому клику                    ║
// ║                                                                              ║
// ║  Для файла или папки — то же меню, что в Проводнике: «Открыть с помощью»,   ║
// ║  7-Zip, «Отправить», «Удалить», «Свойства»… Сверху — свои пункты SmartClip.  ║
// ║  «Переименовать» Проводник умеет только в своём окне — его отдаём наружу    ║
// ║  (onRename), SmartClip спрашивает новое имя сам.                             ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#include <QString>
#include <QList>
#include <QDir>
#include <functional>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
// Из shlobj.h (его целиком не подключаем: там своя struct FolderItem, как у нас)
extern "C" DECLSPEC_IMPORT HRESULT WINAPI SHParseDisplayName(PCWSTR, IBindCtx *, PIDLIST_ABSOLUTE *, SFGAOF, SFGAOF *);
extern "C" DECLSPEC_IMPORT HRESULT WINAPI SHBindToParent(PCIDLIST_ABSOLUTE, REFIID, void **, PCUITEMID_CHILD *);

namespace ShellMenu {

struct Extra { QString text; std::function<void()> action; };

namespace detail {
// Подменю «Отправить», «Открыть с помощью» рисует само расширение — ему надо
// пересылать сообщения меню. Для этого — своё невидимое окно-хозяин меню.
inline IContextMenu2 *&menu2() { static IContextMenu2 *m = nullptr; return m; }
inline IContextMenu3 *&menu3() { static IContextMenu3 *m = nullptr; return m; }

inline LRESULT CALLBACK ownerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITMENUPOPUP:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_MENUCHAR:
        if (IContextMenu3 *m3 = menu3()) {
            LRESULT r = 0;
            if (SUCCEEDED(m3->HandleMenuMsg2(msg, wp, lp, &r))) return r;
        } else if (IContextMenu2 *m2 = menu2()) {
            if (SUCCEEDED(m2->HandleMenuMsg(msg, wp, lp))) return msg == WM_INITMENUPOPUP ? 0 : TRUE;
        }
        break;
    default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

inline HWND ownerWindow()
{
    static HWND w = nullptr;
    if (w) return w;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = ownerProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SmartClipShellMenuOwner";
    RegisterClassW(&wc);
    // Обычное (не message-only) невидимое окно: меню должно уметь стать активным
    w = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0,
                        nullptr, nullptr, wc.hInstance, nullptr);
    return w;
}
} // namespace detail

// Показать меню у курсора. owner — окно SmartClip (для окон, которые откроют
// команды меню). true — что-то выбрали
inline bool show(HWND owner, const QString &path, const QList<Extra> &extra,
                 const std::function<void(const QString &)> &onRename)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(native.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) return false;
    IShellFolder *parent = nullptr;
    PCUITEMID_CHILD child = nullptr;
    IContextMenu *cm = nullptr;
    if (SUCCEEDED(SHBindToParent(pidl, IID_IShellFolder, reinterpret_cast<void **>(&parent), &child)) && parent)
        parent->GetUIObjectOf(owner, 1, &child, IID_IContextMenu, nullptr, reinterpret_cast<void **>(&cm));
    if (!cm) {
        if (parent) parent->Release();
        CoTaskMemFree(pidl);
        return false;
    }

    // Свои пункты — сверху (номера 1..N), меню Проводника — с номера First
    constexpr UINT First = 1000;
    HMENU menu = CreatePopupMenu();
    UINT pos = 0;
    for (int i = 0; i < extra.size(); ++i)
        InsertMenuW(menu, pos++, MF_BYPOSITION | MF_STRING, UINT(i + 1),
                    reinterpret_cast<LPCWSTR>(extra[i].text.utf16()));
    if (!extra.isEmpty()) InsertMenuW(menu, pos++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    const UINT flags = CMF_NORMAL | CMF_CANRENAME | ((GetKeyState(VK_SHIFT) < 0) ? CMF_EXTENDEDVERBS : 0);
    cm->QueryContextMenu(menu, pos, First, 0x7FFF, flags);

    cm->QueryInterface(IID_IContextMenu3, reinterpret_cast<void **>(&detail::menu3()));
    if (!detail::menu3()) cm->QueryInterface(IID_IContextMenu2, reinterpret_cast<void **>(&detail::menu2()));

    HWND host = detail::ownerWindow();
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(host);  // Иначе меню не закрывается кликом мимо
    const UINT cmd = UINT(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, host, nullptr));
    PostMessageW(host, WM_NULL, 0, 0);

    if (detail::menu3()) { detail::menu3()->Release(); detail::menu3() = nullptr; }
    if (detail::menu2()) { detail::menu2()->Release(); detail::menu2() = nullptr; }

    bool chosen = cmd != 0;
    if (cmd >= 1 && cmd <= UINT(extra.size())) {
        DestroyMenu(menu);
        cm->Release();
        parent->Release();
        CoTaskMemFree(pidl);
        if (extra[int(cmd) - 1].action) extra[int(cmd) - 1].action();
        return true;
    }
    if (cmd >= First) {
        wchar_t verb[64] = {};
        cm->GetCommandString(cmd - First, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), 63);
        if (_wcsicmp(verb, L"rename") == 0) {
            if (onRename) onRename(path);
        } else {
            CMINVOKECOMMANDINFOEX ici = {};
            ici.cbSize = sizeof(ici);
            ici.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE | CMIC_MASK_ASYNCOK;
            if (GetKeyState(VK_CONTROL) < 0) ici.fMask |= CMIC_MASK_CONTROL_DOWN;
            if (GetKeyState(VK_SHIFT) < 0) ici.fMask |= CMIC_MASK_SHIFT_DOWN;
            ici.hwnd = owner;
            ici.lpVerb = MAKEINTRESOURCEA(cmd - First);
            ici.lpVerbW = MAKEINTRESOURCEW(cmd - First);
            ici.nShow = SW_SHOWNORMAL;
            ici.ptInvoke = pt;
            cm->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&ici));
        }
    }
    DestroyMenu(menu);
    cm->Release();
    parent->Release();
    CoTaskMemFree(pidl);
    return chosen;
}

// «Открыть с помощью…» — окно выбора программы Windows
inline void openWith(HWND owner, const QString &path)
{
    const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.hwnd = owner;
    sei.lpVerb = L"openas";
    sei.lpFile = file.c_str();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

// Удалить в корзину (можно вернуть). Слишком большой для корзины файл
// Windows без вопроса не удалит насовсем (FOF_WANTNUKEWARNING)
inline bool recycle(const QString &path)
{
    std::wstring from = QDir::toNativeSeparators(path).toStdWString();
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING | FOF_SILENT;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

// Копировать (или переместить) файлы в папку — как в Проводнике, с корзиной
// для отмены. Одинаковые имена — «файл (2)», без вопросов: окно вопроса
// Windows открылось бы под SmartClip (он поверх всех окон).
// Вызывается в фоновом потоке. true — получилось
inline bool copyTo(const QStringList &paths, const QString &folder, bool move)
{
    std::wstring from;
    for (const QString &p : paths) {
        from += QDir::toNativeSeparators(p).toStdWString();
        from.push_back(L'\0');
    }
    from.push_back(L'\0');
    std::wstring to = QDir::toNativeSeparators(folder).toStdWString();
    to.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = move ? FO_MOVE : FO_COPY;
    op.pFrom = from.c_str();
    op.pTo = to.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_RENAMEONCOLLISION | FOF_NOCONFIRMMKDIR | FOF_NOCONFIRMATION
              | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

} // namespace ShellMenu
