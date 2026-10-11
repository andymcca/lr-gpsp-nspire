/* A simulated TI-Nspire CX II for gpSP's frontend on a PC (gpsp-host):
 * the LCD controller, the 32768 Hz timer, the keypad and touchpad and the
 * backlight, driven by a script of key presses in virtual time.
 *
 *   gpsp-host SCRIPT OUTDIR GAMEDIR [ROM]
 *
 * gpSP runs as it does on the calculator (its main(), menus, file list and
 * game loop), as GAMEDIR/gpsp_qol.tns. Virtual time moves a little at every
 * read of the timer, the keypad or the LCD, so gpSP's waits pass time and
 * its frames come at its own pace. Work that takes time on the calculator
 * but none here (running a frame of the game, drawing a line, turning a
 * picture, reading the touchpad) costs what the script's "cost" lines say.
 *
 * The LCD refreshes 60 times a second. Like the PL111, it takes a new buffer
 * address only at the start of a refresh, reads that buffer's 320 lines,
 * then waits out the porches (127 of the 447 lines' time, the CX II OS's
 * timings) before the next refresh. Writing to the buffer while its lines
 * are being read is what makes tearing and flicker, so a buffer that changed
 * between the start and the end of its lines is counted ("writes_to_shown"
 * in OUTDIR/report.txt). The picture it shows is kept, turned to landscape,
 * for the script's screenshots.
 *
 * Script lines (times in seconds):
 *   wait 1.5         let 1.5 s pass
 *   press KEY        down for 0.1 s, then up; 0.25 s pass
 *   tap KEY          down for 0.03 s, then up; 0.1 s pass
 *   hold KEY / release KEY
 *   shot NAME        OUTDIR/NAME.ppm: what the LCD shows now (320x240)
 *   note NAME        a line in the report (game, frame, backlight; see below)
 *   cost KIND N      from now on: frame (ms of a game frame's emulation),
 *                    line (us to draw a line), turn (ms to turn a whole
 *                    320x240 picture; parts cost their share), touchpad (ms),
 *                    read (ms to read 1 MB of a ROM file, or unpack it)
 *   backlight N      the backlight register's value at start (the OS's)
 *   quit             write the report and end
 * KEY is a gpSP key name (enter, esc, menu, del, shift, S, 8, +, ...) or up,
 * down, left, right, click, on.
 *
 * The report's shot and note lines give the game's frame count (the 32-bit
 * number at 0x02000000, where the test ROM keeps it), how many different
 * pictures the LCD has shown, the backlight, and the ROM being played. */

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "os.h"
#include "nspire_hw.h"

#define TICK_HZ 32768.0
#define REFRESH_TICKS (TICK_HZ / 60.0)
#define LCD_LINES_TOTAL 447            /* 320 shown, then porches (CX II OS timings) */
#define PORTRAIT_BYTES (240 * 320 * 2)
#define READ_STEP (1.0 / 64.0)         /* virtual time per hardware read, in ticks */
#define MS (TICK_HZ / 1000.0)

extern char *nspire_key_strings[];
extern unsigned char ewram[];          /* the GBA's 256 KB work RAM (the core's) */
extern char gamepak_filename[];
extern uint32_t skip_next_frame;
int gpsp_main(int argc, char *argv[]);

/* ---- The script ---- */

enum event_type { EV_DOWN, EV_UP, EV_SHOT, EV_NOTE, EV_COST, EV_QUIT };
enum cost_kind { COST_FRAME, COST_LINE, COST_TURN, COST_TOUCHPAD, COST_READ, COSTS };

struct event
{
  double time;
  enum event_type type;
  unsigned key;          /* or the cost's kind */
  double value;          /* a cost, in ticks */
  char name[128];
};

static struct event *events;
static int num_events, next_event;
static char out_dir[4096];   /* realpath needs PATH_MAX */

/* ---- The simulated hardware ---- */

