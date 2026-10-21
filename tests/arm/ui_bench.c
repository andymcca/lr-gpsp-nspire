/*
 * What the new menus cost on the calculator: the kit and the screens
 * (nspire_ui.c, nspire_ui_screens.c, compiled as for the calculator) doing
 * what the menus do, each step between two perf_frame_mark calls, so the
 * ARM926 model (tools/calc_speed/arm926.c) counts each one's cycles:
 *
 *   1  the menu opens: the blurred backdrop and the paused game's picture
 *   2  the main menu drawn and turned onto the LCD's buffer
 *   3  a settings page, the same
 *   4  the game list, the same
 *   5  saving a state: its picture made and packed (zlib, as the state)
 *   6  the game list's details: a state's picture unpacked and scaled
 *   7  for comparison, what saving a state cost before: packing the state
 *      (zlib level 6, nspire_states.c), when one is given
 *
 * tests/arm/ui_bench.sh builds it, runs it under the model and prints the
 * milliseconds.
 *
 *   ui_bench [FRAME.ppm [STATE.svs.tns]]
 *
 * FRAME: a game's picture, 240x160 (P6), to pack (the default, made up,
 * hardly packs at all). STATE: one of this build's states (GPSZ, packed).
 */

#include "nspire_ui_screens.h"
#include "nspire_states.h"
#include "nspire_frontend.h"

#include <stdio.h>
#include <string.h>
#include <zlib.h>

u16 nspire_landscape[320 * 240] __attribute__((aligned(32)));
static u16 lcd[320 * 240] __attribute__((aligned(32)));

void __attribute__((noinline)) perf_frame_mark(void)
{
  __asm__ volatile ("" ::: "memory");
}

/* As on the calculator (nspire_video_ui.c), without the flip itself. */
void flip_screen_ui(void)
{
  nspire_turn_rect_565(lcd, nspire_landscape, 320, 0, 0, 320, 240);
}

/* A game's frame from a file, or bands of color with detail. */
static void make_frame(u16 *frame, const char *path)
{
  static unsigned char rgb[240 * 160 * 3];
  char header[16];
  FILE *f = path ? fopen(path, "rb") : NULL;
  int x, y;

  if (f && fread(header, 1, 15, f) == 15 && !memcmp(header, "P6\n240 160\n255\n", 15) &&
      fread(rgb, 1, sizeof(rgb), f) == sizeof(rgb))
  {
    for (x = 0; x < 240 * 160; x++)
      frame[x] = UI_RGB(rgb[3 * x], rgb[3 * x + 1], rgb[3 * x + 2]);
    fclose(f);
    return;
  }
  if (f)
    fclose(f);
  for (y = 0; y < 160; y++)
    for (x = 0; x < 240; x++)
      frame[y * 240 + x] = UI_RGB((x * 7 + y * 3) & 255, (x ^ y) & 255, (y * 5 + (x >> 2)) & 255);
}

