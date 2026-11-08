/* otd-xp core: platform independent part. See core.h. */
#include "core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIT(b, i) (((b) >> (i)) & 1)
#define U16(p) ((unsigned)(p)[0] | ((unsigned)(p)[1] << 8))

/* ------------------------------------------------------------------ parsers */

static int fill_std(const unsigned char *d, Pen *pen)
{
    pen->x = U16(d + 2);
    pen->y = U16(d + 4);
    pen->pressure = U16(d + 6);
    pen->btn1 = BIT(d[1], 1);
    pen->btn2 = BIT(d[1], 2);
    pen->eraser = 0;
    return PR_TABLET;
}

/* Wacom IntuosV1 / Intuos3 pen report layout */
static int fill_v1(const unsigned char *d, Pen *pen)
{
    pen->x = (((unsigned)d[3] | ((unsigned)d[2] << 8)) << 1) | ((d[9] >> 1) & 1);
    pen->y = (((unsigned)d[5] | ((unsigned)d[4] << 8)) << 1) | (d[9] & 1);
    pen->pressure = ((unsigned)d[6] << 3) | ((d[7] & 0xC0) >> 5) | (d[1] & 1);
    pen->btn1 = BIT(d[1], 1);
    pen->btn2 = BIT(d[1], 2);
    pen->eraser = 0;
    return BIT(d[1], 6) ? PR_TABLET : PR_OUT;   /* bit 6 = near proximity */
}

int parse_report(const TabletDef *t, const unsigned char *data, size_t len, Pen *pen)
{
    const unsigned char *d;
    size_t n;

    memset(pen, 0, sizeof(*pen));
    if (len <= (size_t)t->skip)
        return PR_IGNORE;
    d = data + t->skip;
    n = len - (size_t)t->skip;
    if (n < 2)
        return PR_IGNORE;

    switch (t->parser) {
    case P_STD:
        if (n < 8) return PR_IGNORE;
        return fill_std(d, pen);

    case P_UCLOGIC:
        if (n < 8) return PR_IGNORE;
        if (BIT(d[1], 6)) return PR_IGNORE;               /* aux report */
        return fill_std(d, pen);

    case P_GIANO:
        if (n < 12) return PR_IGNORE;
        if (BIT(d[1], 5) && BIT(d[1], 6)) return PR_IGNORE; /* aux report */
        fill_std(d, pen);
        pen->x |= (unsigned)(d[8] & 1) << 16;
        return PR_TABLET;

    case P_XPPEN:
        if (d[1] == 0xC0) return PR_OUT;
        if (BIT(d[1], 4)) return PR_IGNORE;                /* aux report */
        if (n < 8) return PR_IGNORE;
        fill_std(d, pen);
        if (n >= 12) {
            pen->x |= (unsigned)d[10] << 16;
            pen->y |= (unsigned)d[11] << 16;
        }
        if (n >= 10)
            pen->eraser = BIT(d[1], 3);
        return PR_TABLET;

    case P_INTUOS:
        if (n < 8) return PR_IGNORE;
        if (d[0] != 0x02 || !BIT(d[1], 6)) return PR_IGNORE;
        fill_std(d, pen);
        pen->eraser = BIT(d[1], 3);
        return BIT(d[1], 7) ? PR_TABLET : PR_OUT;           /* bit 7 = near proximity */

    case P_INTUOSV1:
        if (n < 10) return PR_IGNORE;
        if (d[0] != 0x02 && d[0] != 0x10) return PR_IGNORE;
        if (d[0] == 0x10 && d[1] == 0x20) return PR_IGNORE;
        if (d[1] == 0x80) return PR_OUT;
        if (BIT(d[1], 1) && BIT(d[1], 3)) return PR_IGNORE; /* rotation report */
        if (BIT(d[1], 5)) return fill_v1(d, pen);
        return PR_IGNORE;

    case P_INTUOS3:
        if (n < 10) return PR_IGNORE;
        if (d[0] == 0x10) return fill_v1(d, pen);
        if (d[0] != 0x02) return PR_IGNORE;
        if (d[1] == 0xEA || d[1] == 0xAA) return PR_IGNORE; /* rotation report */
        if ((d[1] & 0xF0) == 0xE0 || (d[1] & 0xF0) == 0xA0) return fill_v1(d, pen);
        return PR_IGNORE;                                   /* mouse / other */

    case P_INTUOSV2:
        if (n < 12) return PR_IGNORE;
        if (d[0] != 0x10) return PR_IGNORE;
        pen->x = U16(d + 2) | ((unsigned)d[4] << 16);
        pen->y = U16(d + 5) | ((unsigned)d[7] << 16);
        pen->pressure = U16(d + 8);
        pen->eraser = BIT(d[1], 4);
        pen->btn1 = BIT(d[1], 1);
        pen->btn2 = BIT(d[1], 2);
        return BIT(d[1], 5) ? PR_TABLET : PR_OUT;
        /* note: OTD parses these as ITabletReport + IProximityReport (bit 5) */

    case P_BAMBOO: {
        int avail;
        if (n < 8) return PR_IGNORE;
        if (d[0] != 0x02) return PR_IGNORE;
        avail = BIT(d[1], 7)
             || (d[2] | d[3] | d[4] | d[5]) != 0
             || (d[6] | ((d[7] & 0x03) << 8)) != 0;
        if (!avail) return PR_IGNORE;                       /* aux report */
        if (BIT(d[1], 6)) return PR_IGNORE;                 /* mouse report */
        pen->x = U16(d + 2);
        pen->y = U16(d + 4);
        pen->pressure = BIT(d[1], 0) ? (unsigned)(d[6] | ((d[7] & 0x03) << 8)) : 0;
        pen->eraser = BIT(d[1], 5);
        pen->btn1 = BIT(d[1], 1);
        pen->btn2 = BIT(d[1], 2);
        return BIT(d[1], 7) ? PR_TABLET : PR_OUT;
    }

    default:
        return PR_IGNORE;
    }
}

