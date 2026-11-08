/* otd-xp: minimal OpenTabletDriver-style tablet driver for Windows XP (32-bit) and newer.
 * Reads the tablet through the HID class driver (hid.dll / setupapi.dll) and moves the
 * cursor with SendInput. Everything is loaded from XP-era DLLs only. */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WINVER
#define WINVER 0x0501
#endif

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"

#ifndef MOUSEEVENTF_VIRTUALDESK
#define MOUSEEVENTF_VIRTUALDESK 0x4000
#endif
#ifndef SM_XVIRTUALSCREEN
#define SM_XVIRTUALSCREEN 76
#define SM_YVIRTUALSCREEN 77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#endif

#define VERSION_TEXT "otd-xp 0.1 (tablet database from OpenTabletDriver 0.6.4)"

/* ---------------------------------------------------------------- hid.dll */

typedef struct { ULONG Size; USHORT VendorID; USHORT ProductID; USHORT VersionNumber; } HidAttr;

typedef struct {
    USHORT Usage, UsagePage;
    USHORT InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
    USHORT Reserved[17];
    USHORT NumberLinkCollectionNodes;
    USHORT NumberInputButtonCaps, NumberInputValueCaps, NumberInputDataIndices;
    USHORT NumberOutputButtonCaps, NumberOutputValueCaps, NumberOutputDataIndices;
    USHORT NumberFeatureButtonCaps, NumberFeatureValueCaps, NumberFeatureDataIndices;
} HidCaps;

typedef void    (WINAPI *PFN_GetHidGuid)(GUID *);
typedef BOOLEAN (WINAPI *PFN_GetAttributes)(HANDLE, HidAttr *);
typedef BOOLEAN (WINAPI *PFN_GetPreparsed)(HANDLE, void **);
typedef BOOLEAN (WINAPI *PFN_FreePreparsed)(void *);
typedef LONG    (WINAPI *PFN_GetCaps)(void *, HidCaps *);
typedef BOOLEAN (WINAPI *PFN_SetFeature)(HANDLE, void *, ULONG);
typedef BOOLEAN (WINAPI *PFN_GetIndexedString)(HANDLE, ULONG, void *, ULONG);
typedef BOOLEAN (WINAPI *PFN_GetProductString)(HANDLE, void *, ULONG);

static PFN_GetHidGuid        pHidGuid;
static PFN_GetAttributes     pAttributes;
static PFN_GetPreparsed      pPreparsed;
static PFN_FreePreparsed     pFreePreparsed;
static PFN_GetCaps           pCaps;
static PFN_SetFeature        pSetFeature;
static PFN_GetIndexedString  pIndexedString;
static PFN_GetProductString  pProductString;

static int load_hid(void)
{
    HMODULE h = LoadLibraryA("hid.dll");
    if (!h) return 0;
    pHidGuid       = (PFN_GetHidGuid)GetProcAddress(h, "HidD_GetHidGuid");
    pAttributes    = (PFN_GetAttributes)GetProcAddress(h, "HidD_GetAttributes");
    pPreparsed     = (PFN_GetPreparsed)GetProcAddress(h, "HidD_GetPreparsedData");
    pFreePreparsed = (PFN_FreePreparsed)GetProcAddress(h, "HidD_FreePreparsedData");
    pCaps          = (PFN_GetCaps)GetProcAddress(h, "HidP_GetCaps");
    pSetFeature    = (PFN_SetFeature)GetProcAddress(h, "HidD_SetFeature");
    pIndexedString = (PFN_GetIndexedString)GetProcAddress(h, "HidD_GetIndexedString");
    pProductString = (PFN_GetProductString)GetProcAddress(h, "HidD_GetProductString");
    return pHidGuid && pAttributes && pPreparsed && pFreePreparsed && pCaps && pSetFeature;
}

/* ------------------------------------------------------------- enumeration */

typedef struct {
    char path[512];
    unsigned vid, pid, in_len, out_len, feat_len, usage_page, usage;
    char product[80];
} HidDev;

#define MAX_DEV 64