int main(int argc, char **argv)
{
  static const char *const tabs[] = { "Graphics", "Emulator", "Controls", "Saving", "About" };
  static const char *const games[] = { "Advance Wars (USA) (Rev 1).zip.tns",
                                       "Fire Emblem - The Sacred Stones (USA, Australia).zip.tns",
                                       "Golden Sun (USA, Europe).zip.tns",
                                       "Legend of Zelda, The - The Minish Cap (USA).zip.tns",
                                       "Pokemon - Emerald Version (USA, Europe).zip.tns" };
  static u16 frame[240 * 160], paused[STATE_PIC_W * STATE_PIC_H], pic[STATE_PIC_W * STATE_PIC_H];
  static u8 packed[STATE_PIC_W * STATE_PIC_H * 2 + 1024];
  static struct ui_menu_view mv;
  static struct ui_list_view lv;
  static struct ui_list_details details;
  uLongf packed_size = sizeof(packed), raw_size = sizeof(pic);
  int i;

  make_frame(frame, argc > 1 ? argv[1] : NULL);

  perf_frame_mark();   /* the model counts from here */
  perf_frame_mark();   /* 1: the menu opens */
  ui_backdrop_make(frame, 240);
  ui_scale(paused, STATE_PIC_W, STATE_PIC_H, frame, 240, 160, 240);

  perf_frame_mark();   /* 2: the main menu */
  mv.page = -1;
  mv.tabs = tabs;
  mv.tab_count = 5;
  mv.title = "Pokemon: Emerald Version";
  mv.status = "";
  for (i = 0; i < 9; i++)
  {
    snprintf(mv.rows[i].label, sizeof(mv.rows[i].label), "Line %d of the menu", i);
    mv.rows[i].gap_before = i == 4 || i == 7;
  }
  snprintf(mv.rows[2].value, sizeof(mv.rows[2].value), "015");
  mv.rows[2].kind = UI_ROW_PICK;
  mv.count = 9;
  mv.picked = 2;
  mv.picture = paused;
  snprintf(mv.pic_head, sizeof(mv.pic_head), "Slot 015 " UI_DOT " newest");
  snprintf(mv.pic_sub, sizeof(mv.pic_sub), "16 states");
  mv.help = "Goes back to that moment. Anything since is lost unless you save first.";
  ui_menu_draw(&mv);

  perf_frame_mark();   /* 3: a settings page */
  mv.page = 0;
  for (i = 0; i < 11; i++)
  {
    mv.rows[i].kind = i & 1 ? UI_ROW_SWITCH : UI_ROW_PICK;
    mv.rows[i].on = i & 2;
    snprintf(mv.rows[i].value, sizeof(mv.rows[i].value), "Value %d", i);
  }
  mv.count = 11;
  mv.picked = 0;
  mv.help = "3:2 keeps the GBA's shape (320x214); full screen fills it. Smooth blends the pixels.";
  ui_menu_draw(&mv);

  perf_frame_mark();   /* 4: the game list */
  ui_scale(details.picture, 120, 80, paused, STATE_PIC_W, STATE_PIC_H, STATE_PIC_W);
  details.ready = details.has_picture = 1;
  details.states = 16;
  details.newest = 15;
  details.bytes = 6815744;
  lv.folder = "gba";
  lv.games = 5;
  for (i = 0; i < 5; i++)
  {
    lv.rows[i].file = games[i];
    lv.rows[i].number = i + 1;
  }
  lv.picked_row = 4;
  lv.marked_row = lv.dir_picked = -1;
  lv.dir_count = 1;
  lv.status = "";
  lv.details = &details;
  ui_list_draw(&lv);

  perf_frame_mark();   /* 5: a state's picture, made and packed */
  ui_scale(pic, STATE_PIC_W, STATE_PIC_H, frame, 240, 160, 240);
  compress2(packed, &packed_size, (const Bytef *)pic, sizeof(pic), 1);   /* PICTURE_LEVEL */

  perf_frame_mark();   /* 6: the list's details: unpacked and scaled */
  uncompress((Bytef *)pic, &raw_size, packed, packed_size);
  ui_scale(details.picture, 120, 80, pic, STATE_PIC_W, STATE_PIC_H, STATE_PIC_W);

  perf_frame_mark();
  printf("picture packed: %lu bytes\n", (unsigned long)packed_size);

  if (argc > 2)
  {
    /* 7: the state itself packed, as every save does */
    static u8 file[1024 * 1024], raw[1024 * 1024], out[1024 * 1024];
    FILE *f = fopen(argv[2], "rb");
    size_t n = f ? fread(file, 1, sizeof(file), f) : 0;
    uLongf raw_len = sizeof(raw), out_len = sizeof(out);
    if (f)
      fclose(f);
    if (n > 32 && !memcmp(file, "GPSZ", 4) && uncompress(raw, &raw_len, file + 32, n - 32) == Z_OK)
    {
      perf_frame_mark();
      compress2(out, &out_len, raw, raw_len, 6);
      perf_frame_mark();
      printf("state: %lu bytes packed into %lu\n", (unsigned long)raw_len, (unsigned long)out_len);
    }
  }
  return 0;
}