static double now;                  /* virtual time in timer ticks */
static double costs[COSTS];         /* in ticks (a line's, a whole turn's...) */
static uint32_t keys[4];
static int on_key;
static uint32_t timer_load, timer_control, timer_clock;
static uint32_t lcd_timing[3] = { 0x1425094C, 0x03780D3F, 0x00EF3802 };   /* the OS's portrait setup */
static uint32_t lcd_control = 0x392D;
static uint32_t lcd_raw_irq;
static uint32_t backlight = 120, backlight_at_start = 120;
static long backlight_writes;
static uint16_t os_buffer[320 * 240];   /* the OS's LCD buffer */
static uintptr_t lcd_base = (uintptr_t)os_buffer, shown_base = (uintptr_t)os_buffer;
static long refresh_index;
static int lines_done;              /* the visible lines of this refresh were read */
static uint64_t shown_hash;
static uint16_t shown_image[240 * 320];   /* portrait, as the LCD read it */

/* What the report counts. */
static long refreshes, shown_changes, base_writes, distinct_frames;
static long writes_to_shown;
static double first_writes[5];
static int first_writes_count;
static char registered[256];
static char notes[400][300];   /* shot and note lines, in order */
static int num_notes;

static const char *game_name(void)
{
  const char *slash = strrchr(gamepak_filename, '/');
  return gamepak_filename[0] ? (slash ? slash + 1 : gamepak_filename) : "-";
}

static void add_note(const char *kind, const char *name)
{
  unsigned frame = ewram[0] | (ewram[1] << 8) | (ewram[2] << 16) | ((unsigned)ewram[3] << 24);
  if (num_notes < (int)(sizeof(notes) / sizeof(notes[0])))
    snprintf(notes[num_notes++], sizeof(notes[0]),
             "%s %s frame=%u time=%.3f shown=%ld backlight=%u game=%s", kind, name, frame,
             now / TICK_HZ, distinct_frames, (unsigned)backlight, game_name());
}

static uint64_t hash_bytes(const void *data, size_t size)
{
  const unsigned char *p = data;
  uint64_t h = 1469598103934665603ull;
  while (size--)
    h = (h ^ *p++) * 1099511628211ull;
  return h;
}

