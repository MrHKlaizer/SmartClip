#include "AdminMode.h"

#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <string>
#include <vector>

namespace {

const wchar_t *TASK_NAME = L"SmartClip Admin";

std::wstring exePath()
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return std::wstring(buf, n);
}

std::wstring systemExe(const wchar_t *name)
{
    wchar_t dir[MAX_PATH];
    GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir) + L"\\" + name;
}

// Запустить консольную программу без окна и дождаться кода выхода
DWORD runHidden(const std::wstring &exe, const std::wstring &args, DWORD timeoutMs = 15000)
{
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
        return DWORD(-1);
    DWORD code = DWORD(-1);
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

std::wstring xmlEscape(const std::wstring &s)
{
    std::wstring r;
    for (wchar_t c : s) {
        switch (c) {
        case L'&': r += L"&amp;"; break;
        case L'<': r += L"&lt;"; break;
        case L'>': r += L"&gt;"; break;
        case L'"': r += L"&quot;"; break;
        default:   r += c;
        }
    }
    return r;
}

// SID текущего пользователя — задача должна запускаться именно от него
std::wstring currentUserSid()
{
    std::wstring result;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return result;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buf(size);
    if (size && GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
        LPWSTR str = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buf.data())->User.Sid, &str)) {
            result = str;
            LocalFree(str);
        }
    }
    CloseHandle(token);
    return result;
}

} // namespace

namespace AdminMode {

bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION el = {};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &el, sizeof(el), &size);
    CloseHandle(token);
    return ok && el.TokenIsElevated;
}

bool canElevate()
{
    if (isElevated()) return true;
    // «Ограниченный» токен бывает только у администратора при включённом UAC.
    // У обычного пользователя — Default: повышать права некуда.
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION_TYPE type = TokenElevationTypeDefault;
    DWORD size = 0;
    GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size);
    CloseHandle(token);
    return type == TokenElevationTypeLimited;
}

bool createTask()
{
    const std::wstring exe = exePath();
    const std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
    const std::wstring sid = currentUserSid();

    // Без триггеров: задачу запускает сам SmartClip (runTask).
    // ExecutionTimeLimit PT0S — иначе Windows закроет программу через 72 часа.
    // Priority 4 — обычный приоритет (по умолчанию у задач пониженный — 7).
    const std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo><Description>SmartClip: запуск с правами администратора "
        L"(чтобы работать в окнах администратора)</Description></RegistrationInfo>\r\n"
        L"  <Principals><Principal id=\"Author\">"
        + (sid.empty() ? std::wstring() : L"<UserId>" + sid + L"</UserId>") +
        L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel>"
        L"</Principal></Principals>\r\n"
        L"  <Settings>\r\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
        L"    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>\r\n"
        L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
        L"    <Enabled>true</Enabled>\r\n"
        L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
        L"    <Priority>4</Priority>\r\n"
        L"  </Settings>\r\n"
        L"  <Actions Context=\"Author\"><Exec>"
        L"<Command>" + xmlEscape(exe) + L"</Command>"
        L"<Arguments>--elevated</Arguments>"
        L"<WorkingDirectory>" + xmlEscape(dir) + L"</WorkingDirectory>"
        L"</Exec></Actions>\r\n"
        L"</Task>\r\n";

    // schtasks принимает XML в UTF-16 с BOM
    wchar_t tmpDir[MAX_PATH], tmpFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    if (!GetTempFileNameW(tmpDir, L"scl", 0, tmpFile)) return false;
    HANDLE f = CreateFileW(tmpFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const wchar_t bom = 0xFEFF;
    WriteFile(f, &bom, sizeof(bom), &written, nullptr);
    WriteFile(f, xml.data(), DWORD(xml.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);

    const DWORD code = runHidden(systemExe(L"schtasks.exe"),
        L"/create /tn \"" + std::wstring(TASK_NAME) + L"\" /xml \"" + tmpFile + L"\" /f");
    DeleteFileW(tmpFile);
    return code == 0;
}

void deleteTask()
{
    if (!isElevated()) return;
    runHidden(systemExe(L"schtasks.exe"), L"/delete /tn \"" + std::wstring(TASK_NAME) + L"\" /f");
}

bool runTask()
{
    return runHidden(systemExe(L"schtasks.exe"),
                     L"/run /tn \"" + std::wstring(TASK_NAME) + L"\"") == 0;
}

bool requestCreateTask()
{
    if (isElevated()) return createTask();
    const std::wstring exe = exePath();
    const std::wstring args = L"--create-admin-task";
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"runas";
    sei.lpFile       = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow        = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return false;  // Отказ в окне UAC
    DWORD code = DWORD(-1);
    if (WaitForSingleObject(sei.hProcess, 30000) == WAIT_OBJECT_0)
        GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0;
}

void launchAsUser()
{
    // Проводник всегда работает как обычный пользователь — просим его открыть exe
    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    const std::wstring explorer = std::wstring(winDir) + L"\\explorer.exe";
    const std::wstring args = L"\"" + exePath() + L"\"";
    ShellExecuteW(nullptr, L"open", explorer.c_str(), args.c_str(), nullptr, SW_SHOW);
}

} // namespace AdminMode
