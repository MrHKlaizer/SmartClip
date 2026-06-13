/*
 *  SmartClip Installer / Uninstaller
 *  ──────────────────────────────────
 *  Запуск:  Installer.exe             → установка
 *           Installer.exe /uninstall  → удаление
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
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <stdexcept>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════
//  Константы
// ═══════════════════════════════════════════════════════════════

static const wchar_t* APP_NAME        = L"SmartClip";
static const wchar_t* APP_VERSION     = L"1.0.0";
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
    wchar_t pf[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, pf);
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
    TaskDialogIndirect(&cfg, &clicked, nullptr, nullptr);

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

static void disableWinV() {
    std::wstring val = regReadStr(HKEY_CURRENT_USER, REG_EXPLORER_KEY, L"DisabledHotkeys");
    if (val.find(L'V') == std::wstring::npos) {
        val += L'V';
        regWriteStr(HKEY_CURRENT_USER, REG_EXPLORER_KEY, L"DisabledHotkeys", val.c_str());
    }
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
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    Sleep(600);
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
//  Копирование файлов
// ═══════════════════════════════════════════════════════════════

// Копирует всё из src в dst, пропуская файл с именем skipFilename.
// Обновляет прогресс-окно через pumpMessages().
static bool copyAllFiles(const fs::path& src, const fs::path& dst,
                         const std::wstring& skipFilename) {
    try {
        fs::create_directories(dst);

        for (auto& entry : fs::recursive_directory_iterator(src,
                               fs::directory_options::skip_permission_denied)) {
            pumpMessages();

            auto rel    = fs::relative(entry.path(), src);
            auto target = dst / rel;

            if (entry.is_directory()) {
                fs::create_directories(target);
                continue;
            }

            // Пропускаем сам установщик
            std::wstring fname = entry.path().filename().wstring();
            std::wstring fnameLow = fname;
            std::transform(fnameLow.begin(), fnameLow.end(), fnameLow.begin(), ::towlower);
            if (fnameLow == skipFilename) continue;

            fs::copy_file(entry.path(), target,
                          fs::copy_options::overwrite_existing);
        }
        return true;
    }
    catch (const std::exception& e) {
        std::string s = e.what();
        errMsg(L"Ошибка при копировании файлов:\n" +
               std::wstring(s.begin(), s.end()));
        return false;
    }
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

static void doInstall() {
    const std::wstring srcDir    = getDataDir();
    const std::wstring dstDir    = getInstallDir();
    const std::wstring appExe    = dstDir + L"\\" + APP_EXE;
    const std::wstring uninstExe = dstDir + L"\\" + UNINSTALLER_EXE;

    // ── Проверяем наличие _data\ ───────────────────────────────
    if (!fs::exists(fs::path(srcDir) / APP_EXE)) {
        errMsg(L"SmartClip.exe not found in _data\\ folder.\n"
               L"Make sure the _data\\ folder is next to Installer.exe.");
        return;
    }

    // ── Выбор языка ────────────────────────────────────────────
    std::wstring chosenLang = chooseLanguage();

    // ── Приветствие ────────────────────────────────────────────
    bool alreadyInstalled = fs::exists(fs::path(dstDir) / APP_EXE);

    if (alreadyInstalled) {
        if (!confirm(
                L"SmartClip is already installed.\n\n"
                L"Reinstall? All files will be overwritten.\n"
                L"Your data (history, pins) will be preserved."))
            return;
    } else {
        if (!confirm(
                L"Welcome to SmartClip Setup!\n\n"
                L"The application will be installed to:\n" + dstDir +
                L"\n\nContinue?"))
            return;
    }

    // ── Завершаем работающий SmartClip ─────────────────────────
    killProcessByName(APP_EXE);

    // ── Прогресс-окно ──────────────────────────────────────────
    createProgressWindow(L"Installing SmartClip, please wait...");

    // ── Копируем файлы ─────────────────────────────────────────
    bool ok = copyAllFiles(srcDir, dstDir, L"");

    if (ok) {
        try {
            fs::copy_file(getExePath(),
                          fs::path(dstDir) / UNINSTALLER_EXE,
                          fs::copy_options::overwrite_existing);
        } catch (...) {}
    }

    destroyProgressWindow();

    if (!ok) return;

    // ── Язык → INI SmartClip ───────────────────────────────────
    writeLanguageToIni(chosenLang);

    // ── Реестр: автозапуск ─────────────────────────────────────
    regWriteStr(HKEY_CURRENT_USER, REG_RUN_KEY, APP_NAME, appExe.c_str());

    // ── Реестр: «Programs and Features» ───────────────────────
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"DisplayName",     APP_NAME);
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"DisplayVersion",  APP_VERSION);
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"Publisher",       APP_NAME);
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"InstallLocation", dstDir.c_str());
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY,
                L"UninstallString", (uninstExe + L" /uninstall").c_str());
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY, L"NoModify", L"1");
    regWriteStr(HKEY_LOCAL_MACHINE, REG_UNINSTALL_KEY, L"NoRepair",  L"1");

    // ── Отключаем системный Win+V ──────────────────────────────
    disableWinV();

    // ── Финальный экран ────────────────────────────────────────
    bool restart = confirm(
        L"SmartClip has been installed successfully!\n\n"
        L"To make Win+V open SmartClip instead of the default clipboard,\n"
        L"Explorer needs to be restarted.\n\n"
        L"Restart Explorer now?");

    if (restart) restartExplorer();

    // Запускаем SmartClip
    ShellExecuteW(nullptr, L"open", appExe.c_str(),
                  nullptr, dstDir.c_str(), SW_SHOW);

    if (!restart) {
        info(L"Installation complete!\n\n"
             L"SmartClip is running and added to startup.\n"
             L"To activate Win+V — restart Explorer or log out.");
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

    if (uninstall)
        doUninstall();
    else
        doInstall();

    return 0;
}