static int enum_hid(HidDev *out, int max)
{
    GUID guid;
    HDEVINFO info;
    DWORD i;
    int n = 0;

    pHidGuid(&guid);
    info = SetupDiGetClassDevsA(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE)
        return 0;

    for (i = 0; n < max; i++) {
        SP_DEVICE_INTERFACE_DATA did;
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *det;
        DWORD need = 0;
        HANDLE h;
        HidAttr attr;
        void *pre = NULL;
        HidCaps caps;
        HidDev *d = &out[n];

        memset(&did, 0, sizeof(did));
        did.cbSize = sizeof(did);
        if (!SetupDiEnumDeviceInterfaces(info, NULL, &guid, i, &did))
            break;
        SetupDiGetDeviceInterfaceDetailA(info, &did, NULL, 0, &need, NULL);
        if (need < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A))
            continue;
        det = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)malloc(need);
        if (!det)
            continue;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(info, &did, det, need, NULL, NULL)) {
            free(det);
            continue;
        }

        h = CreateFileA(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            memset(d, 0, sizeof(*d));
            strncpy(d->path, det->DevicePath, sizeof(d->path) - 1);
            attr.Size = sizeof(attr);
            if (pAttributes(h, &attr)) {
                d->vid = attr.VendorID;
                d->pid = attr.ProductID;
                if (pPreparsed(h, &pre)) {
                    memset(&caps, 0, sizeof(caps));
                    if (pCaps(pre, &caps) >= 0) {    /* NTSTATUS success */
                        d->in_len = caps.InputReportByteLength;
                        d->out_len = caps.OutputReportByteLength;
                        d->feat_len = caps.FeatureReportByteLength;
                        d->usage_page = caps.UsagePage;
                        d->usage = caps.Usage;
                    }
                    pFreePreparsed(pre);
                }
                if (pProductString) {
                    WCHAR w[64];
                    memset(w, 0, sizeof(w));
                    if (pProductString(h, w, sizeof(w)))
                        WideCharToMultiByte(CP_ACP, 0, w, -1, d->product, sizeof(d->product) - 1, NULL, NULL);
                }
                n++;
            }
            CloseHandle(h);
        }
        free(det);
    }
    SetupDiDestroyDeviceInfoList(info);
    return n;
}

