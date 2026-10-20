/*
 *  SmartClip Installer / Uninstaller
 *  ──────────────────────────────────
 *  Запуск:  SmartClip_Installer.exe            → установка
 *           SmartClip_Installer.exe /update    → тихое обновление (из самого SmartClip)
 *           Uninstaller.exe /uninstall         → удаление
 *
 *  Файлы программы лежат ВНУТРИ exe (см. Payload.h) — пользователю
 *  достаточно скачать один файл. Старый формат (папка _data\ рядом с
 *  Installer.exe) тоже поддерживается.
 *
 *  Требования: Windows 10+, права администратора (UAC-манифест)
 *  Зависимости: только WinAPI (comctl32, shell32, shlwapi, advapi32)
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <compressapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <cstring>
#include "Payload.h"

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════
//  Константы
// ═══════════════════════════════════════════════════════════════

static const wchar_t* APP_NAME        = L"SmartClip";
// Версия приходит из CMakeLists.txt (project VERSION) — руками не менять
static const wchar_t* APP_VERSION     = L"" SMARTCLIP_VERSION;
static const wchar_t* APP_EXE         = L"SmartClip.exe";
static const wchar_t* INSTALLER_EXE   = L"Installer.exe";
static const wchar_t* UNINSTALLER_EXE = L"Uninstaller.exe";

static const wchar_t* REG_RUN_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static const wchar_t* REG_UNINSTALL_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SmartClip";

static const wchar_t* REG_EXPLORER_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer";

// ═══════════════════════════════════════════════════════════════
//  Пути
// ═══════════════════════════════════════════════════════════════

static std::wstring getExePath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

static std::wstring getExeDir() {
    return fs::path(getExePath()).parent_path().wstring();
}

// Папка с файлами для установки — _data\ рядом с Installer.exe
static std::wstring getDataDir() {
    return getExeDir() + L"\\_data";
}

static std::wstring getInstallDir() {
    wchar_t pf[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, pf))
        || pf[0] == L'\0')
        wcscpy_s(pf, L"C:\\Program Files");  // Без этого путь мог стать "\SmartClip"
    return std::wstring(pf) + L"\\" + APP_NAME;
}

// ═══════════════════════════════════════════════════════════════
//  UI-хелперы
// ═══════════════════════════════════════════════════════════════

static bool confirm(const std::wstring& text) {
    return MessageBoxW(nullptr, text.c_str(), APP_NAME,
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1) == IDYES;
}

static void info(const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), APP_NAME, MB_OK | MB_ICONINFORMATION);
}

static void errMsg(const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), APP_NAME, MB_OK | MB_ICONERROR);
}

// ═══════════════════════════════════════════════════════════════
//  Выбор языка (TaskDialog с кнопками-командами)
// ═══════════════════════════════════════════════════════════════

static std::wstring chooseLanguage()
{
    TASKDIALOG_BUTTON buttons[] = {
        { 200, L"English"     },
        { 201, L"Русский"     },
        { 202, L"Українська"  },
        { 203, L"Deutsch"     },
    };

    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize            = sizeof(cfg);
    cfg.dwFlags           = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    cfg.pszWindowTitle    = APP_NAME;
    cfg.pszMainInstruction = L"Select language / Выберите язык";
    cfg.pszContent        = L"Choose the interface language for SmartClip:";
    cfg.pButtons          = buttons;
    cfg.cButtons          = 4;
    cfg.nDefaultButton    = 200; // English по умолчанию

    int clicked = 200;
    if (FAILED(TaskDialogIndirect(&cfg, &clicked, nullptr, nullptr)))
        return L"en";
    // Закрыли окно крестиком — это отмена установки, а не «English»
    if (clicked == IDCANCEL) return L"";

    switch (clicked) {
        case 201: return L"ru";
        case 202: return L"ua";
        case 203: return L"de";
        default:  return L"en";
    }
}

// ═══════════════════════════════════════════════════════════════
//  Чтение / запись языка в INI SmartClip
//  Путь: %APPDATA%\SmartClipApp\SmartClip.ini
// ═══════════════════════════════════════════════════════════════

static std::wstring getIniPath()
{
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData);
    return std::wstring(appData) + L"\\SmartClipApp\\SmartClip.ini";
}

static void writeLanguageToIni(const std::wstring& langCode)
{
    std::wstring iniPath = getIniPath();
    std::wstring iniDir  = iniPath.substr(0, iniPath.rfind(L'\\'));
    CreateDirectoryW(iniDir.c_str(), nullptr);
    WritePrivateProfileStringW(L"system", L"language",
                               langCode.c_str(), iniPath.c_str());
}

static std::wstring readLanguageFromIni()
{
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"system", L"language", L"en",
                             buf, 64, getIniPath().c_str());
    return buf;
}

// ═══════════════════════════════════════════════════════════════
//  Локализованные строки деинсталлятора
// ═══════════════════════════════════════════════════════════════

struct UninstStrings {
    const wchar_t* notFound;
    const wchar_t* confirmTitle;
    const wchar_t* confirm;
    const wchar_t* restartExplorer;
    const wchar_t* done;
};

static const UninstStrings& getUninstStrings()
{
    static UninstStrings s;
    std::wstring lang = readLanguageFromIni();

    if (lang == L"ru") {
        s.notFound        = L"SmartClip не найден.\nВозможно, программа уже была удалена.";
        s.confirm         = L"Удалить SmartClip?\n\nБудут удалены:\n  • Все файлы программы\n  • Запись об автозапуске\n  • Восстановлен системный Win+V\n\nВаши данные (история, закрепы) сохранятся.";
        s.restartExplorer = L"SmartClip удалён.\n\nДля восстановления Win+V нужно перезапустить Проводник.\n\nПерезапустить сейчас?";
    } else if (lang == L"ua") {
        s.notFound        = L"SmartClip не знайдено.\nМожливо, програму вже було видалено.";
        s.confirm         = L"Видалити SmartClip?\n\nБуде видалено:\n  • Усі файли програми\n  • Запис автозапуску\n  • Відновлено системний Win+V\n\nВаші дані (історія, закріплені) збережуться.";
        s.restartExplorer = L"SmartClip видалено.\n\nДля відновлення Win+V потрібно перезапустити Провідник.\n\nПерезапустити зараз?";
    } else if (lang == L"de") {
        s.notFound        = L"SmartClip wurde nicht gefunden.\nMöglicherweise wurde es bereits deinstalliert.";
        s.confirm         = L"SmartClip deinstallieren?\n\nFolgendes wird entfernt:\n  • Alle Programmdateien\n  • Autostart-Eintrag\n  • Win+V wird wiederhergestellt\n\nIhre Daten (Verlauf, Pins) bleiben erhalten.";
        s.restartExplorer = L"SmartClip wurde deinstalliert.\n\nUm Win+V wiederherzustellen, muss der Explorer neu gestartet werden.\n\nJetzt neu starten?";
    } else { // en (default)
        s.notFound        = L"SmartClip was not found.\nIt may have already been uninstalled.";
        s.confirm         = L"Uninstall SmartClip?\n\nThe following will be removed:\n  • All program files\n  • Startup registry entry\n  • Win+V will be restored\n\nYour data (history, pins) will NOT be affected.";
        s.restartExplorer = L"SmartClip has been uninstalled.\n\nTo restore Win+V, Explorer needs to be restarted.\n\nRestart now?";
    }
    return s;
}

// ═══════════════════════════════════════════════════════════════
//  Прогресс-окно (marquee, без кнопок)
// ═══════════════════════════════════════════════════════════════

static HWND g_progressHwnd  = nullptr;
static HWND g_progressLabel = nullptr;
static HWND g_progressBar   = nullptr;

static LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) return 0;   // нельзя закрыть
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void createProgressWindow(const wchar_t* label) {
    const int W = 400, H = 110;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ProgressWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SmartClipInstProgress";
    wc.hCursor       = LoadCursor(nullptr, IDC_WAIT);
    wc.hIcon         = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    g_progressHwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"SmartClipInstProgress",
        APP_NAME,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (sx - W) / 2, (sy - H) / 2, W, H,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    // Статичная надпись
    g_progressLabel = CreateWindowExW(0, L"STATIC", label,
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 18, W - 36, 22, g_progressHwnd, nullptr,
        GetModuleHandleW(nullptr), nullptr);

    // Прогресс-бар в режиме marquee (бегущий)
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    g_progressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | PBS_MARQUEE | PBS_SMOOTH,
        10, 55, W - 36, 18, g_progressHwnd, nullptr,
        GetModuleHandleW(nullptr), nullptr);

    SendMessageW(g_progressBar, PBM_SETMARQUEE, TRUE, 40);

    ShowWindow(g_progressHwnd, SW_SHOW);
    UpdateWindow(g_progressHwnd);
}

static void destroyProgressWindow() {
    if (g_progressHwnd) {
        DestroyWindow(g_progressHwnd);
        g_progressHwnd = nullptr;
    }
}

static void pumpMessages() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

// ═══════════════════════════════════════════════════════════════
//  Реестр
// ═══════════════════════════════════════════════════════════════

static void regWriteStr(HKEY root, const wchar_t* subkey,
                        const wchar_t* name, const wchar_t* value) {
    HKEY hk;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        DWORD size = (DWORD)((wcslen(value) + 1) * sizeof(wchar_t));
        RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)value, size);
        RegCloseKey(hk);
    }
}

static std::wstring regReadStr(HKEY root, const wchar_t* subkey, const wchar_t* name) {
    std::wstring result;
    HKEY hk;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        wchar_t buf[1024] = {};
        DWORD size = sizeof(buf), type = REG_SZ;
        if (RegQueryValueExW(hk, name, nullptr, &type, (BYTE*)buf, &size) == ERROR_SUCCESS)
            result = buf;
        RegCloseKey(hk);
    }
    return result;
}

static void regDeleteVal(HKEY root, const wchar_t* subkey, const wchar_t* name) {
    HKEY hk;
    if (RegOpenKeyExW(root, subkey, 0, KEY_WRITE, &hk) == ERROR_SUCCESS) {
        RegDeleteValueW(hk, name);
        RegCloseKey(hk);
    }
}

// ═══════════════════════════════════════════════════════════════
//  Win+V (DisabledHotkeys)
// ═══════════════════════════════════════════════════════════════

// Возвращает true, если значение реально поменялось (тогда нужен перезапуск Проводника)
static bool disableWinV() {
    std::wstring val = regReadStr(HKEY_CURRENT_USER, REG_EXPLORER_KEY, L"DisabledHotkeys");
    if (val.find(L'V') == std::wstring::npos) {
        val += L'V';
        regWriteStr(HKEY_CURRENT_USER, REG_EXPLORER_KEY, L"DisabledHotkeys", val.c_str());
        return true;
    }
    return false;
}

static void enableWinV() {
    std::wstring val = regReadStr(HKEY_CURRENT_USER, REG_EXPLORER_KEY, L"DisabledHotkeys");
    val.erase(std::remove(val.begin(), val.end(), L'V'), val.end());
    if (val.empty())
        regDeleteVal(HKEY_CURRENT_USER, REG_EXPLORER_KEY, L"DisabledHotkeys");
    else
        regWriteStr(HKEY_CURRENT_USER, REG_EXPLORER_KEY, L"DisabledHotkeys", val.c_str());
}

// ═══════════════════════════════════════════════════════════════
//  Процессы
// ═══════════════════════════════════════════════════════════════

static void killProcessByName(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                                           pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    // Ждём реального завершения процесса (раньше был Sleep(600) —
                    // на медленном ПК файлы ещё были заняты, и копирование падало)
                    WaitForSingleObject(hProc, 5000);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    Sleep(200);  // Даём Windows отпустить хэндлы файлов
}

static void restartExplorer() {
    // Посылаем WM_QUIT в окно панели задач — Explorer сам себя перезапустит
    // без прав администратора (в отличие от ShellExecuteW из elevated процесса)
    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (hTray) {
        PostMessageW(hTray, WM_QUIT, 0, 0);
        Sleep(1500);
    } else {
        killProcessByName(L"explorer.exe");
        Sleep(800);
    }

    // Если Explorer не перезапустился сам — запускаем вручную через полный путь
    if (!FindWindowW(L"Shell_TrayWnd", nullptr)) {
        wchar_t winDir[MAX_PATH];
        GetWindowsDirectoryW(winDir, MAX_PATH);
        std::wstring exp = std::wstring(winDir) + L"\\explorer.exe";
        ShellExecuteW(nullptr, nullptr, exp.c_str(), nullptr, nullptr, SW_SHOW);
    }
}

// ═══════════════════════════════════════════════════════════════
//  Запись файлов
// ═══════════════════════════════════════════════════════════════

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::wstring lastErrorText(DWORD err) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0,
                   reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring r = buf ? buf : L"";
    if (buf) LocalFree(buf);
    return r + L" (" + std::to_wstring(err) + L")";
}

// Записать файл с несколькими попытками. Сразу после завершения SmartClip
// антивирус или сама Windows могут ещё пару секунд держать DLL открытыми —
// раньше в этот момент установка падала с «отказано в доступе».
static bool writeFileRetry(const fs::path& target, const char* data, size_t size,
                           std::wstring& error) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);

    for (int attempt = 0; attempt < 10; ++attempt) {
        if (attempt > 0) { Sleep(300); pumpMessages(); }

        HANDLE h = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            // Файл занят — отодвигаем старый в сторону (переименовать занятый
            // файл Windows разрешает) и удалим его после перезагрузки
            if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
                fs::path old = target;
                old += L".old" + std::to_wstring(GetTickCount());
                if (MoveFileW(target.c_str(), old.c_str()))
                    MoveFileExW(old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
            }
            error = target.wstring() + L"\n" + lastErrorText(err);
            continue;
        }
        BOOL ok = TRUE;
        size_t done = 0;
        DWORD err = 0;
        while (ok && done < size) {
            DWORD written = 0;
            DWORD chunk = (DWORD)std::min<size_t>(size - done, 1u << 30);
            ok = WriteFile(h, data + done, chunk, &written, nullptr);
            if (!ok) err = GetLastError();
            done += written;
        }
        CloseHandle(h);
        if (ok && done == size) return true;
        error = target.wstring() + L"\n" + lastErrorText(err);
    }
    return false;
}

// Удалить старые файлы программы перед установкой новой версии.
// Иначе после смены версии Qt в папке остаются старые DLL/плагины,
// и программа может не запуститься. Данные пользователя (история,
// закрепы) лежат в %APPDATA% и не затрагиваются.
static void cleanInstallDir(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    const fs::path self = fs::path(getExePath());
    std::vector<fs::path> entries;
    for (auto& entry : fs::directory_iterator(dir, ec))
        entries.push_back(entry.path());
    for (const auto& p : entries) {
        std::error_code eqEc;
        if (fs::equivalent(p, self, eqEc)) continue;
        std::error_code rmEc;
        fs::remove_all(p, rmEc);
        if (rmEc && fs::is_regular_file(p, eqEc)) {
            // Занятый файл: переименовываем и удаляем после перезагрузки
            fs::path old = p;
            old += L".old" + std::to_wstring(GetTickCount());
            if (MoveFileW(p.c_str(), old.c_str()))
                MoveFileExW(old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  Встроенные файлы программы (payload)
// ═══════════════════════════════════════════════════════════════

// Есть ли в конце нашего exe встроенные файлы? Заполняет footer.
static bool readPayloadFooter(PayloadFooter& footer) {
    std::ifstream f(fs::path(getExePath()), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto fileSize = static_cast<uint64_t>(f.tellg());
    if (fileSize < sizeof(PayloadFooter)) return false;
    f.seekg(static_cast<std::streamoff>(fileSize - sizeof(PayloadFooter)));
    f.read(reinterpret_cast<char*>(&footer), sizeof(footer));
    return f && payloadMagicOk(footer) &&
           footer.stubSize + footer.packedSize + sizeof(PayloadFooter) == fileSize;
}

// Распаковать встроенные файлы в dst.
static bool extractPayload(const PayloadFooter& footer, const fs::path& dst,
                           std::wstring& error) {
    std::vector<char> packed(footer.packedSize);
    {
        std::ifstream f(fs::path(getExePath()), std::ios::binary);
        f.seekg(static_cast<std::streamoff>(footer.stubSize));
        f.read(packed.data(), static_cast<std::streamsize>(packed.size()));
        if (!f) { error = L"Cannot read the installer file (damaged download?)"; return false; }
    }
    pumpMessages();

    std::vector<char> raw(footer.rawSize);
    DECOMPRESSOR_HANDLE dec = nullptr;
    SIZE_T outSize = 0;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &dec) ||
        !Decompress(dec, packed.data(), packed.size(), raw.data(), raw.size(), &outSize) ||
        outSize != raw.size()) {
        error = L"Cannot unpack program files (damaged download?)\n" +
                lastErrorText(GetLastError());
        if (dec) CloseDecompressor(dec);
        return false;
    }
    CloseDecompressor(dec);
    packed.clear(); packed.shrink_to_fit();

    size_t pos = 0;
    auto need = [&](uint64_t n) { return n <= raw.size() - pos; };
    while (pos < raw.size()) {
        pumpMessages();
        uint32_t nameLen = 0; uint64_t size = 0;
        if (!need(4)) { error = L"Damaged installer data"; return false; }
        std::memcpy(&nameLen, raw.data() + pos, 4); pos += 4;
        if (!need(uint64_t(nameLen) + 8)) { error = L"Damaged installer data"; return false; }
        std::string name(raw.data() + pos, nameLen); pos += nameLen;
        std::memcpy(&size, raw.data() + pos, 8); pos += 8;
        if (!need(size)) { error = L"Damaged installer data"; return false; }

        // Защита от путей вида "../../Windows/..." — пишем только внутрь dst
        fs::path rel = fs::u8path(name).lexically_normal();
        if (rel.empty() || rel.is_absolute() || rel.has_root_name() || *rel.begin() == L"..") {
            error = L"Invalid path inside installer"; return false;
        }
        if (!writeFileRetry(dst / rel, raw.data() + pos, size, error)) return false;
        pos += size;
    }
    return true;
}

// Старый формат: копируем из папки _data\ рядом с установщиком.
static bool copyAllFiles(const fs::path& src, const fs::path& dst, std::wstring& error) {
    try {
        for (auto& entry : fs::recursive_directory_iterator(src,
                               fs::directory_options::skip_permission_denied)) {
            pumpMessages();
            if (!entry.is_regular_file()) continue;
            auto target = dst / fs::relative(entry.path(), src);
            std::ifstream in(entry.path(), std::ios::binary);
            std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
            if (!writeFileRetry(target, data.data(), data.size(), error)) return false;
        }
        return true;
    }
    catch (const std::exception& e) {
        error = toWide(e.what());
        return false;
    }
}

// Сохранить себя как Uninstaller.exe — только сам установщик, без
// встроенных файлов программы (иначе деинсталлятор весил бы десятки МБ).
static bool writeUninstaller(const fs::path& target, uint64_t stubSize, std::wstring& error) {
    std::ifstream in(fs::path(getExePath()), std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    if (stubSize > 0 && stubSize <= data.size()) data.resize(stubSize);
    return writeFileRetry(target, data.data(), data.size(), error);
}

// Запустить SmartClip БЕЗ прав администратора. Установщик работает от имени
// администратора, и обычный ShellExecute запустил бы SmartClip тоже с правами
// админа (тогда, например, не работает перетаскивание из обычных окон).
// Трюк: просим Проводник открыть exe — он запускает его как обычный пользователь.
static void launchAsUser(const std::wstring& exe, const std::wstring& dir) {
    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::wstring explorer = std::wstring(winDir) + L"\\explorer.exe";
    std::wstring args = L"\"" + exe + L"\"";
    if ((INT_PTR)ShellExecuteW(nullptr, L"open", explorer.c_str(), args.c_str(),
                               dir.c_str(), SW_SHOW) <= 32)
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, dir.c_str(), SW_SHOW);
}

// ═══════════════════════════════════════════════════════════════
//  Самоудаление через .bat (для деинсталлятора)
// ═══════════════════════════════════════════════════════════════

static void scheduleDeleteDir(const std::wstring& dir) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring bat = std::wstring(tempPath) + L"smartclip_cleanup.bat";

    // Batch-файл: ждём 2 сек, удаляем папку, удаляем сам себя
    // Пути ASCII-only (Program Files\SmartClip) → безопасно писать как ANSI
    std::string content =
        "@echo off\r\n"
        "ping 127.0.0.1 -n 5 > nul\r\n"   // ждём ~4 сек, пока Uninstaller.exe выйдет
        "rmdir /s /q \"" + std::string(dir.begin(), dir.end()) + "\"\r\n"
        "del \"%~f0\"\r\n";

    HANDLE hf = CreateFileW(bat.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hf, content.c_str(), (DWORD)content.size(), &written, nullptr);
        CloseHandle(hf);

        STARTUPINFOW si = {};
        si.cb           = sizeof(si);
        si.dwFlags      = STARTF_USESHOWWINDOW;
        si.wShowWindow  = SW_HIDE;

        PROCESS_INFORMATION pi = {};
        std::wstring cmd = L"cmd.exe /c \"" + bat + L"\"";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');

        CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread)  CloseHandle(pi.hThread);
    }
}

// ═══════════════════════════════════════════════════════════════
//  УСТАНОВКА
// ═══════════════════════════════════════════════════════════════

// updateMode = true — запуск из самого SmartClip (кнопка «Обновить»):
// не задаём лишних вопросов, язык не спрашиваем, в конце просто запускаем SmartClip.
static void doInstall(bool updateMode) {
    const std::wstring srcDir    = getDataDir();
    const std::wstring dstDir    = getInstallDir();
    const std::wstring appExe    = dstDir + L"\\" + APP_EXE;
    const std::wstring uninstExe = dstDir + L"\\" + UNINSTALLER_EXE;

    // ── Откуда берём файлы: изнутри exe (новый формат) или из _data\ ──
    PayloadFooter footer{};
    const bool embedded = readPayloadFooter(footer);
    if (!embedded && !fs::exists(fs::path(srcDir) / APP_EXE)) {
        errMsg(L"The installer is damaged or incomplete: program files not found.\n"
               L"Please download SmartClip_Installer again.");
        return;
    }

    const bool alreadyInstalled = fs::exists(fs::path(dstDir) / APP_EXE);

    // ── Язык: спрашиваем только при первой установке ───────────
    // (раньше спрашивали при каждом обновлении и сбрасывали выбор)
    std::wstring chosenLang = alreadyInstalled ? readLanguageFromIni() : chooseLanguage();
    if (chosenLang.empty()) return;  // Пользователь закрыл окно выбора языка

    // ── Подтверждение ──────────────────────────────────────────
    if (!updateMode) {
        if (alreadyInstalled) {
            if (!confirm(std::wstring(L"SmartClip is already installed.\n\n"
                         L"Update / reinstall to version ") + APP_VERSION + L"?\n"
                         L"Your data (history, pins) will be preserved."))
                return;
        } else {
            if (!confirm(L"Welcome to SmartClip " + std::wstring(APP_VERSION) + L" Setup!\n\n"
                         L"The application will be installed to:\n" + dstDir +
                         L"\n\nContinue?"))
                return;
        }
    }

    // ── Завершаем работающий SmartClip ─────────────────────────
    killProcessByName(APP_EXE);

    // ── Прогресс-окно ──────────────────────────────────────────
    createProgressWindow(updateMode ? L"Updating SmartClip, please wait..."
                                    : L"Installing SmartClip, please wait...");
    pumpMessages();

    // ── Копируем файлы ─────────────────────────────────────────
    // Старую версию не удаляем сразу, а отодвигаем в «SmartClip.old».
    // Если установка сорвётся (диск полон, антивирус, сбой) — вернём её назад,
    // и у пользователя останется рабочая программа. Раньше старые файлы
    // удалялись до копирования, и при ошибке программы не оставалось вообще.
    std::wstring error;
    std::error_code ec;
    const fs::path backupDir = dstDir + L".old";
    fs::remove_all(backupDir, ec);  // Остатки прошлой неудачной попытки
    bool haveBackup = false;
    if (fs::exists(dstDir, ec)) {
        haveBackup = MoveFileExW(dstDir.c_str(), backupDir.c_str(), 0) != 0;
        if (!haveBackup)
            cleanInstallDir(dstDir);  // Папку не отодвинуть (что-то держит файл) — чистим
    }
    fs::create_directories(dstDir, ec);

    bool ok = embedded ? extractPayload(footer, dstDir, error)
                       : copyAllFiles(srcDir, dstDir, error);
    if (ok)
        ok = writeUninstaller(uninstExe, embedded ? footer.stubSize : 0, error);

    if (haveBackup) {
        if (ok) {
            // Успех — старая версия больше не нужна
            fs::remove_all(backupDir, ec);
            if (ec) MoveFileExW(backupDir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        } else {
            // Провал — убираем недоустановленное и возвращаем старую версию
            fs::remove_all(dstDir, ec);
            MoveFileExW(backupDir.c_str(), dstDir.c_str(), 0);
        }
    }

    destroyProgressWindow();

    if (!ok) {
        errMsg(L"Installation failed:\n\n" + error +
               (haveBackup ? L"\n\nThe previous version has been restored." : L"") +
               L"\n\nClose all programs and try again. If it keeps failing, "
               L"restart the computer and run the installer once more.");
        if (haveBackup) launchAsUser(appExe, dstDir);  // Возвращаем пользователю рабочий SmartClip
        return;
    }

    // ── Язык → INI SmartClip ───────────────────────────────────
    writeLanguageToIni(chosenLang);

    // ── Реестр: автозапуск ─────────────────────────────────────
    // При обновлении не включаем автозапуск заново, если пользователь
    // сам выключил его в настройках SmartClip (тогда ключа в реестре нет).
    const bool autostartWasOn =
        !regReadStr(HKEY_CURRENT_USER, REG_RUN_KEY, APP_NAME).empty();
    if (!alreadyInstalled || autostartWasOn)
        regWriteStr(HKEY_CURRENT_USER, REG_RUN_KEY, APP_NAME,
                    (L"\"" + appExe + L"\"").c_str());

    // ── Реестр: «Programs and Features» ───────────────────────
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"DisplayName",     APP_NAME);
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"DisplayVersion",  APP_VERSION);
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"DisplayIcon",     appExe.c_str());
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"Publisher",       APP_NAME);
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"InstallLocation", dstDir.c_str());
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"UninstallString", (L"\"" + uninstExe + L"\" /uninstall").c_str());
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY, L"NoModify", L"1");
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY, L"NoRepair",  L"1");

    // ── Отключаем системный Win+V ──────────────────────────────
    const bool winVChanged = disableWinV();

    // ── Перезапуск Проводника — только если Win+V только что отключили ──
    bool restarted = false;
    if (winVChanged) {
        restarted = confirm(
            L"SmartClip has been installed successfully!\n\n"
            L"To make Win+V open SmartClip instead of the default clipboard,\n"
            L"Explorer needs to be restarted.\n\n"
            L"Restart Explorer now?");
        if (restarted) restartExplorer();
    }

    // Запускаем SmartClip (как обычный пользователь, не админ)
    launchAsUser(appExe, dstDir);

    if (!updateMode && !(winVChanged && restarted)) {
        info(winVChanged
             ? std::wstring(L"Installation complete!\n\nSmartClip is running and added to startup.\n"
                            L"To activate Win+V — restart Explorer or log out.")
             : L"Done! SmartClip " + std::wstring(APP_VERSION) + L" is installed and running.");
    }
}

// ═══════════════════════════════════════════════════════════════
//  УДАЛЕНИЕ
// ═══════════════════════════════════════════════════════════════

static void doUninstall() {
    const std::wstring dstDir = getInstallDir();

    const UninstStrings& str = getUninstStrings();

    if (!fs::exists(dstDir)) {
        info(str.notFound);
        return;
    }

    if (!confirm(str.confirm))
        return;

    // ── Завершаем SmartClip ────────────────────────────────────
    killProcessByName(APP_EXE);

    // ── Удаляем записи реестра ────────────────────────────────
    regDeleteVal(HKEY_CURRENT_USER, REG_RUN_KEY, APP_NAME);
    enableWinV();
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY);

    // ── Спрашиваем про Explorer ДО запуска batch ──────────────
    // (важно: scheduleDeleteDir должен быть самым последним действием,
    //  чтобы Uninstaller.exe успел завершиться до удаления папки)
    bool restart = confirm(str.restartExplorer);
    if (restart) restartExplorer();

    // ── Планируем удаление папки — самым последним! ───────────
    // Batch ждёт 4 сек → к тому времени процесс уже завершится
    scheduleDeleteDir(dstDir);
}

// ═══════════════════════════════════════════════════════════════
//  Точка входа
// ═══════════════════════════════════════════════════════════════

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
    // Включаем визуальные стили (Common Controls v6)
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    // Разбираем аргументы (lpCmdLine — ANSI-строка командной строки)
    std::string args = lpCmdLine ? lpCmdLine : "";
    bool uninstall   = (args.find("/uninstall") != std::string::npos);
    // Двойной клик по Uninstaller.exe в папке программы (без /uninstall)
    // раньше показывал «установщик повреждён» — теперь это тоже удаление
    std::wstring selfName = fs::path(getExePath()).filename().wstring();
    if (_wcsicmp(selfName.c_str(), UNINSTALLER_EXE) == 0) uninstall = true;
    bool update      = (args.find("/update")    != std::string::npos);

    if (uninstall)
        doUninstall();
    else
        doInstall(update);

    return 0;
}
