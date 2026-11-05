// tide-crash's window on Windows (crash.h): drawn with GDI, a heading, a
// line of text and two flat buttons, so it looks the same on every Windows
// and needs nothing but user32 and gdi32. Enter shows the report, Escape
// closes.
#define WIN32_LEAN_AND_MEAN
#define UNICODE // IDC_ and IDI_ names as wide resources
#include <windows.h>

#include "crash.h"

#include <stdlib.h>
#include <string.h>

typedef struct button {
    const wchar_t *label;
    RECT rect;
    bool primary;
} button;

static struct {
    wchar_t heading[512], body[1024];
    button buttons[2]; // Show report, Close
    int hover;         // A button under the pointer, or -1
    int answer;
    int dpi;
    HFONT heading_font, body_font;
} ui = {.hover = -1};

static int px(const int logical)
{
    return MulDiv(logical, ui.dpi, 96);
}

static HFONT make_font(const int size, const int weight)
{
    return CreateFontW(-px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static void layout(const RECT *client)
{
    const int height = px(32), gap = px(8), pad = px(24);
    const int widths[2] = {px(124), px(88)};
    int right = client->right - pad;
    for (int i = 1; i >= 0; i--) {
        ui.buttons[i].rect = (RECT){right - widths[i], client->bottom - pad - height, right, client->bottom - pad};
        right -= widths[i] + gap;
    }
}

static void fill(const HDC dc, const RECT *rect, const COLORREF color)
{
    const HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, rect, brush);
    DeleteObject(brush);
}

static void paint(const HWND window)
{
    PAINTSTRUCT ps;
    const HDC dc = BeginPaint(window, &ps);
    RECT client;
    GetClientRect(window, &client);
    fill(dc, &client, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    const int pad = px(24);
    RECT text = {pad, pad, client.right - pad, client.bottom};
    SelectObject(dc, ui.heading_font);
    SetTextColor(dc, RGB(28, 28, 30));
    RECT measured = text;
    DrawTextW(dc, ui.heading, -1, &measured, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    DrawTextW(dc, ui.heading, -1, &text, DT_WORDBREAK | DT_NOPREFIX);
    text.top = measured.bottom + px(10);
    SelectObject(dc, ui.body_font);
    SetTextColor(dc, RGB(80, 80, 86));
    DrawTextW(dc, ui.body, -1, &text, DT_WORDBREAK | DT_NOPREFIX);
    for (int i = 0; i < 2; i++) {
        const button *b = &ui.buttons[i];
        const bool over = ui.hover == i;
        if (b->primary) fill(dc, &b->rect, over ? RGB(29, 78, 216) : RGB(37, 99, 235));
        else {
            fill(dc, &b->rect, RGB(200, 200, 206));
            RECT inside = {b->rect.left + 1, b->rect.top + 1, b->rect.right - 1, b->rect.bottom - 1};
            fill(dc, &inside, over ? RGB(242, 242, 245) : RGB(255, 255, 255));
        }
        SetTextColor(dc, b->primary ? RGB(255, 255, 255) : RGB(28, 28, 30));
        RECT label = b->rect;
        DrawTextW(dc, b->label, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    EndPaint(window, &ps);
}

static int button_at(const LPARAM where)
{
    const POINT p = {(short)LOWORD(where), (short)HIWORD(where)};
    for (int i = 0; i < 2; i++) if (PtInRect(&ui.buttons[i].rect, p)) return i;
    return -1;
}

static void answer(const HWND window, const int value)
{
    ui.answer = value;
    DestroyWindow(window);
}

static LRESULT CALLBACK on_message(const HWND window, const UINT message, const WPARAM w, const LPARAM l)
{
    switch (message) {
    case WM_SIZE: {
        RECT client;
        GetClientRect(window, &client);
        layout(&client);
        return 0;
    }
    case WM_PAINT: paint(window); return 0;
    case WM_MOUSEMOVE: {
        const int over = button_at(l);
        if (over != ui.hover) {
            ui.hover = over;
            InvalidateRect(window, NULL, FALSE);
            TRACKMOUSEEVENT track = {.cbSize = sizeof track, .dwFlags = TME_LEAVE, .hwndTrack = window};
            TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        ui.hover = -1;
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, ui.hover >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP: {
        const int at = button_at(l);
        if (at >= 0) answer(window, at == 0 ? 1 : 0);
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_RETURN) answer(window, 1);
        if (w == VK_ESCAPE) answer(window, 0);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(window, message, w, l);
}

static void widen(const char *text, wchar_t *out, const int size)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, out, size)) out[0] = L'\0';
}

int crash_ask(const char *title, const char *text)
{
    SetProcessDPIAware();
    const HDC screen = GetDC(NULL);
    ui.dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen) ReleaseDC(NULL, screen);
    if (ui.dpi <= 0) ui.dpi = 96;
    // The heading is the text's first paragraph, the rest is under it
    const char *split = strstr(text, "\n\n");
    char heading[512];
    const size_t length = split ? (size_t)(split - text) : strlen(text);
    memcpy(heading, text, length < sizeof heading - 1u ? length : sizeof heading - 1u);
    heading[length < sizeof heading - 1u ? length : sizeof heading - 1u] = '\0';
    widen(heading, ui.heading, 512);
    widen(split ? split + 2 : "", ui.body, 1024);
    ui.buttons[0] = (button){.label = L"Show report", .primary = true};
    ui.buttons[1] = (button){.label = L"Close"};
    ui.heading_font = make_font(17, FW_SEMIBOLD);
    ui.body_font = make_font(14, FW_NORMAL);
    const HINSTANCE instance = GetModuleHandleW(NULL);
    const WNDCLASSW kind = {.lpfnWndProc = on_message,
                            .hInstance = instance,
                            .hCursor = LoadCursorW(NULL, IDC_ARROW),
                            .hIcon = LoadIconW(NULL, IDI_ERROR),
                            .lpszClassName = L"TideCrash"};
    if (!RegisterClassW(&kind)) return -1;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT size = {0, 0, px(460), px(190)};
    AdjustWindowRect(&size, style, FALSE);
    const int width = size.right - size.left, height = size.bottom - size.top;
    RECT area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    wchar_t wide_title[256];
    widen(title, wide_title, 256);
    const HWND window = CreateWindowExW(0, kind.lpszClassName, wide_title, style,
                                        area.left + (area.right - area.left - width) / 2,
                                        area.top + (area.bottom - area.top - height) / 2, width, height, NULL, NULL,
                                        instance, NULL);
    if (!window) return -1;
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DeleteObject(ui.heading_font);
    DeleteObject(ui.body_font);
    return ui.answer;
}