/* ------------------------------------------------------------- tablet db */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "0202;0901" -> two byte arrays */
static void parse_hexlist(const char *s, unsigned char out[][MAX_INIT_LEN], int *lens, int *count)
{
    *count = 0;
    while (*s && *count < MAX_INIT) {
        int n = 0;
        while (*s && *s != ';') {
            int hi = hexval(s[0]), lo = hi >= 0 ? hexval(s[1]) : -1;
            if (hi < 0 || lo < 0 || n >= MAX_INIT_LEN) { n = -1; break; }
            out[*count][n++] = (unsigned char)(hi * 16 + lo);
            s += 2;
        }
        if (n < 0)
            while (*s && *s != ';') s++;
        else if (n > 0) {
            lens[*count] = n;
            (*count)++;
        }
        if (*s == ';') s++;
    }
}

static ParserId parser_from_name(const char *s)
{
    static const struct { const char *name; ParserId id; } map[] = {
        {"std", P_STD}, {"uclogic", P_UCLOGIC}, {"giano", P_GIANO}, {"xppen", P_XPPEN},
        {"intuos", P_INTUOS}, {"intuosv1", P_INTUOSV1}, {"intuosv2", P_INTUOSV2},
        {"intuos3", P_INTUOS3}, {"bamboo", P_BAMBOO}
    };
    size_t i;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (strcmp(map[i].name, s) == 0)
            return map[i].id;
    return P_UNSUPPORTED;
}

/* split a line in place on '|'; returns number of fields */
static int split_fields(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;
    while (*line && n < max) {
        if (*line == '|') { *line = 0; f[n++] = line + 1; }
        line++;
    }
    return n;
}

int tabletdb_load(TabletDb *db, const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[2048];
    size_t cap = 0;

    db->items = NULL;
    db->count = 0;
    if (!fp)
        return -1;

    while (fgets(line, sizeof(line), fp)) {
        char *f[16];
        TabletDef *t;
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!l || line[0] == '#')
            continue;
        if (split_fields(line, f, 16) < 14)
            continue;

        if (db->count == cap) {
            TabletDef *p;
            cap = cap ? cap * 2 : 128;
            p = (TabletDef *)realloc(db->items, cap * sizeof(TabletDef));
            if (!p) { fclose(fp); return -2; }
            db->items = p;
        }
        t = &db->items[db->count];
        memset(t, 0, sizeof(*t));
        strncpy(t->name, f[0], sizeof(t->name) - 1);
        t->vid = (unsigned)strtoul(f[1], NULL, 10);
        t->pid = (unsigned)strtoul(f[2], NULL, 10);
        t->report_len = (unsigned)strtoul(f[3], NULL, 10);
        strncpy(t->parser_name, f[4], sizeof(t->parser_name) - 1);
        t->parser = parser_from_name(f[4]);
        t->skip = atoi(f[5]);
        t->max_x = (unsigned)strtoul(f[6], NULL, 10);
        t->max_y = (unsigned)strtoul(f[7], NULL, 10);
        t->max_pressure = (unsigned)strtoul(f[8], NULL, 10);
        t->width_mm = atof(f[9]);
        t->height_mm = atof(f[10]);
        parse_hexlist(f[11], t->feature, t->feature_len, &t->feature_count);
        parse_hexlist(f[12], t->output, t->output_len, &t->output_count);
        {
            const char *s = f[13];
            while (*s && t->string_count < MAX_INIT) {
                t->strings[t->string_count++] = atoi(s);
                while (*s && *s != ',') s++;
                if (*s == ',') s++;
            }
        }
        db->count++;
    }
    fclose(fp);
    return 0;
}

void tabletdb_free(TabletDb *db)
{
    free(db->items);
    db->items = NULL;
    db->count = 0;
}

/* ---------------------------------------------------------------- config */

