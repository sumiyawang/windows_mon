#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winreg.h>
#include <stdio.h>
#include <string.h>

#define WM_TRAYICON (WM_APP + 1)
#define TIMER_MONITOR 1
#define IDM_REFRESH 1001
#define IDM_INTERVAL_1S 1010
#define IDM_INTERVAL_2S 1011
#define IDM_INTERVAL_5S 1012
#define IDM_EXIT 1099
#define IDI_APP 1
#define MAX_COLOR_RULES 16

typedef struct ColorRule {
    int min_percent;
    COLORREF color;
} ColorRule;

static NOTIFYICONDATAA g_notify;
static UINT g_interval_ms = 1000;
static ULONGLONG g_prev_idle = 0;
static ULONGLONG g_prev_kernel = 0;
static ULONGLONG g_prev_user = 0;
static BOOL g_have_cpu_sample = FALSE;
static HANDLE g_single_instance = NULL;
static HICON g_tray_icon = NULL;
static ColorRule g_color_rules[MAX_COLOR_RULES];
static int g_color_rule_count = 0;

static void add_color_rule(int min_percent, int red, int green, int blue) {
    if (g_color_rule_count >= MAX_COLOR_RULES) {
        return;
    }
    if (min_percent < 0 || min_percent > 100 ||
        red < 0 || red > 255 ||
        green < 0 || green > 255 ||
        blue < 0 || blue > 255) {
        return;
    }
    g_color_rules[g_color_rule_count].min_percent = min_percent;
    g_color_rules[g_color_rule_count].color = RGB(red, green, blue);
    g_color_rule_count++;
}

static void sort_color_rules(void) {
    int i, j;
    for (i = 0; i < g_color_rule_count - 1; i++) {
        for (j = i + 1; j < g_color_rule_count; j++) {
            if (g_color_rules[j].min_percent > g_color_rules[i].min_percent) {
                ColorRule temp = g_color_rules[i];
                g_color_rules[i] = g_color_rules[j];
                g_color_rules[j] = temp;
            }
        }
    }
}

static void load_default_color_rules(void) {
    g_color_rule_count = 0;
    add_color_rule(90, 128, 0, 128);
    add_color_rule(80, 220, 20, 60);
    add_color_rule(70, 255, 215, 0);
    add_color_rule(50, 0, 112, 192);
    add_color_rule(0, 0, 176, 80);
}

static void get_config_path(char *path, DWORD path_size) {
    DWORD length = GetModuleFileNameA(NULL, path, path_size);
    char *last_slash;

    if (length == 0 || length >= path_size) {
        lstrcpynA(path, "memory-colors.cfg", path_size);
        return;
    }

    last_slash = strrchr(path, '\\');
    if (!last_slash) {
        last_slash = strrchr(path, '/');
    }
    if (last_slash) {
        *(last_slash + 1) = '\0';
        lstrcatA(path, "memory-colors.cfg");
    } else {
        lstrcpynA(path, "memory-colors.cfg", path_size);
    }
}

static void load_color_rules(void) {
    char path[MAX_PATH];
    char line[128];
    FILE *file = NULL;
    ColorRule loaded_rules[MAX_COLOR_RULES];
    int loaded_count = 0;

    load_default_color_rules();
    get_config_path(path, sizeof(path));

    if (fopen_s(&file, path, "r") != 0 || !file) {
        return;
    }

    while (fgets(line, sizeof(line), file)) {
        int min_percent, red, green, blue;
        char marker;
        if (line[0] == '#' || line[0] == ';' || line[0] == '\r' || line[0] == '\n') {
            continue;
        }
        if (sscanf_s(line, " %d %d %d %d %c", &min_percent, &red, &green, &blue, &marker, 1) != 4) {
            continue;
        }
        if (min_percent < 0 || min_percent > 100 ||
            red < 0 || red > 255 ||
            green < 0 || green > 255 ||
            blue < 0 || blue > 255 ||
            loaded_count >= MAX_COLOR_RULES) {
            continue;
        }
        loaded_rules[loaded_count].min_percent = min_percent;
        loaded_rules[loaded_count].color = RGB(red, green, blue);
        loaded_count++;
    }
    fclose(file);

    if (loaded_count == 0) {
        return;
    }

    g_color_rule_count = loaded_count;
    memcpy(g_color_rules, loaded_rules, sizeof(ColorRule) * loaded_count);
    sort_color_rules();
}

