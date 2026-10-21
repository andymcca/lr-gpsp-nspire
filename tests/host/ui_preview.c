/* The menus' screens drawn as the calculator draws them (nspire_ui.c,
 * nspire_ui_screens.c), with pictures from real games, to look at the
 * design on a PC:
 *
 *     ui_preview OUTDIR GAME.ppm BATTLE.ppm
 *
 * The pictures are 240x160 frames (P6); each screen is written as
 * OUTDIR/<name>.ppm, 320x240. */

#include "nspire_ui_screens.h"
#include "nspire_states.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u16 nspire_landscape[320 * 240];

static const char *out_dir;
static const char *screen_name;

void flip_screen_ui(void)
{
  char path[600];
  FILE *f;
  int i;

  snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, screen_name);
  f = fopen(path, "wb");
  if (!f)
  {
    perror(path);
    exit(1);
  }
  fprintf(f, "P6\n320 240\n255\n");
  for (i = 0; i < 320 * 240; i++)
  {
    u16 p = nspire_landscape[i];
    unsigned char rgb[3] = { (unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2),
                             (unsigned char)((p & 31) << 3) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void load_frame(const char *path, u16 *frame)
{
  unsigned char rgb[240 * 160 * 3];
  char header[16];
  FILE *f = fopen(path, "rb");
  int i;

  if (!f || fread(header, 1, 15, f) != 15 || memcmp(header, "P6\n240 160\n255\n", 15) ||
      fread(rgb, 1, sizeof(rgb), f) != sizeof(rgb))
  {
    fprintf(stderr, "ui_preview: %s isn't a 240x160 P6 picture\n", path);
    exit(1);
  }
  fclose(f);
  for (i = 0; i < 240 * 160; i++)
    frame[i] = UI_RGB(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]);
}

static const char *const games[] = {
  "Advance Wars (USA) (Rev 1).zip.tns",
  "Fire Emblem - The Sacred Stones (USA, Australia).zip.tns",
  "Golden Sun (USA, Europe).zip.tns",
  "Legend of Zelda, The - The Minish Cap (USA).zip.tns",
  "Pokemon - Emerald Version (USA, Europe).zip.tns",
};

static void list_screens(const u16 *world)
{
  static const char *const dirs[] = { "..", "GBC", "Homebrew" };
  static struct ui_list_details details;
  struct ui_list_view v;
  u16 pic[STATE_PIC_W * STATE_PIC_H];
  int i;

  memset(&v, 0, sizeof(v));
  v.folder = "gba";
  v.games = 5;
  for (i = 0; i < 5; i++)
  {
    v.rows[i].file = games[i];
    v.rows[i].number = (i + 1) % 10;
  }
  v.picked_row = 4;
  v.marked_row = -1;
  v.dirs = dirs;
  v.dir_count = 3;
  v.dir_picked = -1;
  v.status = "";
  ui_scale(pic, STATE_PIC_W, STATE_PIC_H, world, 240, 160, 240);
  ui_scale(details.picture, 120, 80, pic, STATE_PIC_W, STATE_PIC_H, STATE_PIC_W);
  details.ready = details.has_picture = 1;
  details.states = 16;
  details.newest = 15;
  details.bytes = 6815744;
  v.details = &details;
  screen_name = "list";
  ui_list_draw(&v);

  v.moving = 1;
  v.picked_row = 1;
  details.has_picture = 0;
  details.states = 0;
  details.newest = -1;
  details.bytes = 9646899;
  screen_name = "list_moving";
  ui_list_draw(&v);

  v.moving = 0;
  v.picked_row = -1;
  v.marked_row = 1;
  v.dir_picked = 1;
  screen_name = "list_folders";
  ui_list_draw(&v);

  memset(&v, 0, sizeof(v));
  v.folder = "Homebrew";
  v.dirs = dirs;
  v.dir_count = 1;
  v.dir_picked = 0;
  v.picked_row = v.marked_row = -1;
  v.status = "";
  screen_name = "list_empty";
  ui_list_draw(&v);
}

static void menu_screens(const u16 *world, const u16 *battle)
{
  static const char *const tabs[] = { "Graphics", "Emulator", "Controls", "Saving", "About" };
  static const char *const main_rows[] = { "Resume", "Save state", "Load state", "Restart", "Settings",
                                           "Save manager", "ROM manager", "Game list", "Exit gpSP" };
  static u16 paused[STATE_PIC_W * STATE_PIC_H], slot[STATE_PIC_W * STATE_PIC_H];
  static struct ui_menu_view v;
  int i;

  ui_backdrop_make(world, 240);
  ui_scale(paused, STATE_PIC_W, STATE_PIC_H, world, 240, 160, 240);
  ui_scale(slot, STATE_PIC_W, STATE_PIC_H, battle, 240, 160, 240);

  memset(&v, 0, sizeof(v));
  v.page = -1;
  v.tabs = tabs;
  v.tab_count = 5;
  v.title = "Pokemon: Emerald Version";
  v.status = "";
  for (i = 0; i < 9; i++)
  {
    snprintf(v.rows[i].label, sizeof(v.rows[i].label), "%s", main_rows[i]);
    v.rows[i].gap_before = i == 4 || i == 7;
  }
  snprintf(v.rows[1].value, sizeof(v.rows[1].value), "016");
  snprintf(v.rows[2].value, sizeof(v.rows[2].value), "015");
  v.rows[2].kind = UI_ROW_PICK;
  v.count = 9;

  v.picked = 2;
  v.picture = slot;
  snprintf(v.pic_head, sizeof(v.pic_head), "Slot 015 " UI_DOT " newest");
  snprintf(v.pic_sub, sizeof(v.pic_sub), "16 states " UI_DOT " " UI_ARROW_L " " UI_ARROW_R " picks one");
  v.help = "Goes back to that moment. Anything since is lost unless you save first.";
  screen_name = "main_load";
  ui_menu_draw(&v);

  v.picked = 0;
  v.picture = paused;
  v.pic_head[0] = v.pic_sub[0] = 0;
  v.help = "Back to the game, where you left it.";
  screen_name = "main_resume";
  ui_menu_draw(&v);

  /* Graphics */
  memset(v.rows, 0, sizeof(v.rows));
  v.page = 0;
  v.picked = 0;
  {
    static const char *const rows[][2] = {
      { "Display scaling", "3:2, smooth" }, { "Screen brightness", "9" }, { "Frameskip type", "Off" },
      { "Frameskip value", "4" }, { "Frameskip variation", "Uniform" }, { "LCD framebuffer cache", "Range" },
      { "Show FPS counter", "On" }, { "GBA blending (BLDCNT)", "On" }, { "Video renderer", "video.cc" },
      { "Fast forward max speed", "Unlimited" }, { "Run speed test", NULL },
    };
    for (i = 0; i < 11; i++)
    {
      snprintf(v.rows[i].label, sizeof(v.rows[i].label), "%s", rows[i][0]);
      if (rows[i][1])
      {
        snprintf(v.rows[i].value, sizeof(v.rows[i].value), "%s", rows[i][1]);
        v.rows[i].kind = UI_ROW_PICK;
      }
    }
    v.count = 11;
  }
  v.help = "3:2 keeps the GBA's shape (320x214); full screen fills it. Smooth blends the pixels, sharp is "
           "the fastest. Small: the GBA's own 240x160.";
  screen_name = "graphics";
  ui_menu_draw(&v);

  /* Controls, setting the second key of B */
  memset(v.rows, 0, sizeof(v.rows));
  v.page = 2;
  {
    static const char *const rows[][3] = {
      { "D-pad up", "8", "" }, { "D-pad down", "5", "" }, { "D-pad left", "4", "" }, { "D-pad right", "6", "" },
      { "A", "del", "7" }, { "B", "var", "" }, { "L", "scratch", "" },
    };
    for (i = 0; i < 7; i++)
    {
      snprintf(v.rows[i].label, sizeof(v.rows[i].label), "%s", rows[i][0]);
      snprintf(v.rows[i].value, sizeof(v.rows[i].value), "%s", rows[i][1]);
      snprintf(v.rows[i].value2, sizeof(v.rows[i].value2), "%s", rows[i][2]);
      v.rows[i].kind = UI_ROW_KEYS;
      v.rows[i].key_column = 1;
    }
    v.count = 24;
  }
  v.picked = 5;
  v.help = "Left/right: key 1 or 2. Enter: set it (hold a key and press another for a combination). del: clear.";
  screen_name = "controls";
  ui_menu_draw(&v);
  v.capturing = 1;
  v.status = "Press the key (or two keys together). An arrow cancels.";
  screen_name = "controls_capture";
  ui_menu_draw(&v);
  v.capturing = 0;
  v.status = "";

  /* Saving */
  memset(v.rows, 0, sizeof(v.rows));
  v.page = 3;
  {
    static const char *const rows[] = { "Auto-increment slot on save", "Save a state when leaving",
                                        "Load newest state on start", "Update backup", "Compress saves" };
    for (i = 0; i < 5; i++)
    {
      snprintf(v.rows[i].label, sizeof(v.rows[i].label), "%s", rows[i]);
      v.rows[i].kind = UI_ROW_SWITCH;
      v.rows[i].on = i != 1;
    }
    v.rows[3].kind = UI_ROW_PICK;
    snprintf(v.rows[3].value, sizeof(v.rows[3].value), "Automatic");
    v.count = 5;
  }
  v.picked = 1;
  v.help = "Save+Exit, Exit gpSP and Load new game save a state first, to carry on from there next time.";
  screen_name = "saving";
  ui_menu_draw(&v);
}

int main(int argc, char **argv)
{
  static u16 world[240 * 160], battle[240 * 160];

  if (argc != 4)
  {
    fprintf(stderr, "usage: ui_preview OUTDIR GAME.ppm BATTLE.ppm\n");
    return 2;
  }
  out_dir = argv[1];
  load_frame(argv[2], world);
  load_frame(argv[3], battle);
  list_screens(world);
  menu_screens(world, battle);
  return 0;
}
