/* otd-xp-gui: small settings window for otd-xp (plain Win32, works on Windows XP).
 * Edits otd-xp.ini, shows the tablet area (drag it with the mouse) and starts/stops otd-xp.exe. */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WINVER
#define WINVER 0x0501
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"

/* control ids */
enum {
    ID_W = 100, ID_H, ID_CX, ID_CY, ID_ROT, ID_LOCK, ID_THR, ID_B1, ID_B2, ID_ERASER,
    ID_FULL, ID_SAVE, ID_APPLY, ID_STARTSTOP, ID_STATUS, ID_PREVIEW
};
#define TIMER_ID 1

static HWND g_main, g_preview;
static HWND c_w, c_h, c_cx, c_cy, c_rot, c_lock, c_thr, c_b1, c_b2, c_eraser, c_startstop, c_status;
static HFONT g_font;

static Config g_cfg;
static char g_ini[MAX_PATH + 32];
static char g_driver[MAX_PATH + 32];
static char g_status_file[MAX_PATH + 32];
static char g_dir[MAX_PATH + 32];

static char g_tab_name[96];
static double g_tab_w = 160, g_tab_h = 100;
static int g_tab_known;

static PROCESS_INFORMATION g_pi;
static int g_running;
static DWORD g_started_at;
static int g_ignore_change;
static int g_dragging;

/* ------------------------------------------------------------------ helpers */

static void path_in_exe_dir(const char *name, char *out, size_t cap)
{
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (n == 0 || n >= cap) { strncpy(out, name, cap - 1); out[cap - 1] = 0; return; }
    slash = strrchr(out, '\\');
    if (slash) slash[1] = 0; else out[0] = 0;
    strncpy(g_dir, out, sizeof(g_dir) - 1);
    g_dir[sizeof(g_dir) - 1] = 0;
    if (strlen(out) + strlen(name) + 1 < cap) strcat(out, name);
}

static HWND make(const char *cls, const char *text, DWORD style, DWORD ex,
                 int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                             g_main, (HMENU)(UINT_PTR)id, GetModuleHandleA(NULL), NULL);
    if (c && g_font)
        SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static HWND label(const char *text, int x, int y, int w)
{
    return make("STATIC", text, 0, 0, x, y + 3, w, 18, -1);
}

static HWND edit(int x, int y, int w, int id)
{
    return make("EDIT", "", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, x, y, w, 22, id);
}

static double get_num(HWND h)
{
    char buf[64];
    GetWindowTextA(h, buf, sizeof(buf));
    return atof(buf);
}

static void set_num(HWND h, double v)
{
    char buf[64];
    sprintf(buf, "%g", v);
    SetWindowTextA(h, buf);
}

static int action_to_index(ButtonAction a)   /* combo order: none, left, right, middle */
{
    return (int)a;
}

static ButtonAction index_to_action(int i)
{
    if (i == 1) return B_LEFT;
    if (i == 2) return B_RIGHT;
    if (i == 3) return B_MIDDLE;
    return B_NONE;
}

static const char *action_name(ButtonAction a)
{
    switch (a) {
    case B_LEFT: return "left";
    case B_RIGHT: return "right";
    case B_MIDDLE: return "middle";
    default: return "none";
    }
}

/* UI <-> config */

static void cfg_to_ui(void)
{
    int swap = (g_cfg.rotation == 90 || g_cfg.rotation == 270);
    double w = g_cfg.area_w, h = g_cfg.area_h, cx = g_cfg.area_cx, cy = g_cfg.area_cy;
    if (g_tab_known) {      /* show real numbers instead of "0 = whole tablet" / "-1 = centre" */
        if (w <= 0) w = swap ? g_tab_h : g_tab_w;
        if (h <= 0) h = swap ? g_tab_w : g_tab_h;
        if (cx < 0) cx = g_tab_w / 2;
        if (cy < 0) cy = g_tab_h / 2;
    }
    g_ignore_change = 1;
    set_num(c_w, w);
    set_num(c_h, h);
    set_num(c_cx, cx);
    set_num(c_cy, cy);
    SendMessageA(c_rot, CB_SETCURSEL, (WPARAM)(g_cfg.rotation / 90), 0);
    SendMessageA(c_lock, BM_SETCHECK, g_cfg.lock_aspect ? BST_CHECKED : BST_UNCHECKED, 0);
    set_num(c_thr, g_cfg.click_threshold);
    SendMessageA(c_b1, CB_SETCURSEL, (WPARAM)action_to_index(g_cfg.button1), 0);
    SendMessageA(c_b2, CB_SETCURSEL, (WPARAM)action_to_index(g_cfg.button2), 0);
    SendMessageA(c_eraser, BM_SETCHECK, g_cfg.eraser_clicks ? BST_CHECKED : BST_UNCHECKED, 0);
    g_ignore_change = 0;
}