static void write_ppm(const char *name)
{
  char path[4300];
  FILE *f;
  int x, y;

  snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
  f = fopen(path, "wb");
  if (!f)
  {
    fprintf(stderr, "gpsp-host: can't write %s\n", path);
    return;
  }
  fprintf(f, "P6\n320 240\n255\n");
  /* Landscape pixel (x, y) is position 239 - y of LCD line x. */
  for (y = 0; y < 240; y++)
    for (x = 0; x < 320; x++)
    {
      uint16_t p = shown_image[x * 240 + 239 - y];
      unsigned char rgb[3] = { (unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2),
                               (unsigned char)((p & 31) << 3) };
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static void write_report(void)
{
  char path[4300];
  FILE *f;
  int i;

  snprintf(path, sizeof(path), "%s/report.txt", out_dir);
  f = fopen(path, "w");
  if (!f)
    return;
  fprintf(f, "time %.2f\n", now / TICK_HZ);
  fprintf(f, "refreshes %ld\n", refreshes);
  fprintf(f, "frames_shown %ld\n", distinct_frames);
  fprintf(f, "lcd_base_writes %ld\n", base_writes);
  fprintf(f, "writes_to_shown %ld\n", writes_to_shown);
  for (i = 0; i < first_writes_count; i++)
    fprintf(f, "write_to_shown_at %.3f\n", first_writes[i] / TICK_HZ);
  fprintf(f, "registered %s\n", registered[0] ? registered : "-");
  fprintf(f, "backlight_start %u\n", (unsigned)backlight_at_start);
  fprintf(f, "backlight_end %u\n", (unsigned)backlight);
  fprintf(f, "backlight_writes %ld\n", backlight_writes);
  fprintf(f, "lcd_base_end %s\n", lcd_base == (uintptr_t)os_buffer ? "os" : "gpsp");
  for (i = 0; i < num_notes; i++)
    fprintf(f, "%s\n", notes[i]);
  fclose(f);
}

/* A refresh starts at every multiple of REFRESH_TICKS: the LCD takes the
 * buffer address and starts reading its lines. */
static void refresh_start(void)
{
  if (lcd_base != shown_base)
    shown_changes++;
  shown_base = lcd_base;
  shown_hash = hash_bytes((const void *)shown_base, PORTRAIT_BYTES);
  if (memcmp(shown_image, (const void *)shown_base, PORTRAIT_BYTES))
    distinct_frames++;
  memcpy(shown_image, (const void *)shown_base, PORTRAIT_BYTES);
  refreshes++;
}

/* Its 320 lines are read: the buffer mustn't have changed meanwhile. The
 * vertical compare interrupt comes here (the OS sets it to the porch's start). */
static void refresh_lines_done(void)
{
  if (hash_bytes((const void *)shown_base, PORTRAIT_BYTES) != shown_hash)
  {
    writes_to_shown++;
    if (first_writes_count < 5)
      first_writes[first_writes_count++] = now;
  }
  lcd_raw_irq |= 4;
}

static void run_events(void)
{
  while (next_event < num_events && events[next_event].time <= now)
  {
    struct event *e = &events[next_event++];
    switch (e->type)
    {
    case EV_DOWN:
      if (e->key == 0)
        on_key = 1;
      else
        keys[(e->key - 1) >> 5] |= 1u << ((e->key - 1) & 31);
      break;
    case EV_UP:
      if (e->key == 0)
        on_key = 0;
      else
        keys[(e->key - 1) >> 5] &= ~(1u << ((e->key - 1) & 31));
      break;
    case EV_SHOT:
      write_ppm(e->name);
      add_note("shot", e->name);
      break;
    case EV_NOTE:
      add_note("note", e->name);
      break;
    case EV_COST:
      costs[e->key] = e->value;
      break;
    case EV_QUIT:
      add_note("note", "quit");
      exit(0);   /* write_report runs at exit */
    }
  }
}

static void advance(double ticks)
{
  now += ticks;
  for (;;)
  {
    double lines_end = refresh_index * REFRESH_TICKS + REFRESH_TICKS * 320 / LCD_LINES_TOTAL;
    if (!lines_done && now >= lines_end)
    {
      lines_done = 1;
      refresh_lines_done();
    }
    else if (now >= (refresh_index + 1) * REFRESH_TICKS)
    {
      if (!lines_done)
        refresh_lines_done();
      refresh_index++;
      lines_done = 0;
      refresh_start();
    }
    else
      break;
  }
  run_events();
}

uint32_t host_hw_read(uint32_t reg)
{
  advance(READ_STEP);
  switch (reg)
  {
  case HW_TIMER_VALUE:
    return ~(uint32_t)(uint64_t)now;   /* counts down */
  case HW_TIMER_LOAD:
    return timer_load;
  case HW_TIMER_CONTROL:
    return timer_control;
  case HW_TIMER_CLOCK:
    return timer_clock;
  case HW_KEYPAD:
  case HW_KEYPAD + 4:
  case HW_KEYPAD + 8:
  case HW_KEYPAD + 12:
    return keys[(reg - HW_KEYPAD) / 4];
  case HW_LCD_TIMING0:
  case HW_LCD_TIMING1:
  case HW_LCD_TIMING2:
    return lcd_timing[(reg - HW_LCD_TIMING0) / 4];
  case HW_LCD_CONTROL:
    return lcd_control;
  case HW_LCD_RAW_IRQ:
    return lcd_raw_irq;
  case HW_BACKLIGHT:
    return backlight;
  default:
    fprintf(stderr, "gpsp-host: read of unknown register %08x\n", (unsigned)reg);
    return 0;
  }
}

void host_hw_write(uint32_t reg, uint32_t value)
{
  switch (reg)
  {
  case HW_TIMER_LOAD:
    timer_load = value;
    break;
  case HW_TIMER_CONTROL:
    timer_control = value;
    break;
  case HW_TIMER_CLOCK:
    timer_clock = value;
    break;
  case HW_LCD_TIMING0:
  case HW_LCD_TIMING1:
  case HW_LCD_TIMING2:
    lcd_timing[(reg - HW_LCD_TIMING0) / 4] = value;
    break;
  case HW_LCD_CONTROL:
    lcd_control = value;
    break;
  case HW_LCD_IRQ_CLEAR:
    lcd_raw_irq &= ~value;
    break;
  case HW_BACKLIGHT:
    if (value > 225)
    {
      fprintf(stderr, "gpsp-host: backlight set to %u, past the OS's darkest (225)\n", (unsigned)value);
      exit(4);
    }
    backlight = value;
    backlight_writes++;
    break;
  default:
    fprintf(stderr, "gpsp-host: write of unknown register %08x\n", (unsigned)reg);
  }
}

uintptr_t host_hw_lcd_base(void)
{
  return lcd_base;
}

void host_hw_set_lcd_base(uintptr_t base)
{
  lcd_base = base;
  base_writes++;
}

/* The line of the shown buffer the LCD reads now (past its end during the
 * porches). */
uintptr_t host_hw_lcd_current(void)
{
  double line;
  advance(READ_STEP);
  line = fmod(now, REFRESH_TICKS) / REFRESH_TICKS * LCD_LINES_TOTAL;
  if (line >= 320)
    return shown_base + PORTRAIT_BYTES;
  return shown_base + (uintptr_t)line * 480;
}

/* ---- What takes time on the calculator (the linker's --wrap) ---- */

void __real_execute_arm(uint32_t cycles);
void __real_update_scanline(void);
void __real_nspire_turn_raw(uint16_t *dst, const uint16_t *src, uint32_t pitch);
void __real_nspire_turn_1x(uint16_t *dst, const uint16_t *src, uint32_t pitch);
void __real_nspire_turn_rect_555(uint16_t *dst, const uint16_t *src, uint32_t pitch, uint32_t x0,
                                 uint32_t y0, uint32_t w, uint32_t h);
void __real_nspire_turn_rect_565(uint16_t *dst, const uint16_t *src, uint32_t pitch, uint32_t x0,
                                 uint32_t y0, uint32_t w, uint32_t h);

void __wrap_execute_arm(uint32_t cycles)
{
  __real_execute_arm(cycles);
  advance(costs[COST_FRAME]);
}

void __wrap_update_scanline(void)
{
  __real_update_scanline();
  if (!skip_next_frame)
    advance(costs[COST_LINE]);
}

static void turn_cost(uint32_t pixels)
{
  advance(costs[COST_TURN] * pixels / (320.0 * 240.0));
}

void __wrap_nspire_turn_raw(uint16_t *dst, const uint16_t *src, uint32_t pitch)
{
  __real_nspire_turn_raw(dst, src, pitch);
  turn_cost(320 * 216);
}

void __wrap_nspire_turn_1x(uint16_t *dst, const uint16_t *src, uint32_t pitch)
{
  __real_nspire_turn_1x(dst, src, pitch);
  turn_cost(240 * 160);
}

void __wrap_nspire_turn_rect_555(uint16_t *dst, const uint16_t *src, uint32_t pitch, uint32_t x0,
                                 uint32_t y0, uint32_t w, uint32_t h)
{
  __real_nspire_turn_rect_555(dst, src, pitch, x0, y0, w, h);
  turn_cost(w * h);
}

void __wrap_nspire_turn_rect_565(uint16_t *dst, const uint16_t *src, uint32_t pitch, uint32_t x0,
                                 uint32_t y0, uint32_t w, uint32_t h)
{
  __real_nspire_turn_rect_565(dst, src, pitch, x0, y0, w, h);
  turn_cost(w * h);
}

/* Reading ROM files (and unpacking zips) costs what the script says a MB. */
struct RFILE;
struct zip_rom;
int64_t __real_filestream_read(struct RFILE *stream, void *data, int64_t len);
int __real_zip_read(struct zip_rom *zip, uint8_t *dst, uint32_t n);

int64_t __wrap_filestream_read(struct RFILE *stream, void *data, int64_t len)
{
  int64_t got = __real_filestream_read(stream, data, len);
  if (got > 0)
    advance(costs[COST_READ] * (double)got / (1024.0 * 1024.0));
  return got;
}

int __wrap_zip_read(struct zip_rom *zip, uint8_t *dst, uint32_t n)
{
  int result = __real_zip_read(zip, dst, n);
  advance(costs[COST_READ] * (double)n / (1024.0 * 1024.0));
  return result;
}

/* The blended pictures cost what turning as many pixels does (roughly so
 * on the calculator, tests/arm and nspire_blend.c). */
void __real_nspire_blend_turn_aspect(uint16_t *dst, const uint16_t *src, uint16_t *prev);
void __real_nspire_blend_turn_full(uint16_t *dst, const uint16_t *src, uint16_t *prev);

void __wrap_nspire_blend_turn_aspect(uint16_t *dst, const uint16_t *src, uint16_t *prev)
{
  __real_nspire_blend_turn_aspect(dst, src, prev);
  turn_cost(320 * 216);
}

void __wrap_nspire_blend_turn_full(uint16_t *dst, const uint16_t *src, uint16_t *prev)
{
  __real_nspire_blend_turn_full(dst, src, prev);
  turn_cost(320 * 240);
}

/* ---- Ndless's functions ---- */

static int held(unsigned key)
{
  return (keys[(key - 1) >> 5] >> ((key - 1) & 31)) & 1;
}

/* The arrows and the click are the touchpad's: gpSP masks them out of the
 * keypad registers and reads them here, as on a CX II. */
int touchpad_scan(touchpad_report_t *report)
{
  int up = held(113), right = held(115), down = held(117), left = held(119);
  advance(costs[COST_TOUCHPAD]);
  memset(report, 0, sizeof(*report));
  if (held(98))
    report->arrow = TPAD_ARROW_CLICK;
  else if (up && right)
    report->arrow = TPAD_ARROW_UPRIGHT;
  else if (right && down)
    report->arrow = TPAD_ARROW_RIGHTDOWN;
  else if (down && left)
    report->arrow = TPAD_ARROW_DOWNLEFT;
  else if (left && up)
    report->arrow = TPAD_ARROW_LEFTUP;
  else if (up)
    report->arrow = TPAD_ARROW_UP;
  else if (right)
    report->arrow = TPAD_ARROW_RIGHT;
  else if (down)
    report->arrow = TPAD_ARROW_DOWN;
  else if (left)
    report->arrow = TPAD_ARROW_LEFT;
  report->pressed = report->arrow != TPAD_ARROW_NONE;
  return 0;
}

int on_key_pressed(void)
{
  advance(READ_STEP);
  return on_key;
}

void cfg_register_fileext(const char *ext, const char *prgm)
{
  size_t len = strlen(registered);
  snprintf(registered + len, sizeof(registered) - len, "%s%s=%s", len ? "," : "", ext, prgm);
}

int NU_Current_Dir(const char *drive, char *path)
{
  (void)drive;
  return getcwd(path, 512) ? 0 : -1;
}

/* ---- Setting up ---- */

static unsigned key_number(const char *name)
{
  static const struct { const char *name; unsigned key; } extra[] =
  {
    { "up", 113 }, { "right", 115 }, { "down", 117 }, { "left", 119 }, { "click", 98 }, { "on", 0 },
  };
  unsigned i;
  for (i = 0; i < sizeof(extra) / sizeof(extra[0]); i++)
    if (!strcasecmp(name, extra[i].name))
      return extra[i].key;
  for (i = 1; i <= 128; i++)
    if (nspire_key_strings[i] && !strcasecmp(name, nspire_key_strings[i]))
      return i;
  fprintf(stderr, "gpsp-host: unknown key %s\n", name);
  exit(2);
}

static void add_event(double time, enum event_type type, unsigned key, double value, const char *name)
{
  events = realloc(events, sizeof(*events) * (num_events + 1));
  events[num_events].time = time;
  events[num_events].type = type;
  events[num_events].key = key;
  events[num_events].value = value;
  snprintf(events[num_events].name, sizeof(events[num_events].name), "%s", name ? name : "");
  num_events++;
}

static int by_time(const void *a, const void *b)
{
  const struct event *x = a, *y = b;
  if (x->time != y->time)
    return x->time < y->time ? -1 : 1;
  return x < y ? -1 : 1;
}

static void read_script(const char *path)
{
  char line[300], word[64], arg[200], arg2[64];
  double t = 0;
  FILE *f = fopen(path, "r");

  if (!f)
  {
    fprintf(stderr, "gpsp-host: can't read %s\n", path);
    exit(2);
  }
  while (fgets(line, sizeof(line), f))
  {
    int n;
    char *hash = strchr(line, '#');
    if (hash)
      *hash = 0;
    arg[0] = arg2[0] = 0;
    n = sscanf(line, "%63s %199s %63s", word, arg, arg2);
    if (n < 1)
      continue;
    if (!strcmp(word, "wait"))
      t += atof(arg) * TICK_HZ;
    else if (!strcmp(word, "press") || !strcmp(word, "tap"))
    {
      int tap = !strcmp(word, "tap");
      unsigned key = key_number(arg);
      add_event(t, EV_DOWN, key, 0, NULL);
      add_event(t + (tap ? 0.03 : 0.1) * TICK_HZ, EV_UP, key, 0, NULL);
      t += (tap ? 0.1 : 0.25) * TICK_HZ;
    }
    else if (!strcmp(word, "hold"))
      add_event(t, EV_DOWN, key_number(arg), 0, NULL);
    else if (!strcmp(word, "release"))
      add_event(t, EV_UP, key_number(arg), 0, NULL);
    else if (!strcmp(word, "shot"))
      add_event(t, EV_SHOT, 0, 0, arg);
    else if (!strcmp(word, "note"))
      add_event(t, EV_NOTE, 0, 0, arg);
    else if (!strcmp(word, "cost"))
    {
      static const char *const kinds[COSTS] = { "frame", "line", "turn", "touchpad", "read" };
      unsigned kind;
      for (kind = 0; kind < COSTS && strcmp(arg, kinds[kind]); kind++)
        ;
      if (kind == COSTS || n < 3)
      {
        fprintf(stderr, "gpsp-host: unknown cost: %s", line);
        exit(2);
      }
      /* Lines cost microseconds, the rest milliseconds. */
      add_event(t, EV_COST, kind, atof(arg2) * MS / (kind == COST_LINE ? 1000.0 : 1.0), NULL);
    }
    else if (!strcmp(word, "backlight"))
      backlight = backlight_at_start = (uint32_t)atoi(arg);
    else if (!strcmp(word, "quit"))
      add_event(t, EV_QUIT, 0, 0, NULL);
    else
    {
      fprintf(stderr, "gpsp-host: unknown script line: %s", line);
      exit(2);
    }
  }
  fclose(f);
  /* Stable: events at the same time keep the script's order. */
  qsort(events, num_events, sizeof(*events), by_time);
}

static void timed_out(int sig)
{
  (void)sig;
  fprintf(stderr, "gpsp-host: no progress (stuck at %.2f s of virtual time)\n", now / TICK_HZ);
  write_report();
  _exit(3);
}

int main(int argc, char *argv[])
{
  static char exe[600];
  char *args[3];

  if (argc < 4)
  {
    fprintf(stderr, "usage: gpsp-host SCRIPT OUTDIR GAMEDIR [ROM]\n");
    return 2;
  }
  read_script(argv[1]);
  if (!realpath(argv[2], out_dir))
  {
    fprintf(stderr, "gpsp-host: no folder %s\n", argv[2]);
    return 2;
  }
  if (chdir(argv[3]) != 0)
  {
    fprintf(stderr, "gpsp-host: no folder %s\n", argv[3]);
    return 2;
  }
  if (!getcwd(exe, sizeof(exe) - 20))
    return 2;
  strcat(exe, "/gpsp_qol.tns");
  args[0] = exe;
  args[1] = argc > 4 ? argv[4] : NULL;
  args[2] = NULL;

  shown_hash = hash_bytes((const void *)shown_base, PORTRAIT_BYTES);
  atexit(write_report);
  signal(SIGALRM, timed_out);
  alarm(120);   /* wall-clock seconds: a script that hangs fails */
  return gpsp_main(argc > 4 ? 2 : 1, args);
}