static int ci_contains(const char *hay, const char *needle)
{
    size_t i, j, hl = strlen(hay), nl = strlen(needle);
    if (!nl) return 1;
    for (i = 0; i + nl <= hl; i++) {
        for (j = 0; j < nl; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (j == nl) return 1;
    }
    return 0;
}

static const TabletDef *match_tablet(const TabletDb *db, const HidDev *d, const Config *cfg)
{
    size_t i;
    for (i = 0; i < db->count; i++) {
        const TabletDef *t = &db->items[i];
        if (t->vid == d->vid && t->pid == d->pid && t->report_len == d->in_len &&
            ci_contains(t->name, cfg->tablet))
            return t;
    }
    return NULL;
}

/* ------------------------------------------------------------------ output */

#define BTN_LEFT   1
#define BTN_RIGHT  2
#define BTN_MIDDLE 4

static volatile int g_buttons;

static void send_mouse(long ax, long ay, int move, int old_btn, int new_btn)
{
    INPUT in;
    DWORD f = 0;
    memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE;
    if (move) {
        f |= MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        in.mi.dx = ax;
        in.mi.dy = ay;
    }
    if ((new_btn & BTN_LEFT)   && !(old_btn & BTN_LEFT))   f |= MOUSEEVENTF_LEFTDOWN;
    if (!(new_btn & BTN_LEFT)  && (old_btn & BTN_LEFT))    f |= MOUSEEVENTF_LEFTUP;
    if ((new_btn & BTN_RIGHT)  && !(old_btn & BTN_RIGHT))  f |= MOUSEEVENTF_RIGHTDOWN;
    if (!(new_btn & BTN_RIGHT) && (old_btn & BTN_RIGHT))   f |= MOUSEEVENTF_RIGHTUP;
    if ((new_btn & BTN_MIDDLE)  && !(old_btn & BTN_MIDDLE)) f |= MOUSEEVENTF_MIDDLEDOWN;
    if (!(new_btn & BTN_MIDDLE) && (old_btn & BTN_MIDDLE))  f |= MOUSEEVENTF_MIDDLEUP;
    if (!f)
        return;
    in.mi.dwFlags = f;
    SendInput(1, &in, sizeof(INPUT));
}

static BOOL WINAPI on_ctrl(DWORD ev)
{
    (void)ev;
    if (g_buttons) {
        send_mouse(0, 0, 0, g_buttons, 0);
        g_buttons = 0;
    }
    ExitProcess(0);
    return TRUE;
}

static int action_bit(ButtonAction a)
{
    switch (a) {
    case B_LEFT: return BTN_LEFT;
    case B_RIGHT: return BTN_RIGHT;
    case B_MIDDLE: return BTN_MIDDLE;
    default: return 0;
    }
}

/* ---------------------------------------------------------------- init/run */

static void init_device(HANDLE h, const HidDev *d, const TabletDef *t)
{
    int i;
    for (i = 0; i < t->string_count; i++) {
        WCHAR w[128];
        if (pIndexedString)
            pIndexedString(h, (ULONG)t->strings[i], w, sizeof(w));
    }
    for (i = 0; i < t->feature_count; i++) {
        unsigned char buf[MAX_INIT_LEN];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, t->feature[i], t->feature_len[i]);
        if (!pSetFeature(h, buf, (ULONG)t->feature_len[i])) {
            if ((int)d->feat_len > t->feature_len[i] && d->feat_len <= MAX_INIT_LEN &&
                pSetFeature(h, buf, d->feat_len))
                continue;
            printf("warning: feature init report %d failed (error %lu)\n", i, GetLastError());
        }
    }
    for (i = 0; i < t->output_count; i++) {
        unsigned char buf[MAX_INIT_LEN];
        DWORD wrote = 0;
        DWORD len = (DWORD)t->output_len[i];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, t->output[i], len);
        if (d->out_len > len && d->out_len <= MAX_INIT_LEN)
            len = d->out_len;
        if (!WriteFile(h, buf, len, &wrote, NULL))
            printf("warning: output init report %d failed (error %lu)\n", i, GetLastError());
    }
}

static HANDLE open_dev(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    return h;
}

static void screen_rect(const Config *cfg, int *x, int *y, int *w, int *h)
{
    if (cfg->screen_w > 0 && cfg->screen_h > 0) {
        *x = cfg->screen_x; *y = cfg->screen_y; *w = cfg->screen_w; *h = cfg->screen_h;
    } else {
        *x = 0; *y = 0; *w = GetSystemMetrics(SM_CXSCREEN); *h = GetSystemMetrics(SM_CYSCREEN);
    }
}

static long to_abs(double px, int origin, int size)
{
    double v = (px - origin) * 65535.0 / (size > 1 ? size - 1 : 1);
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    return (long)(v + 0.5);
}

static void exe_dir_path(const char *name, char *out, size_t cap);

/* tells otd-xp-gui which tablet is in use (name|width_mm|height_mm) */
static void write_status(const TabletDef *t)
{
    char p[MAX_PATH + 32];
    FILE *f;
    exe_dir_path("otd-xp.status", p, sizeof(p));
    f = fopen(p, "w");
    if (f) {
        fprintf(f, "%s|%g|%g\n", t->name, t->width_mm, t->height_mm);
        fclose(f);
    }
}