static BOOL register_startup(void) {
    HKEY key;
    char executable[MAX_PATH];
    char command[MAX_PATH + 3];
    DWORD length;
    LONG result;

    length = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (length == 0 || length >= sizeof(executable)) {
        return FALSE;
    }

    _snprintf_s(command, sizeof(command), _TRUNCATE, "\"%s\"", executable);
    result = RegCreateKeyExA(HKEY_CURRENT_USER,
                             "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                             0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS) {
        return FALSE;
    }

    result = RegSetValueExA(key, "TaskbarMonitor", 0, REG_SZ,
                            (const BYTE *)command,
                            (DWORD)(strlen(command) + 1));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static ULONGLONG file_time_to_u64(FILETIME value) {
    ULARGE_INTEGER converted;
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

static int get_cpu_percent(void) {
    FILETIME idle_time, kernel_time, user_time;
    ULONGLONG idle, kernel, user;
    ULONGLONG idle_delta, kernel_delta, user_delta, total_delta;
    int percent;

    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        return -1;
    }

    idle = file_time_to_u64(idle_time);
    kernel = file_time_to_u64(kernel_time);
    user = file_time_to_u64(user_time);

    if (!g_have_cpu_sample) {
        g_prev_idle = idle;
        g_prev_kernel = kernel;
        g_prev_user = user;
        g_have_cpu_sample = TRUE;
        return 0;
    }

    idle_delta = idle - g_prev_idle;
    kernel_delta = kernel - g_prev_kernel;
    user_delta = user - g_prev_user;
    total_delta = kernel_delta + user_delta;

    g_prev_idle = idle;
    g_prev_kernel = kernel;
    g_prev_user = user;

    if (total_delta == 0) {
        return 0;
    }

    if (kernel_delta < idle_delta) {
        return 0;
    }

    percent = (int)(((total_delta - idle_delta) * 100ULL) / total_delta);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

static int get_memory_percent(void) {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status) || status.ullTotalPhys == 0) {
        return -1;
    }
    return (int)(((status.ullTotalPhys - status.ullAvailPhys) * 100ULL) / status.ullTotalPhys);
}

static COLORREF get_memory_color(int memory) {
    int i;
    for (i = 0; i < g_color_rule_count; i++) {
        if (memory >= g_color_rules[i].min_percent) {
            return g_color_rules[i].color;
        }
    }
    return RGB(0, 176, 80);
}

static HICON create_memory_icon(int memory) {
    const int size = 32;
    COLORREF color = get_memory_color(memory);
    BITMAPINFO bmi;
    void *bits = NULL;
    HBITMAP color_bitmap = NULL;
    HBITMAP mask_bitmap = NULL;
    HDC dc = NULL;
    HGDIOBJ old_bitmap = NULL;
    HPEN pen = NULL;
    HBRUSH brush = NULL;
    HICON icon = NULL;
    ICONINFO icon_info;
    DWORD *pixels;
    DWORD fill_color;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dc = CreateCompatibleDC(NULL);
    if (!dc) {
        goto cleanup;
    }

    color_bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!color_bitmap || !bits) {
        goto cleanup;
    }

    mask_bitmap = CreateBitmap(size, size, 1, 1, NULL);
    if (!mask_bitmap) {
        goto cleanup;
    }

    old_bitmap = SelectObject(dc, color_bitmap);
    if (!old_bitmap) {
        goto cleanup;
    }

    fill_color = 0xFF000000UL |
                 ((DWORD)GetRValue(color) << 16) |
                 ((DWORD)GetGValue(color) << 8) |
                 (DWORD)GetBValue(color);
    pixels = (DWORD *)bits;
    for (int i = 0; i < size * size; ++i) {
        pixels[i] = fill_color;
    }

    pen = CreatePen(PS_SOLID, 2, RGB(25, 25, 25));
    brush = CreateSolidBrush(RGB(25, 25, 25));
    if (!pen || !brush) {
        goto cleanup;
    }

    SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, 4, 4, 28, 28);
    SelectObject(dc, brush);
    Ellipse(dc, 9, 11, 12, 14);
    Ellipse(dc, 20, 11, 23, 14);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Arc(dc, 10, 14, 22, 24, 10, 20, 22, 20);
    MoveToEx(dc, 12, 6, NULL);
    LineTo(dc, 15, 3);

    ZeroMemory(&icon_info, sizeof(icon_info));
    icon_info.fIcon = TRUE;
    icon_info.hbmColor = color_bitmap;
    icon_info.hbmMask = mask_bitmap;
    icon = CreateIconIndirect(&icon_info);

cleanup:
    if (dc) {
        if (old_bitmap) {
            SelectObject(dc, old_bitmap);
        }
        DeleteDC(dc);
    }
    if (pen) {
        DeleteObject(pen);
    }
    if (brush) {
        DeleteObject(brush);
    }
    if (color_bitmap) {
        DeleteObject(color_bitmap);
    }
    if (mask_bitmap) {
        DeleteObject(mask_bitmap);
    }

    return icon;
}

static void update_tray_icon(void) {
    HICON next_icon;
    HICON previous_icon;
    int memory;

    memory = get_memory_percent();
    next_icon = create_memory_icon(memory);
    if (!next_icon) {
        return;
    }

    previous_icon = g_tray_icon;
    g_tray_icon = next_icon;
    g_notify.hIcon = g_tray_icon;
    if (!Shell_NotifyIconA(NIM_MODIFY, &g_notify)) {
        g_notify.hIcon = previous_icon;
        g_tray_icon = previous_icon;
        DestroyIcon(next_icon);
        return;
    }

    if (previous_icon) {
        DestroyIcon(previous_icon);
    }
}