static void ui_to_cfg(void)
{
    int r;
    g_cfg.area_w = get_num(c_w);
    g_cfg.area_h = get_num(c_h);
    g_cfg.area_cx = get_num(c_cx);
    g_cfg.area_cy = get_num(c_cy);
    r = (int)SendMessageA(c_rot, CB_GETCURSEL, 0, 0);
    g_cfg.rotation = (r >= 0 && r <= 3) ? r * 90 : 0;
    g_cfg.lock_aspect = SendMessageA(c_lock, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_cfg.click_threshold = get_num(c_thr);
    if (g_cfg.click_threshold < 0) g_cfg.click_threshold = 0;
    g_cfg.button1 = index_to_action((int)SendMessageA(c_b1, CB_GETCURSEL, 0, 0));
    g_cfg.button2 = index_to_action((int)SendMessageA(c_b2, CB_GETCURSEL, 0, 0));
    g_cfg.eraser_clicks = SendMessageA(c_eraser, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static int save_config(void)
{
    FILE *f;
    ui_to_cfg();
    f = fopen(g_ini, "w");
    if (!f) {
        MessageBoxA(g_main, "Could not write otd-xp.ini (is the folder read-only?).", "otd-xp", MB_OK | MB_ICONERROR);
        return 0;
    }
    fprintf(f, "# otd-xp configuration (written by otd-xp-gui)\n");
    fprintf(f, "tablet = %s\n", g_cfg.tablet);
    fprintf(f, "area_width_mm = %g\n", g_cfg.area_w);
    fprintf(f, "area_height_mm = %g\n", g_cfg.area_h);
    fprintf(f, "area_center_x_mm = %g\n", g_cfg.area_cx);
    fprintf(f, "area_center_y_mm = %g\n", g_cfg.area_cy);
    fprintf(f, "lock_aspect = %d\n", g_cfg.lock_aspect);
    fprintf(f, "rotation = %d\n", g_cfg.rotation);
    fprintf(f, "screen_x = %d\n", g_cfg.screen_x);
    fprintf(f, "screen_y = %d\n", g_cfg.screen_y);
    fprintf(f, "screen_width = %d\n", g_cfg.screen_w);
    fprintf(f, "screen_height = %d\n", g_cfg.screen_h);
    fprintf(f, "click_threshold = %g\n", g_cfg.click_threshold);
    fprintf(f, "button1 = %s\n", action_name(g_cfg.button1));
    fprintf(f, "button2 = %s\n", action_name(g_cfg.button2));
    fprintf(f, "eraser_clicks = %d\n", g_cfg.eraser_clicks);
    fprintf(f, "debug = %d\n", g_cfg.debug);
    fclose(f);
    return 1;
}

static void set_status(const char *text)
{
    SetWindowTextA(c_status, text);
}

/* --------------------------------------------------------------- driver proc */

static void release_buttons(void)
{
    INPUT in;
    memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_RIGHTUP | MOUSEEVENTF_MIDDLEUP;
    SendInput(1, &in, sizeof(INPUT));
}

static void cfg_to_ui(void);
static void ui_to_cfg(void);

static void read_status_file(int refill)
{
    FILE *f = fopen(g_status_file, "r");
    char line[256];
    if (!f) return;
    if (fgets(line, sizeof(line), f)) {
        char *a = strchr(line, '|');
        char *b = a ? strchr(a + 1, '|') : NULL;
        if (a && b) {
            double w, h;
            *a = 0;
            w = atof(a + 1);
            h = atof(b + 1);
            if (w > 0 && h > 0) {
                if (strcmp(g_tab_name, line) != 0 || w != g_tab_w || h != g_tab_h) {
                    strncpy(g_tab_name, line, sizeof(g_tab_name) - 1);
                    g_tab_name[sizeof(g_tab_name) - 1] = 0;
                    g_tab_w = w;
                    g_tab_h = h;
                    g_tab_known = 1;
                    if (refill) {           /* keep what the user typed, but fill in real defaults */
                        ui_to_cfg();
                        cfg_to_ui();
                    }
                    InvalidateRect(g_preview, NULL, TRUE);
                }
            }
        }
    }
    fclose(f);
}

static void update_buttons(void)
{
    SetWindowTextA(c_startstop, g_running ? "Stop driver" : "Start driver");
}

static void stop_driver(void)
{
    if (!g_running) return;
    TerminateProcess(g_pi.hProcess, 0);
    WaitForSingleObject(g_pi.hProcess, 2000);
    CloseHandle(g_pi.hProcess);
    g_running = 0;
    release_buttons();
    update_buttons();
    set_status("Driver stopped.");
}

static void start_driver(void)
{
    STARTUPINFOA si;
    char cmd[MAX_PATH + 64];
    if (g_running) return;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&g_pi, 0, sizeof(g_pi));
    sprintf(cmd, "\"%s\"", g_driver);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_dir, &si, &g_pi)) {
        MessageBoxA(g_main, "Could not start otd-xp.exe. Is it in the same folder as this program?",
                    "otd-xp", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(g_pi.hThread);
    g_running = 1;
    g_started_at = GetTickCount();
    update_buttons();
    set_status("Driver started - waiting for the tablet...");
}

static void poll_driver(void)
{
    if (g_running) {
        DWORD code = 0;
        if (GetExitCodeProcess(g_pi.hProcess, &code) && code != STILL_ACTIVE) {
            char msg[160];
            CloseHandle(g_pi.hProcess);
            g_running = 0;
            update_buttons();
            sprintf(msg, "Driver exited (code %lu). Is another copy already running?", (unsigned long)code);
            set_status(msg);
        } else {
            char msg[200];
            read_status_file(1);
            if (g_tab_known) sprintf(msg, "Driver running. Tablet: %s", g_tab_name);
            else strcpy(msg, "Driver running - waiting for the tablet...");
            set_status(msg);
        }
    }
}

/* ------------------------------------------------------------------- preview */

static void preview_geometry(const RECT *rc, double *scale, double *ox, double *oy)
{
    double pw = rc->right - rc->left - 24.0, ph = rc->bottom - rc->top - 24.0;
    double sx = pw / g_tab_w, sy = ph / g_tab_h;
    *scale = sx < sy ? sx : sy;
    *ox = rc->left + ((rc->right - rc->left) - g_tab_w * *scale) / 2.0;
    *oy = rc->top + ((rc->bottom - rc->top) - g_tab_h * *scale) / 2.0;
}

static void screen_aspect_cfg(double *aspect)
{
    int w = g_cfg.screen_w > 0 ? g_cfg.screen_w : GetSystemMetrics(SM_CXSCREEN);
    int h = g_cfg.screen_h > 0 ? g_cfg.screen_h : GetSystemMetrics(SM_CYSCREEN);
    *aspect = h > 0 ? (double)w / h : 0;
}

static void paint_preview(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc, a;
    double scale, ox, oy, aspect, aw, ah, cx, cy, tw, th;
    TabletDef t;
    HBRUSH bg = CreateSolidBrush(RGB(236, 236, 236));
    HBRUSH tab = CreateSolidBrush(RGB(60, 60, 60));
    HBRUSH area = CreateSolidBrush(RGB(70, 140, 220));
    HBRUSH edge = CreateSolidBrush(RGB(255, 190, 0));
    HBRUSH outline = CreateSolidBrush(RGB(0, 0, 0));
    char buf[160];
    int swap;

    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, bg);
    preview_geometry(&rc, &scale, &ox, &oy);

    /* tablet */
    a.left = (LONG)ox; a.top = (LONG)oy;
    a.right = (LONG)(ox + g_tab_w * scale); a.bottom = (LONG)(oy + g_tab_h * scale);
    FillRect(dc, &a, tab);
    FrameRect(dc, &a, outline);

    /* active area (same math the driver uses) */
    ui_to_cfg();
    memset(&t, 0, sizeof(t));
    t.width_mm = g_tab_w; t.height_mm = g_tab_h;
    screen_aspect_cfg(&aspect);
    effective_area(&t, &g_cfg, aspect, &aw, &ah, &cx, &cy);
    swap = (g_cfg.rotation == 90 || g_cfg.rotation == 270);
    tw = swap ? ah : aw;      /* size in tablet orientation */
    th = swap ? aw : ah;
    a.left = (LONG)(ox + (cx - tw / 2) * scale);
    a.right = (LONG)(ox + (cx + tw / 2) * scale);
    a.top = (LONG)(oy + (cy - th / 2) * scale);
    a.bottom = (LONG)(oy + (cy + th / 2) * scale);
    FillRect(dc, &a, area);
    FrameRect(dc, &a, outline);

    /* the edge that is the top of the screen */
    {
        RECT e = a;
        switch (g_cfg.rotation) {
        case 90:  e.right = e.left + 4; break;
        case 180: e.top = e.bottom - 4; break;
        case 270: e.left = e.right - 4; break;
        default:  e.bottom = e.top + 4; break;
        }
        FillRect(dc, &e, edge);
    }

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_font);
    SetTextColor(dc, RGB(0, 0, 0));
    sprintf(buf, "%s  (%g x %g mm)%s", g_tab_known ? g_tab_name : "Tablet size unknown",
            g_tab_w, g_tab_h, g_tab_known ? "" : " - start the driver once");
    TextOutA(dc, rc.left + 6, rc.top + 4, buf, (int)strlen(buf));
    SetTextColor(dc, RGB(90, 90, 90));
    strcpy(buf, "Drag in the picture to move the area. Yellow edge = top of the screen.");
    TextOutA(dc, rc.left + 6, rc.bottom - 18, buf, (int)strlen(buf));

    DeleteObject(bg); DeleteObject(tab); DeleteObject(area); DeleteObject(edge); DeleteObject(outline);
    EndPaint(hwnd, &ps);
}