void config_defaults(Config *c)
{
    memset(c, 0, sizeof(*c));
    c->area_w = c->area_h = 0;
    c->area_cx = c->area_cy = -1;
    c->lock_aspect = 1;
    c->rotation = 0;
    c->click_threshold = 1.0;
    c->button1 = B_RIGHT;
    c->button2 = B_MIDDLE;
    c->eraser_clicks = 0;
}

static char *trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int parse_button(const char *v, ButtonAction *out)
{
    if (!strcmp(v, "none"))   { *out = B_NONE;   return 1; }
    if (!strcmp(v, "left"))   { *out = B_LEFT;   return 1; }
    if (!strcmp(v, "right"))  { *out = B_RIGHT;  return 1; }
    if (!strcmp(v, "middle")) { *out = B_MIDDLE; return 1; }
    return 0;
}

int config_load(Config *c, const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[512];
    int lineno = 0, first_err = 0;

    if (!fp)
        return -1;
    while (fgets(line, sizeof(line), fp)) {
        char *k, *v, *eq, *cm;
        int ok = 1;
        lineno++;
        cm = strpbrk(line, "#;");
        if (cm) *cm = 0;
        k = trim(line);
        if (!*k || *k == '[')
            continue;
        eq = strchr(k, '=');
        if (!eq) { if (!first_err) first_err = lineno; continue; }
        *eq = 0;
        v = trim(eq + 1);
        k = trim(k);

        if (!strcmp(k, "tablet")) { strncpy(c->tablet, v, sizeof(c->tablet) - 1); }
        else if (!strcmp(k, "area_width_mm"))    c->area_w = atof(v);
        else if (!strcmp(k, "area_height_mm"))   c->area_h = atof(v);
        else if (!strcmp(k, "area_center_x_mm")) c->area_cx = atof(v);
        else if (!strcmp(k, "area_center_y_mm")) c->area_cy = atof(v);
        else if (!strcmp(k, "lock_aspect"))      c->lock_aspect = atoi(v) != 0;
        else if (!strcmp(k, "rotation")) {
            int r = atoi(v);
            if (r == 0 || r == 90 || r == 180 || r == 270) c->rotation = r; else ok = 0;
        }
        else if (!strcmp(k, "screen_x"))      c->screen_x = atoi(v);
        else if (!strcmp(k, "screen_y"))      c->screen_y = atoi(v);
        else if (!strcmp(k, "screen_width"))  c->screen_w = atoi(v);
        else if (!strcmp(k, "screen_height")) c->screen_h = atoi(v);
        else if (!strcmp(k, "click_threshold")) c->click_threshold = atof(v);
        else if (!strcmp(k, "button1")) ok = parse_button(v, &c->button1);
        else if (!strcmp(k, "button2")) ok = parse_button(v, &c->button2);
        else if (!strcmp(k, "eraser_clicks")) c->eraser_clicks = atoi(v) != 0;
        else if (!strcmp(k, "debug"))         c->debug = atoi(v) != 0;
        else ok = 0;

        if (!ok && !first_err) first_err = lineno;
    }
    fclose(fp);
    return first_err;
}

/* --------------------------------------------------------------- mapping */

static double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

void effective_area(const TabletDef *t, const Config *c, double screen_aspect,
                    double *aw_out, double *ah_out, double *cx_out, double *cy_out)
{
    int swap = (c->rotation == 90 || c->rotation == 270);
    double aw = c->area_w > 0 ? c->area_w : (swap ? t->height_mm : t->width_mm);
    double ah = c->area_h > 0 ? c->area_h : (swap ? t->width_mm : t->height_mm);

    if (c->lock_aspect && screen_aspect > 0 && aw > 0 && ah > 0) {
        if (aw / ah > screen_aspect) aw = ah * screen_aspect;
        else                         ah = aw / screen_aspect;
    }
    *aw_out = aw;
    *ah_out = ah;
    *cx_out = c->area_cx >= 0 ? c->area_cx : t->width_mm / 2;
    *cy_out = c->area_cy >= 0 ? c->area_cy : t->height_mm / 2;
}

Pos map_point(const TabletDef *t, const Config *c, double screen_aspect, unsigned x, unsigned y)
{
    Pos p;
    double tx = t->max_x ? (double)x / (double)t->max_x * t->width_mm : 0;
    double ty = t->max_y ? (double)y / (double)t->max_y * t->height_mm : 0;
    double aw, ah, cx, cy, dx, dy, du, dv;

    effective_area(t, c, screen_aspect, &aw, &ah, &cx, &cy);
    dx = tx - cx;
    dy = ty - cy;
    switch (c->rotation) {
    case 90:  du = -dy; dv =  dx; break;
    case 180: du = -dx; dv = -dy; break;
    case 270: du =  dy; dv = -dx; break;
    default:  du =  dx; dv =  dy; break;
    }
    p.u = aw > 0 ? clamp01(du / aw + 0.5) : 0.5;
    p.v = ah > 0 ? clamp01(dv / ah + 0.5) : 0.5;
    return p;
}