static void run_tablet(const HidDev *d, const TabletDef *t, const Config *cfg)
{
    HANDLE h = open_dev(d->path);
    unsigned char *buf;
    DWORD got;
    int sx, sy, sw, sh, vx, vy, vw, vh;
    long last_ax = -1, last_ay = -1;
    int cur_btn = 0;
    unsigned thr;

    if (h == INVALID_HANDLE_VALUE) {
        printf("cannot open %s (error %lu) - is another program using the tablet?\n", t->name, GetLastError());
        Sleep(2000);
        return;
    }
    buf = (unsigned char *)malloc(d->in_len ? d->in_len : 1);
    if (!buf) { CloseHandle(h); return; }

    init_device(h, d, t);
    write_status(t);
    screen_rect(cfg, &sx, &sy, &sw, &sh);
    vx = GetSystemMetrics(SM_XVIRTUALSCREEN); vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    vw = GetSystemMetrics(SM_CXVIRTUALSCREEN); vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) { vx = 0; vy = 0; vw = GetSystemMetrics(SM_CXSCREEN); vh = GetSystemMetrics(SM_CYSCREEN); }
    thr = (unsigned)(t->max_pressure * cfg->click_threshold / 100.0);
    if (thr < 1) thr = 1;

    printf("using %s (%04X:%04X, parser %s), screen %dx%d. Ctrl+C to quit.\n",
           t->name, t->vid, t->pid, t->parser_name, sw, sh);

    for (;;) {
        Pen pen;
        int r, want = 0, move = 0;
        long ax = last_ax, ay = last_ay;

        if (!ReadFile(h, buf, d->in_len, &got, NULL) || got == 0) {
            printf("tablet disconnected (error %lu)\n", GetLastError());
            break;
        }
        r = parse_report(t, buf, got, &pen);
        if (r == PR_IGNORE)
            continue;
        if (r == PR_TABLET) {
            Pos p = map_point(t, cfg, sh > 0 ? (double)sw / sh : 0, pen.x, pen.y);
            ax = to_abs(sx + p.u * (sw - 1), vx, vw);
            ay = to_abs(sy + p.v * (sh - 1), vy, vh);
            if (pen.pressure >= thr && (!pen.eraser || cfg->eraser_clicks))
                want |= BTN_LEFT;
            if (pen.btn1) want |= action_bit(cfg->button1);
            if (pen.btn2) want |= action_bit(cfg->button2);
            if (cfg->debug)
                printf("x=%u y=%u p=%u b1=%d b2=%d er=%d -> %ld,%ld btn=%d\n",
                       pen.x, pen.y, pen.pressure, pen.btn1, pen.btn2, pen.eraser, ax, ay, want);
        }
        move = (ax != last_ax || ay != last_ay) && r == PR_TABLET;
        if (move || want != cur_btn) {
            send_mouse(ax, ay, move, cur_btn, want);
            if (move) { last_ax = ax; last_ay = ay; }
            cur_btn = want;
            g_buttons = want;
        }
    }
    if (cur_btn) {
        send_mouse(0, 0, 0, cur_btn, 0);
        g_buttons = 0;
    }
    free(buf);
    CloseHandle(h);
}

/* ------------------------------------------------------------------- tools */

static void cmd_list(const TabletDb *db, const Config *cfg)
{
    HidDev dev[MAX_DEV];
    int n = enum_hid(dev, MAX_DEV), i;
    printf("%d HID interfaces found\n", n);
    for (i = 0; i < n; i++) {
        const TabletDef *t = match_tablet(db, &dev[i], cfg);
        printf("[%d] %04X:%04X in=%u out=%u feature=%u usage=%04X/%04X \"%s\"\n", i,
               dev[i].vid, dev[i].pid, dev[i].in_len, dev[i].out_len, dev[i].feat_len,
               dev[i].usage_page, dev[i].usage, dev[i].product);
        if (t)
            printf("      -> %s (%s)%s\n", t->name, t->parser_name,
                   t->parser == P_UNSUPPORTED ? "  ** parser not implemented in otd-xp **" : "");
        else {
            size_t k; int shown = 0;
            for (k = 0; k < db->count && !shown; k++)
                if (db->items[k].vid == dev[i].vid && db->items[k].pid == dev[i].pid) {
                    printf("      known tablet %s, but with report length %u (this interface has %u)\n",
                           db->items[k].name, db->items[k].report_len, dev[i].in_len);
                    shown = 1;
                }
        }
    }
}

