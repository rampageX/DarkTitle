#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

#ifdef _MSC_VER
#pragma comment(lib, "dwmapi.lib")
#endif

namespace
{
constexpr DWORD DarkModeAttribute = 20;
constexpr wchar_t ThemesKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes";
constexpr wchar_t PersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

bool lightTheme = true;
bool watchingTheme = false;

bool ReadLightTheme(bool previous)
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, PersonalizeKey, L"AppsUseLightTheme",
        RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status == ERROR_SUCCESS)
        return value != 0;
    return status == ERROR_FILE_NOT_FOUND ? true : previous;
}

void ApplyTheme(HWND window)
{
    const BOOL dark = !lightTheme;
    DwmSetWindowAttribute(window, DarkModeAttribute, &dark, sizeof(dark));
}

BOOL CALLBACK ApplyToExistingWindow(HWND window, LPARAM)
{
    ApplyTheme(window);
    return TRUE;
}

void RefreshTheme()
{
    const bool current = ReadLightTheme(lightTheme);
    if (current != lightTheme)
    {
        lightTheme = current;
        EnumWindows(ApplyToExistingWindow, 0);
    }
}

void CALLBACK OnWindowShown(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child,
                            DWORD, DWORD)
{
    if (!window || object != OBJID_WINDOW || child != CHILDID_SELF ||
        GetAncestor(window, GA_ROOT) != window || !IsWindowVisible(window))
        return;

    if (!watchingTheme)
        RefreshTheme();
    ApplyTheme(window);
}

bool ArmThemeNotification(HKEY& key, HANDLE changed)
{
    if (!key && RegOpenKeyExW(HKEY_CURRENT_USER, ThemesKey, 0, KEY_NOTIFY, &key) != ERROR_SUCCESS)
        return false;

    // Watch the parent so replacing the Personalize key also produces a notification.
    if (RegNotifyChangeKeyValue(key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                               changed, TRUE) == ERROR_SUCCESS)
        return true;

    RegCloseKey(key);
    key = nullptr;
    return false;
}
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    const HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                                             OnWindowShown, 0, 0,
                                             WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!hook)
        return 1;

    const HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HKEY key = nullptr;
    watchingTheme = changed && ArmThemeNotification(key, changed);
    lightTheme = ReadLightTheme(lightTheme);
    EnumWindows(ApplyToExistingWindow, 0);

    int exitCode = 0;
    for (;;)
    {
        const DWORD handleCount = watchingTheme ? 1 : 0;
        const DWORD result = MsgWaitForMultipleObjectsEx(
            handleCount, watchingTheme ? &changed : nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (watchingTheme && result == WAIT_OBJECT_0)
        {
            watchingTheme = ArmThemeNotification(key, changed);
            RefreshTheme();
        }
        else if (result == WAIT_OBJECT_0 + handleCount)
        {
            MSG message;
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    exitCode = static_cast<int>(message.wParam);
                    break;
                }
                DispatchMessageW(&message);
            }
        }
        else
        {
            exitCode = 2;
            break;
        }
    }

    UnhookWinEvent(hook);
    if (key)
        RegCloseKey(key);
    if (changed)
        CloseHandle(changed);
    return exitCode;
}