static void update_tooltip(void) {
    int cpu = get_cpu_percent();
    int memory = get_memory_percent();
    char text[128];

    if (cpu < 0 || memory < 0) {
        lstrcpynA(text, "CPU --% | MEM --%", sizeof(text));
    } else {
        _snprintf_s(text, sizeof(text), _TRUNCATE, "CPU %d%% | MEM %d%%", cpu, memory);
    }

    ZeroMemory(g_notify.szTip, sizeof(g_notify.szTip));
    lstrcpynA(g_notify.szTip, text, sizeof(g_notify.szTip));
    Shell_NotifyIconA(NIM_MODIFY, &g_notify);
}

static void set_interval(HWND window, UINT interval_ms) {
    g_interval_ms = interval_ms;
    KillTimer(window, TIMER_MONITOR);
    SetTimer(window, TIMER_MONITOR, g_interval_ms, NULL);
    update_tray_icon();
    update_tooltip();
}

static void show_context_menu(HWND window) {
    POINT point;
    HMENU menu;
    HMENU interval_menu;

    GetCursorPos(&point);
    menu = CreatePopupMenu();
    interval_menu = CreatePopupMenu();
    if (!menu || !interval_menu) {
        if (interval_menu) DestroyMenu(interval_menu);
        if (menu) DestroyMenu(menu);
        return;
    }

    AppendMenuA(interval_menu, MF_STRING | (g_interval_ms == 1000 ? MF_CHECKED : 0), IDM_INTERVAL_1S, "1 second");
    AppendMenuA(interval_menu, MF_STRING | (g_interval_ms == 2000 ? MF_CHECKED : 0), IDM_INTERVAL_2S, "2 seconds");
    AppendMenuA(interval_menu, MF_STRING | (g_interval_ms == 5000 ? MF_CHECKED : 0), IDM_INTERVAL_5S, "5 seconds");
    AppendMenuA(menu, MF_STRING, IDM_REFRESH, "Refresh now");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)interval_menu, "Refresh interval");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_EXIT, "Exit");

    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window, NULL);
    PostMessageA(window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        update_tooltip();
        SetTimer(window, TIMER_MONITOR, g_interval_ms, NULL);
        return 0;

    case WM_TIMER:
        if (wparam == TIMER_MONITOR) {
            update_tray_icon();
            update_tooltip();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDM_REFRESH:
            update_tray_icon();
            update_tooltip();
            return 0;
        case IDM_INTERVAL_1S:
            set_interval(window, 1000);
            return 0;
        case IDM_INTERVAL_2S:
            set_interval(window, 2000);
            return 0;
        case IDM_INTERVAL_5S:
            set_interval(window, 5000);
            return 0;
        case IDM_EXIT:
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
        break;

    case WM_TRAYICON:
        if (lparam == WM_RBUTTONUP) {
            show_context_menu(window);
        } else if (lparam == WM_LBUTTONDBLCLK) {
            ShellExecuteA(NULL, "open", "taskmgr.exe", NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(window, TIMER_MONITOR);
        Shell_NotifyIconA(NIM_DELETE, &g_notify);
        if (g_tray_icon) {
            DestroyIcon(g_tray_icon);
            g_tray_icon = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command) {
    const char class_name[] = "TaskbarMonitorWindow";
    WNDCLASSA window_class;
    HWND window;
    MSG message;

    (void)previous;
    (void)command_line;
    (void)show_command;
    g_single_instance = CreateMutexA(NULL, FALSE, "Local\\TaskbarMonitor.SingleInstance");
    if (!g_single_instance) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_single_instance);
        return 0;
    }
    load_color_rules();
    register_startup();
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    if (!RegisterClassA(&window_class)) {
        return 1;
    }

    window = CreateWindowExA(0, class_name, "Taskbar Monitor", WS_OVERLAPPED,
                             0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!window) {
        return 1;
    }

    ZeroMemory(&g_notify, sizeof(g_notify));
    g_notify.cbSize = sizeof(g_notify);
    g_notify.hWnd = window;
    g_notify.uID = 1;
    g_notify.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_notify.uCallbackMessage = WM_TRAYICON;
    lstrcpynA(g_notify.szTip, "CPU --% | MEM --%", sizeof(g_notify.szTip));

    g_tray_icon = create_memory_icon(get_memory_percent());
    g_notify.hIcon = g_tray_icon;

    if (!Shell_NotifyIconA(NIM_ADD, &g_notify)) {
        if (g_tray_icon) {
            DestroyIcon(g_tray_icon);
            g_tray_icon = NULL;
        }
        DestroyWindow(window);
        return 1;
    }

    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    CloseHandle(g_single_instance);
    return (int)message.wParam;
}
