#include "../src/core.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-6)

static TabletDef mk(ParserId p, int skip)
{
    TabletDef t; memset(&t, 0, sizeof(t));
    t.parser = p; t.skip = skip; t.max_x = 15200; t.max_y = 9500; t.max_pressure = 2047;
    t.width_mm = 152; t.height_mm = 95;
    return t;
}

int main(int argc, char **argv)
{
    Pen pen;
    TabletDef t;

    /* Wacom Intuos: id 2, flags 0xC2 = tool(6) + prox(7) + btn1(1) */
    { unsigned char r[10] = {0x02,0xC2,0x34,0x12,0x67,0x05,0xFF,0x01,0,0};
      t = mk(P_INTUOS, 0);
      CHECK(parse_report(&t, r, 10, &pen) == PR_TABLET);
      CHECK(pen.x == 0x1234 && pen.y == 0x0567 && pen.pressure == 0x01FF);
      CHECK(pen.btn1 == 1 && pen.btn2 == 0 && pen.eraser == 0);
      r[1] = 0x40;                                  /* tool bit but no proximity */
      CHECK(parse_report(&t, r, 10, &pen) == PR_OUT);
      r[1] = 0x00;                                  /* not a tool report */
      CHECK(parse_report(&t, r, 10, &pen) == PR_IGNORE);
      r[1] = 0xC8;                                  /* eraser */
      CHECK(parse_report(&t, r, 10, &pen) == PR_TABLET && pen.eraser == 1);
      /* WacomDriver variant: one extra leading byte */
      { unsigned char w[11] = {0x00,0x02,0xC2,0x34,0x12,0x67,0x05,0xFF,0x01,0,0};
        t = mk(P_INTUOS, 1);
        CHECK(parse_report(&t, w, 11, &pen) == PR_TABLET && pen.x == 0x1234); } }

    /* IntuosV1 tablet report */
    { unsigned char r[10] = {0x02,0x60,0x12,0x34,0x05,0x67,0x7F,0xC0,0,0x02};
      t = mk(P_INTUOSV1, 0);
      CHECK(parse_report(&t, r, 10, &pen) == PR_TABLET);
      CHECK(pen.x == 0x2469);
      CHECK(pen.y == 0x0ACE);
      CHECK(pen.pressure == 0x3FE);
      r[1] = 0x80; CHECK(parse_report(&t, r, 10, &pen) == PR_OUT);
      r[0] = 0x03; r[1] = 0x60; CHECK(parse_report(&t, r, 10, &pen) == PR_IGNORE);
      /* Intuos3 accepts 0xE0 flags */
      t = mk(P_INTUOS3, 0);
      r[0] = 0x02; r[1] = 0xE0; CHECK(parse_report(&t, r, 10, &pen) == PR_TABLET);
      r[1] = 0xEA; CHECK(parse_report(&t, r, 10, &pen) == PR_IGNORE); }

    /* IntuosV2 */
    { unsigned char r[17] = {0x10,0x22,0x34,0x12,0x01,0x78,0x56,0x02,0xFF,0x0F,0,0,0,0,0,0,0};
      t = mk(P_INTUOSV2, 0);
      CHECK(parse_report(&t, r, 17, &pen) == PR_TABLET);
      CHECK(pen.x == 0x11234 && pen.y == 0x25678 && pen.pressure == 0x0FFF);
      CHECK(pen.btn1 == 1 && pen.eraser == 0);
      r[1] = 0x00; CHECK(parse_report(&t, r, 17, &pen) == PR_OUT);
      r[0] = 0x11; CHECK(parse_report(&t, r, 17, &pen) == PR_IGNORE); }

    /* XP-Pen */
    { unsigned char r[12] = {0x02,0x01,0x34,0x12,0x67,0x05,0xFF,0x01,0,0,0x01,0x02};
      t = mk(P_XPPEN, 0);
      CHECK(parse_report(&t, r, 12, &pen) == PR_TABLET);
      CHECK(pen.x == (0x1234 | (1u << 16)) && pen.y == (0x0567 | (2u << 16)));
      CHECK(parse_report(&t, r, 10, &pen) == PR_TABLET && pen.x == 0x1234);
      r[1] = 0xC0; CHECK(parse_report(&t, r, 12, &pen) == PR_OUT);
      r[1] = 0x10; CHECK(parse_report(&t, r, 12, &pen) == PR_IGNORE); }

    /* UCLogic / Huion Giano / std */
    { unsigned char r[12] = {0x08,0x82,0x34,0x12,0x67,0x05,0x10,0x00,0x01,0,0,0};
      t = mk(P_UCLOGIC, 0);
      CHECK(parse_report(&t, r, 12, &pen) == PR_TABLET && pen.btn1 == 1 && pen.pressure == 0x10);
      r[1] = 0xE0; CHECK(parse_report(&t, r, 12, &pen) == PR_IGNORE);   /* aux */
      t = mk(P_GIANO, 0);
      r[1] = 0x82; CHECK(parse_report(&t, r, 12, &pen) == PR_TABLET && pen.x == (0x1234 | (1u << 16)));
      r[1] = 0xE0; CHECK(parse_report(&t, r, 12, &pen) == PR_IGNORE);
      t = mk(P_STD, 0);
      CHECK(parse_report(&t, r, 12, &pen) == PR_TABLET); }

    /* Bamboo */
    { unsigned char r[10] = {0x02,0x81,0x34,0x12,0x67,0x05,0xFF,0x03,0,0};
      t = mk(P_BAMBOO, 0);
      CHECK(parse_report(&t, r, 10, &pen) == PR_TABLET && pen.pressure == 0x3FF);
      r[1] = 0x80; CHECK(parse_report(&t, r, 10, &pen) == PR_TABLET && pen.pressure == 0);
      r[1] = 0xC0; CHECK(parse_report(&t, r, 10, &pen) == PR_IGNORE);   /* mouse */ }

    /* short / garbage input must never crash */
    { unsigned char r[3] = {0x02,0xC2,0x00};
      int p;
      for (p = P_STD; p <= P_BAMBOO; p++) { t = mk((ParserId)p, 1); (void)parse_report(&t, r, 3, &pen); (void)parse_report(&t, r, 0, &pen); (void)parse_report(&t, r, 1, &pen); }
      CHECK(1); }

    /* mapping */
    { Config c; Pos p;
      t = mk(P_INTUOS, 0);
      config_defaults(&c);
      p = map_point(&t, &c, 16.0 / 9.0, 7600, 4750);
      CHECK(NEAR(p.u, 0.5) && NEAR(p.v, 0.5));
      p = map_point(&t, &c, 16.0 / 9.0, 0, 0);
      CHECK(NEAR(p.u, 0.0) && NEAR(p.v, 0.0));
      p = map_point(&t, &c, 16.0 / 9.0, 15200, 9500);
      CHECK(NEAR(p.u, 1.0) && NEAR(p.v, 1.0));
      /* aspect lock: area is 152 x 85.5 mm, so 10 mm below center = 10/85.5 */
      p = map_point(&t, &c, 16.0 / 9.0, 7600, 4750 + 1000);
      CHECK(NEAR(p.v, 0.5 + 10.0 / (152.0 * 9.0 / 16.0)));
      c.lock_aspect = 0;
      p = map_point(&t, &c, 16.0 / 9.0, 7600, 4750 + 1000);
      CHECK(NEAR(p.v, 0.5 + 10.0 / 95.0));
      /* rotation 180 flips both axes */
      c.rotation = 180;
      p = map_point(&t, &c, 16.0 / 9.0, 0, 0);
      CHECK(NEAR(p.u, 1.0) && NEAR(p.v, 1.0));
      /* rotation 90: moving +x on the tablet moves the cursor down */
      c.rotation = 90; c.lock_aspect = 0;
      p = map_point(&t, &c, 16.0 / 9.0, 7600 + 1000, 4750);
      CHECK(NEAR(p.u, 0.5) && p.v > 0.5);
      /* explicit area */
      config_defaults(&c); c.area_w = 100; c.area_h = 50; c.lock_aspect = 0;
      p = map_point(&t, &c, 1.0, 7600 + 1000, 4750);                    /* +10 mm in x */
      CHECK(NEAR(p.u, 0.6)); }

    /* effective area: lock shrinks height for a wide screen, rotation swaps defaults */
    { Config c; double aw, ah, cx, cy;
      t = mk(P_INTUOS, 0);
      config_defaults(&c);
      effective_area(&t, &c, 16.0 / 9.0, &aw, &ah, &cx, &cy);
      CHECK(NEAR(aw, 152.0) && NEAR(ah, 152.0 * 9.0 / 16.0) && NEAR(cx, 76.0) && NEAR(cy, 47.5));
      c.lock_aspect = 0; c.rotation = 90;
      effective_area(&t, &c, 16.0 / 9.0, &aw, &ah, &cx, &cy);
      CHECK(NEAR(aw, 95.0) && NEAR(ah, 152.0)); }

    /* database + config files */
    { const char *dbpath = argc > 1 ? argv[1] : "tablets.txt";
      TabletDb db; size_t i; int found = 0, ok = 1;
      if (tabletdb_load(&db, dbpath) != 0) { printf("cannot open %s\n", dbpath); return 2; }
      CHECK(db.count > 300);
      for (i = 0; i < db.count; i++) {
          const TabletDef *d = &db.items[i];
          if (d->vid == 1386 && d->pid == 890 && d->report_len == 10) {
              found = 1;
              CHECK(d->parser == P_INTUOS && d->skip == 0);
              CHECK(d->max_x == 15200 && d->max_y == 9500 && d->max_pressure == 2047);
              CHECK(d->feature_count == 1 && d->feature_len[0] == 2 && d->feature[0][0] == 2 && d->feature[0][1] == 2);
          }
          if (d->parser != P_UNSUPPORTED && (!d->max_x || !d->max_y || !d->width_mm)) ok = 0;
      }
      CHECK(found); CHECK(ok);
      tabletdb_free(&db); }

    { Config c; FILE *f = fopen("/tmp/otdxp_test.ini", "w");
      fputs("# comment\n[section]\ntablet = CTL\nrotation=90\nlock_aspect = 0 ; trailing\nbutton1 = none\nclick_threshold=2.5\nbogus=1\n", f);
      fclose(f);
      config_defaults(&c);
      CHECK(config_load(&c, "/tmp/otdxp_test.ini") == 8);     /* line of 'bogus' */
      CHECK(!strcmp(c.tablet, "CTL") && c.rotation == 90 && c.lock_aspect == 0 && c.button1 == B_NONE && NEAR(c.click_threshold, 2.5));
      CHECK(config_load(&c, "/nonexistent.ini") == -1); }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