static void drag_to(int mx, int my)
{
    RECT rc;
    double scale, ox, oy, x, y;
    GetClientRect(g_preview, &rc);
    preview_geometry(&rc, &scale, &ox, &oy);
    if (scale <= 0 || !g_tab_known) return;
    x = (mx - ox) / scale;
    y = (my - oy) / scale;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > g_tab_w) x = g_tab_w;
    if (y > g_tab_h) y = g_tab_h;
    g_ignore_change = 1;
    set_num(c_cx, (double)((long)(x * 100 + 0.5)) / 100.0);
    set_num(c_cy, (double)((long)(y * 100 + 0.5)) / 100.0);
    g_ignore_change = 0;
    InvalidateRect(g_preview, NULL, FALSE);
}

static LRESULT CALLBACK preview_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        paint_preview(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        g_dragging = 1;
        SetCapture(hwnd);
        drag_to((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging) drag_to((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) { g_dragging = 0; ReleaseCapture(); }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* --------------------------------------------------------------- main window */

static void full_tablet(void)
{
    int swap;
    if (!g_tab_known) {
        MessageBoxA(g_main, "The tablet size is not known yet.\nStart the driver with the tablet plugged in first.",
                    "otd-xp", MB_OK | MB_ICONERROR);
        return;
    }
    ui_to_cfg();
    swap = (g_cfg.rotation == 90 || g_cfg.rotation == 270);
    g_ignore_change = 1;
    set_num(c_w, swap ? g_tab_h : g_tab_w);
    set_num(c_h, swap ? g_tab_w : g_tab_h);
    set_num(c_cx, g_tab_w / 2);
    set_num(c_cy, g_tab_h / 2);
    g_ignore_change = 0;
    InvalidateRect(g_preview, NULL, FALSE);
}

static void add_combo_items(HWND c, const char **items, int n)
{
    int i;
    for (i = 0; i < n; i++)
        SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
}

static void create_controls(void)
{
    static const char *rots[] = { "0", "90", "180", "270" };
    static const char *acts[] = { "nothing", "left click", "right click", "middle click" };
    int y = 14, x1 = 14, x2 = 150, dy = 30;

    label("Area width (mm)", x1, y, 130);  c_w  = edit(x2, y, 90, ID_W);  y += dy;
    label("Area height (mm)", x1, y, 130); c_h  = edit(x2, y, 90, ID_H);  y += dy;
    label("Centre X (mm)", x1, y, 130);    c_cx = edit(x2, y, 90, ID_CX); y += dy;
    label("Centre Y (mm)", x1, y, 130);    c_cy = edit(x2, y, 90, ID_CY); y += dy;
    label("Rotation (degrees)", x1, y, 130);
    c_rot = make("COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, x2, y, 90, 120, ID_ROT);
    add_combo_items(c_rot, rots, 4);
    y += dy;
    c_lock = make("BUTTON", "Keep screen proportions", WS_TABSTOP | BS_AUTOCHECKBOX, 0, x1, y, 230, 20, ID_LOCK);
    y += dy;
    label("Click pressure (%)", x1, y, 130); c_thr = edit(x2, y, 90, ID_THR); y += dy;
    label("Pen button 1", x1, y, 130);
    c_b1 = make("COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, x2, y, 110, 120, ID_B1);
    add_combo_items(c_b1, acts, 4);
    y += dy;
    label("Pen button 2", x1, y, 130);
    c_b2 = make("COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, x2, y, 110, 120, ID_B2);
    add_combo_items(c_b2, acts, 4);
    y += dy;
    c_eraser = make("BUTTON", "Eraser end also clicks", WS_TABSTOP | BS_AUTOCHECKBOX, 0, x1, y, 230, 20, ID_ERASER);
    y += dy;
    make("BUTTON", "Use whole tablet", WS_TABSTOP | BS_PUSHBUTTON, 0, x1, y, 226, 26, ID_FULL);

    g_preview = CreateWindowExA(WS_EX_CLIENTEDGE, "OtdXpPreview", "", WS_CHILD | WS_VISIBLE,
                                276, 14, 370, 300, g_main, (HMENU)(UINT_PTR)ID_PREVIEW,
                                GetModuleHandleA(NULL), NULL);

    make("BUTTON", "Save", WS_TABSTOP | BS_PUSHBUTTON, 0, 276, 328, 90, 28, ID_SAVE);
    make("BUTTON", "Save && restart driver", WS_TABSTOP | BS_PUSHBUTTON, 0, 372, 328, 150, 28, ID_APPLY);
    c_startstop = make("BUTTON", "Start driver", WS_TABSTOP | BS_PUSHBUTTON, 0, 528, 328, 118, 28, ID_STARTSTOP);
    c_status = make("STATIC", "", 0, 0, 14, 372, 632, 18, ID_STATUS);
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_main = hwnd;
        create_controls();
        read_status_file(0);
        cfg_to_ui();
        update_buttons();
        SetTimer(hwnd, TIMER_ID, 700, NULL);
        PostMessageA(hwnd, WM_USER + 1, 0, 0);     /* auto-start once the window is up */
        return 0;

    case WM_USER + 1:
        start_driver();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == ID_PREVIEW)
            return 0;
        if (code == EN_CHANGE || code == CBN_SELCHANGE || (code == BN_CLICKED && (id == ID_LOCK))) {
            if (!g_ignore_change) InvalidateRect(g_preview, NULL, FALSE);
            if (code != BN_CLICKED) return 0;
        }
        switch (id) {
        case ID_FULL: full_tablet(); break;
        case ID_SAVE:
            if (save_config()) set_status("Saved. Use \"Save && restart driver\" to apply it now.");
            break;
        case ID_APPLY:
            if (save_config()) {
                stop_driver();
                start_driver();
            }
            break;
        case ID_STARTSTOP:
            if (g_running) stop_driver(); else start_driver();
            break;
        }
        return 0;
    }

    case WM_TIMER:
        poll_driver();
        return 0;

    case WM_CLOSE:
        if (g_running && MessageBoxA(hwnd, "The driver only runs while this window is open.\nClose it and stop the driver?",
                                     "otd-xp", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
        break;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        stop_driver();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    RECT r = { 0, 0, 660, 400 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    HWND wnd;
    MSG m;
    (void)prev; (void)cmd;

    path_in_exe_dir("otd-xp.ini", g_ini, sizeof(g_ini));
    path_in_exe_dir("otd-xp.exe", g_driver, sizeof(g_driver));
    path_in_exe_dir("otd-xp.status", g_status_file, sizeof(g_status_file));
    config_defaults(&g_cfg);
    config_load(&g_cfg, g_ini);

    g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = preview_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, IDC_CROSS);
    wc.lpszClassName = "OtdXpPreview";
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "OtdXpMain";
    RegisterClassA(&wc);

    AdjustWindowRect(&r, style, FALSE);
    wnd = CreateWindowExA(0, "OtdXpMain", "otd-xp tablet settings", style,
                          CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                          NULL, NULL, inst, NULL);
    if (!wnd) return 1;
    ShowWindow(wnd, show);
    UpdateWindow(wnd);

    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
    return (int)m.wParam;
}
