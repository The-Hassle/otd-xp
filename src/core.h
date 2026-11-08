/* otd-xp core: platform independent part (report parsers, tablet db, config, area mapping).
 * Report parsers are ports of the ones in OpenTabletDriver 0.6.x (LGPL-3.0-or-later). */
#ifndef OTDXP_CORE_H
#define OTDXP_CORE_H

#include <stddef.h>

typedef enum {
    P_UNSUPPORTED = 0,
    P_STD,        /* generic: x@2 y@4 pressure@6 (TabletReportParser, SkipByte) */
    P_UCLOGIC,    /* generic, aux reports flagged by bit 6 of byte 1 */
    P_GIANO,      /* Huion Giano */
    P_XPPEN,
    P_INTUOS,     /* Wacom Intuos (CTL-4x0 ...) */
    P_INTUOSV1,
    P_INTUOSV2,
    P_INTUOS3,
    P_BAMBOO
} ParserId;

#define MAX_INIT 8
#define MAX_INIT_LEN 64

typedef struct {
    char name[96];
    unsigned vid, pid, report_len;
    ParserId parser;
    char parser_name[48];
    int skip;                     /* leading bytes to drop before parsing */
    unsigned max_x, max_y, max_pressure;
    double width_mm, height_mm;
    unsigned char feature[MAX_INIT][MAX_INIT_LEN]; int feature_len[MAX_INIT]; int feature_count;
    unsigned char output[MAX_INIT][MAX_INIT_LEN];  int output_len[MAX_INIT];  int output_count;
    int strings[MAX_INIT]; int string_count;
} TabletDef;

typedef struct {
    TabletDef *items;
    size_t count;
} TabletDb;

/* parse results */
#define PR_IGNORE 0
#define PR_TABLET 1
#define PR_OUT    2   /* pen left the tablet range */

typedef struct {
    unsigned x, y, pressure;
    int btn1, btn2, eraser;
} Pen;

/* Parse one input report (including the report id byte). */
int parse_report(const TabletDef *t, const unsigned char *data, size_t len, Pen *pen);

int tabletdb_load(TabletDb *db, const char *path);   /* returns 0 on success */
void tabletdb_free(TabletDb *db);

/* ---- user configuration (otd-xp.ini) ---- */
typedef enum { B_NONE = 0, B_LEFT, B_RIGHT, B_MIDDLE } ButtonAction;

typedef struct {
    char tablet[96];              /* name substring filter, empty = auto */
    double area_w, area_h;        /* mm, in screen orientation; <=0 = full tablet */
    double area_cx, area_cy;      /* mm from tablet top-left; <0 = tablet center */
    int lock_aspect;
    int rotation;                 /* 0, 90, 180, 270 (degrees the tablet is turned clockwise) */
    int screen_x, screen_y, screen_w, screen_h;  /* pixels; w<=0 = whole virtual desktop/primary */
    double click_threshold;       /* percent of max pressure */
    ButtonAction button1, button2;
    int eraser_clicks;            /* 1 = eraser tip clicks like the pen tip */
    int debug;
} Config;

void config_defaults(Config *c);
int config_load(Config *c, const char *path);   /* 0 ok, -1 file missing (defaults kept), >0 = line of first error */

/* Effective active area after defaults and aspect lock. Width/height are in screen
 * orientation (i.e. after rotation); the centre is in tablet coordinates (mm). */
void effective_area(const TabletDef *t, const Config *c, double screen_aspect,
                    double *aw, double *ah, double *cx, double *cy);

/* Map raw tablet coordinates to a 0..1 position inside the screen rectangle. */
typedef struct { double u, v; } Pos;
Pos map_point(const TabletDef *t, const Config *c, double screen_aspect, unsigned x, unsigned y);

#endif