static void cmd_dump(int index)
{
    HidDev dev[MAX_DEV];
    int n = enum_hid(dev, MAX_DEV);
    HANDLE h;
    unsigned char *buf;
    DWORD got;
    unsigned long count = 0;

    if (index < 0 || index >= n) { printf("no such interface, run --list first\n"); return; }
    h = open_dev(dev[index].path);
    if (h == INVALID_HANDLE_VALUE) { printf("cannot open (error %lu)\n", GetLastError()); return; }
    buf = (unsigned char *)malloc(dev[index].in_len ? dev[index].in_len : 1);
    printf("dumping %04X:%04X (%u byte reports). Move the pen over the tablet; Ctrl+C to stop.\n",
           dev[index].vid, dev[index].pid, dev[index].in_len);
    while (buf && ReadFile(h, buf, dev[index].in_len, &got, NULL) && got) {
        DWORD i;
        printf("%6lu:", ++count);
        for (i = 0; i < got; i++) printf(" %02X", buf[i]);
        printf("\n");
    }
    free(buf);
    CloseHandle(h);
}

static void exe_dir_path(const char *name, char *out, size_t cap)
{
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (n == 0 || n >= cap) { strncpy(out, name, cap - 1); out[cap - 1] = 0; return; }
    slash = strrchr(out, '\\');
    if (slash) slash[1] = 0; else out[0] = 0;
    if (strlen(out) + strlen(name) + 1 < cap) strcat(out, name);
}

static void usage(void)
{
    printf("%s\n\n"
           "  otd-xp.exe                run the driver (reads otd-xp.ini)\n"
           "  otd-xp.exe --list         list HID devices and which one is a known tablet\n"
           "  otd-xp.exe --dump N       print raw reports of interface N from --list\n"
           "  --config FILE             use another config file\n"
           "  --tablets FILE            use another tablet database\n", VERSION_TEXT);
}

int main(int argc, char **argv)
{
    char cfgpath[MAX_PATH + 16], dbpath[MAX_PATH + 16];
    Config cfg;
    TabletDb db;
    int i, do_list = 0, dump = -1;
    int cfgrc;

    exe_dir_path("otd-xp.ini", cfgpath, sizeof(cfgpath));
    exe_dir_path("tablets.txt", dbpath, sizeof(dbpath));
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list")) do_list = 1;
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) { strncpy(cfgpath, argv[++i], sizeof(cfgpath) - 1); cfgpath[sizeof(cfgpath) - 1] = 0; }
        else if (!strcmp(argv[i], "--tablets") && i + 1 < argc) { strncpy(dbpath, argv[++i], sizeof(dbpath) - 1); dbpath[sizeof(dbpath) - 1] = 0; }
        else { usage(); return 1; }
    }

    printf("%s\n", VERSION_TEXT);
    if (!load_hid()) {
        printf("cannot load hid.dll\n");
        return 1;
    }
    config_defaults(&cfg);
    cfgrc = config_load(&cfg, cfgpath);
    if (cfgrc == -1) printf("note: %s not found, using defaults\n", cfgpath);
    else if (cfgrc > 0) printf("warning: problem in %s near line %d\n", cfgpath, cfgrc);

    if (dump >= 0) { cmd_dump(dump); return 0; }

    if (tabletdb_load(&db, dbpath) != 0) {
        printf("cannot read tablet database %s\n", dbpath);
        return 1;
    }
    if (do_list) { cmd_list(&db, &cfg); return 0; }

    {
        HANDLE mtx = CreateMutexA(NULL, TRUE, "otd-xp-driver-instance");
        if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
            printf("otd-xp is already running.\n");
            return 1;
        }
    }
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    {
        int announced = 0;
        for (;;) {
            HidDev dev[MAX_DEV];
            int n = enum_hid(dev, MAX_DEV), k, ran = 0;
            for (k = 0; k < n && !ran; k++) {
                const TabletDef *t = match_tablet(&db, &dev[k], &cfg);
                if (t && t->parser != P_UNSUPPORTED) {
                    run_tablet(&dev[k], t, &cfg);
                    ran = 1;
                    announced = 0;
                    Sleep(1000);
                } else if (t && !announced) {
                    printf("found %s but its report format (%s) is not implemented\n", t->name, t->parser_name);
                    announced = 1;
                }
            }
            if (!ran) {
                if (!announced) {
                    printf("no supported tablet found, waiting... (run with --list to see what Windows sees)\n");
                    announced = 1;
                }
                Sleep(1500);
            }
        }
    }
    return 0;
}
