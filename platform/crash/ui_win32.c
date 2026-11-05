// tide-crash's window on Windows (crash.h): drawn with GDI, a heading, a
// status line, the report's preview in a read-only box (an EDIT control, so
// it scrolls, selects and copies as any), and two flat buttons, so it looks
// the same on every Windows and needs nothing but user32 and gdi32. Enter
// shows the report, Escape closes.
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

enum { STATUS_TIMER = 1 };

static struct {
    wchar_t heading[512], body[1024], show[64];
    button buttons[2]; // Show report, Close
    int hover;         // A button under the pointer, or -1
    int answer;
    int dpi;
    HFONT heading_font, body_font, preview_font;
    HWND preview;
    HBRUSH preview_brush;
    RECT text; // Where the heading and the status line go
    crash_status status;
    void *user;
} ui = {.hover = -1};

static int px(const int logical)
{
    return MulDiv(logical, ui.dpi, 96);
}

static HFONT make_font(const int size, const int weight, const wchar_t *face)
{
    return CreateFontW(-px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

// How tall `text` is in `font`, `width` wide
static int measure(const HWND window, const HFONT font, const wchar_t *text, const int width)
{
    const HDC dc = GetDC(window);
    SelectObject(dc, font);
    RECT r = {0, 0, width, 0};
    DrawTextW(dc, text, -1, &r, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    ReleaseDC(window, dc);
    return r.bottom;
}

static void layout(const HWND window)
{
    RECT client;
    GetClientRect(window, &client);
    const int height = px(32), gap = px(8), pad = px(24);
    const int widths[2] = {px(124), px(88)};
    int right = client.right - pad;
    for (int i = 1; i >= 0; i--) {
        ui.buttons[i].rect = (RECT){right - widths[i], client.bottom - pad - height, right, client.bottom - pad};
        right -= widths[i] + gap;
    }
    const int width = client.right - 2 * pad;
    const int heading = measure(window, ui.heading_font, ui.heading, width);
    const int body = measure(window, ui.body_font, ui.body, width);
    ui.text = (RECT){pad, pad, client.right - pad, pad + heading + px(8) + body};
    const int top = ui.text.bottom + px(14), bottom = ui.buttons[0].rect.top - px(16);
    if (ui.preview) MoveWindow(ui.preview, pad, top, width, bottom > top ? bottom - top : 0, TRUE);
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
    RECT text = ui.text;
    SelectObject(dc, ui.heading_font);
    SetTextColor(dc, RGB(28, 28, 30));
    RECT measured = text;
    DrawTextW(dc, ui.heading, -1, &measured, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    DrawTextW(dc, ui.heading, -1, &text, DT_WORDBREAK | DT_NOPREFIX);
    text.top = measured.bottom + px(8);
    SelectObject(dc, ui.body_font);
    SetTextColor(dc, RGB(80, 80, 86));
    DrawTextW(dc, ui.body, -1, &text, DT_WORDBREAK | DT_NOPREFIX);
    if (ui.preview) { // A line around the preview
        RECT box;
        GetWindowRect(ui.preview, &box);
        MapWindowPoints(NULL, window, (POINT *)&box, 2);
        InflateRect(&box, 1, 1);
        const HBRUSH line = CreateSolidBrush(RGB(220, 220, 226));
        FrameRect(dc, &box, line);
        DeleteObject(line);
    }
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
        SelectObject(dc, ui.body_font);
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

static void widen(const char *text, wchar_t *out, const int size)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, out, size)) out[0] = L'\0';
}

static void poll_status(const HWND window)
{
    if (!ui.status) return;
    bool settled = false;
    const char *line = ui.status(ui.user, &settled);
    if (line) {
        widen(line, ui.body, 1024);
        layout(window);
        InvalidateRect(window, NULL, FALSE);
    }
    if (settled) {
        KillTimer(window, STATUS_TIMER);
        ui.status = NULL;
    }
}

static LRESULT CALLBACK on_message(const HWND window, const UINT message, const WPARAM w, const LPARAM l)
{
    switch (message) {
    case WM_SIZE: layout(window); return 0;
    case WM_PAINT: paint(window); return 0;
    case WM_TIMER: poll_status(window); return 0;
    case WM_CTLCOLORSTATIC: // The read-only preview's colors
        SetBkColor((HDC)w, RGB(247, 247, 249));
        SetTextColor((HDC)w, RGB(40, 40, 46));
        return (LRESULT)ui.preview_brush;
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
        if ((HWND)w == window && LOWORD(l) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, ui.hover >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP: {
        const int at = button_at(l);
        if (at >= 0) answer(window, at == 0 ? 1 : 0);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(window, message, w, l);
}

// The preview with Windows' line ends, which an EDIT control needs
static wchar_t *preview_text(const char *text)
{
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wide = calloc((size_t)(size > 0 ? size : 1), sizeof *wide);
    wchar_t *out = calloc((size_t)(size > 0 ? size : 1) * 2u, sizeof *out);
    if (!wide || !out) {
        free(wide);
        free(out);
        return NULL;
    }
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, size);
    size_t n = 0;
    for (const wchar_t *c = wide; *c; c++) {
        if (*c == L'\n' && (c == wide || c[-1] != L'\r')) out[n++] = L'\r';
        out[n++] = *c;
    }
    out[n] = L'\0';
    free(wide);
    return out;
}

int crash_show(const crash_view *view, const crash_status status, void *user)
{
    SetProcessDPIAware();
    const HDC screen = GetDC(NULL);
    ui.dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen) ReleaseDC(NULL, screen);
    if (ui.dpi <= 0) ui.dpi = 96;
    widen(view->heading, ui.heading, 512);
    widen("", ui.body, 1024);
    widen(view->show, ui.show, 64);
    ui.status = status;
    ui.user = user;
    if (status) {
        bool settled = false;
        const char *line = status(user, &settled);
        if (line) widen(line, ui.body, 1024);
        if (settled) ui.status = NULL;
    }
    ui.buttons[0] = (button){.label = ui.show, .primary = true};
    ui.buttons[1] = (button){.label = L"Close"};
    ui.heading_font = make_font(17, FW_SEMIBOLD, L"Segoe UI");
    ui.body_font = make_font(14, FW_NORMAL, L"Segoe UI");
    ui.preview_font = make_font(13, FW_NORMAL, L"Consolas");
    ui.preview_brush = CreateSolidBrush(RGB(247, 247, 249));
    const HINSTANCE instance = GetModuleHandleW(NULL);
    const WNDCLASSW kind = {.lpfnWndProc = on_message,
                            .hInstance = instance,
                            .hCursor = LoadCursorW(NULL, IDC_ARROW),
                            .hIcon = LoadIconW(NULL, IDI_ERROR),
                            .lpszClassName = L"TideCrash"};
    if (!RegisterClassW(&kind)) return -1;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    RECT size = {0, 0, px(640), px(460)};
    AdjustWindowRect(&size, style, FALSE);
    const int width = size.right - size.left, height = size.bottom - size.top;
    RECT area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    wchar_t wide_title[256];
    widen(view->title, wide_title, 256);
    const HWND window = CreateWindowExW(0, kind.lpszClassName, wide_title, style,
                                        area.left + (area.right - area.left - width) / 2,
                                        area.top + (area.bottom - area.top - height) / 2, width, height, NULL, NULL,
                                        instance, NULL);
    if (!window) return -1;
    ui.preview = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY
                                     | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                 0, 0, 0, 0, window, NULL, instance, NULL);
    if (ui.preview) {
        SendMessageW(ui.preview, WM_SETFONT, (WPARAM)ui.preview_font, TRUE);
        SendMessageW(ui.preview, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(px(8), px(8)));
        wchar_t *text = preview_text(view->preview ? view->preview : "");
        if (text) SetWindowTextW(ui.preview, text);
        free(text);
    }
    layout(window);
    if (ui.status) SetTimer(window, STATUS_TIMER, 100, NULL);
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        // Enter and Escape answer wherever the focus is, the preview's too
        if (message.message == WM_KEYDOWN && (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE)) {
            answer(window, message.wParam == VK_RETURN ? 1 : 0);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DeleteObject(ui.heading_font);
    DeleteObject(ui.body_font);
    DeleteObject(ui.preview_font);
    DeleteObject(ui.preview_brush);
    return ui.answer;
}
