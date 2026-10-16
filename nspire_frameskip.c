/* Frame pacing, frameskip and frame timing (nspire_frameskip.h).
 *
 * gpSP used to wait for the LCD's vertical blank after every frame, drawn or
 * skipped, which tied the game to the LCD's refresh rate and kept automatic
 * frameskip from ever skipping. Now frames are paced with the 32768 Hz timer
 * at the GBA's 59.73 frames a second, and nspire_flip shows each one at the
 * LCD's next refresh without waiting. */

#include "common.h"
#include "main.h"
#include "cpu.h"
#include "nspire.h"
#include "nspire_frameskip.h"
#include "nspire_frontend.h"
#include "nspire_paths.h"

#include <stdio.h>
#include <string.h>

/* A GBA frame is 280896 cycles at 16.78 MHz. In 16.16 fixed-point ticks of
 * the 32768 Hz timer that is 280896 * 32768 / 2^24 * 2^16 = 280896 * 128. */
#define FRAME_PERIOD (280896u * 128u)
#define FRAME_TICKS  (FRAME_PERIOD >> 16)
/* Fast forward draws one frame in this many (as gpSP always did), unless
 * frameskip is off. */
#define FAST_FORWARD_DRAW_EVERY 5
/* Fast forward with frameskip off: the game runs as fast as it can, and is
 * kept at twice its speed or more by drawing fewer pictures when drawing
 * them all would slow it down: frames are due every 0.48 GBA frame times
 * (124 a second, so the counter shows 120 or more), and at least one in
 * FAST_FORWARD_MAX_SKIP + 1 frames is drawn. */
#define FAST_FORWARD_PERIOD (FRAME_PERIOD / 25u * 12u)
#define FAST_FORWARD_MAX_SKIP 7

enum nspire_pace_mode nspire_pace = PACE_NORMAL;

static u32 next_frame, next_frame_frac;
static u32 skipped_frames, manual_counter, fast_forward_counter;
static u32 ff_last_picture, ff_next, ff_next_frac;
static int ff_started;

struct nspire_perf nspire_perf_totals;
static struct nspire_perf perf_window;   /* totals at the last counter update */
u32 nspire_render_ticks;
int nspire_perf_on;

static u32 fps_start, fps_frames, fps_shown;
static int fps_started;

#define PERF_LOG_LINES 600
static char perf_log[PERF_LOG_LINES][96];
static int perf_log_count;

void nspire_reset_timing(void)
{
  next_frame = nspire_ticks();
  next_frame_frac = 0;
  skipped_frames = 0;
  ff_started = 0;
  fps_started = 0;
  memset(&nspire_perf_totals, 0, sizeof(nspire_perf_totals));
  nspire_perf_totals.start = next_frame;
  perf_window = nspire_perf_totals;
  nspire_render_ticks = 0;
  num_skipped_frames = 0;
  relative_frame_count = 0;
}

void nspire_frameskip_reset(void)
{
  nspire_reset_timing();
}

/* Called by the core's reset_gba. */
void nspire_fps_overlay_reset(void)
{
  fps_started = 0;
  fps_shown = 0;
}

struct nspire_perf_stats nspire_perf_since(const struct nspire_perf *since, u32 now)
{
  struct nspire_perf_stats s;
  u32 elapsed = now - since->start;
  u32 frames = nspire_perf_totals.frames - since->frames;
  u32 render = nspire_perf_totals.render - since->render;
  u32 output = nspire_perf_totals.output - since->output;
  u32 idle = nspire_perf_totals.idle - since->idle;
  u32 cpu = elapsed > render + output + idle ? elapsed - render - output - idle : 0;
  u64 per_frame = (u64)NSPIRE_TICK_HZ * (frames ? frames : 1);

  s.game_fps = elapsed ? (u32)((u64)frames * NSPIRE_TICK_HZ * 10 / elapsed) : 0;
  s.drawn_fps = elapsed ? (u32)((u64)(nspire_perf_totals.drawn - since->drawn) * NSPIRE_TICK_HZ * 10 / elapsed) : 0;
  s.cpu_ms = (u32)(cpu * 10000ull / per_frame);
  s.render_ms = (u32)(render * 10000ull / per_frame);
  s.output_ms = (u32)(output * 10000ull / per_frame);
  s.idle_ms = (u32)(idle * 10000ull / per_frame);
  return s;
}

#define TENTHS(v) (unsigned)((v) / 10), (unsigned)((v) % 10)

void nspire_perf_log_flush(void)
{
  char path[600];
  FILE *f;
  int i;

  if (!perf_log_count)
    return;
  /* A new file each time: appending has garbled files on the calculator. */
  nspire_save_file(path, sizeof(path), "gpsp_perf.txt.tns");
  f = fopen(path, "w");
  if (f)
  {
    fprintf(f, "%s (%s): every second, ms per frame\n", gamepak_title, gamepak_code);
    for (i = 0; i < perf_log_count; i++)
      fprintf(f, "%s\n", perf_log[i]);
    fclose(f);
  }
  perf_log_count = 0;
}

/* Fast forward with frameskip off counts game frames against twice the
 * GBA's speed (see fast_forward_draw). */
static int counts_game_frames(void)
{
  return nspire_pace == PACE_FAST_FORWARD ||
         (nspire_pace == PACE_NORMAL && !synchronize_flag && current_frameskip_type == no_frameskip);
}

/* Frames drawn per second at full speed with this frameskip; in fast
 * forward, at normal speed (it draws one frame in FAST_FORWARD_DRAW_EVERY),
 * as PocketSNES counts it; with frameskip off, game frames at twice the
 * speed. */
static u32 fps_expected(void)
{
  u32 n = 0;
  if (counts_game_frames())
    return 120;
  if (!synchronize_flag)
    n = FAST_FORWARD_DRAW_EVERY - 1;
  else if (current_frameskip_type == manual_frameskip)
    n = frameskip_value > 99 ? 99 : frameskip_value;
  return (5973u + 50u * (n + 1u)) / (100u * (n + 1u));
}

/* Fast forward with frameskip off: whether this frame is drawn. A picture
 * is drawn once the LCD has taken the last one (nspire_flip_taken: it takes
 * one per refresh, so pictures drawn faster would never be shown), or two
 * refreshes after it whatever the LCD says; and not while the game is
 * behind its fast forward speed (ff_next, when this frame is due, with at
 * most a frame's lead or 4 frames' lag kept), unless FAST_FORWARD_MAX_SKIP
 * frames in a row went undrawn. */
static int fast_forward_draw(void)
{
  u32 now = nspire_ticks();
  s32 ahead;
  int draw;

  if (!ff_started)
  {
    ff_started = 1;
    ff_last_picture = now - FRAME_TICKS * 2;
    ff_next = now;
    ff_next_frac = 0;
    skipped_frames = 0;
  }
  ff_next_frac += FAST_FORWARD_PERIOD;
  ff_next += ff_next_frac >> 16;
  ff_next_frac &= 0xFFFF;
  ahead = (s32)(ff_next - now);
  if (ahead > (s32)FRAME_TICKS)
    ff_next = now + FRAME_TICKS;
  else if (ahead < -(s32)(FRAME_TICKS * 4))
    ff_next = now - FRAME_TICKS * 4;

  draw = (nspire_flip_taken() || now - ff_last_picture >= FRAME_TICKS * 2) &&
         (ahead >= 0 || skipped_frames >= FAST_FORWARD_MAX_SKIP);
  if (draw)
    ff_last_picture = now;
  return draw;
}

/* The counters' text, once a second (detailed) or twice (frames). */
static void update_counters(u32 now)
{
  u32 expected;

  nspire_perf_on = nspire_fps_overlay == 2 || nspire_pace != PACE_NORMAL;
  if (!nspire_fps_overlay)
  {
    nspire_fps_text[0] = 0;
    nspire_perf_text[0] = 0;
  }

  if (now - perf_window.start >= NSPIRE_TICK_HZ)
  {
    struct nspire_perf_stats s = nspire_perf_since(&perf_window, now);
    if (nspire_fps_overlay == 2)
    {
      snprintf(nspire_perf_text, sizeof(nspire_perf_text),
               "%u.%u fps  cpu %u.%u gfx %u.%u out %u.%u idle %u.%u", TENTHS(s.game_fps),
               TENTHS(s.cpu_ms), TENTHS(s.render_ms), TENTHS(s.output_ms), TENTHS(s.idle_ms));
      if (nspire_pace == PACE_NORMAL && perf_log_count < PERF_LOG_LINES)
        snprintf(perf_log[perf_log_count++], sizeof(perf_log[0]),
                 "game %u.%u fps, drawn %u.%u; cpu %u.%u, gfx %u.%u, out %u.%u, idle %u.%u",
                 TENTHS(s.game_fps), TENTHS(s.drawn_fps), TENTHS(s.cpu_ms), TENTHS(s.render_ms),
                 TENTHS(s.output_ms), TENTHS(s.idle_ms));
    }
    else
      nspire_perf_text[0] = 0;
    perf_window = nspire_perf_totals;
    perf_window.start = now;
  }

  if (nspire_fps_overlay)
  {
    /* The same in fast forward, against its own expected count. */
    expected = fps_expected();
    snprintf(nspire_fps_text, sizeof(nspire_fps_text), "%u/%u", (unsigned)fps_shown, (unsigned)expected);
    /* Green at full speed, amber below, red below three quarters. */
    nspire_fps_color = nspire_osd_color(fps_shown + 1 >= expected ? OSD_GOOD :
                                        fps_shown * 4 >= expected * 3 ? OSD_WARN : OSD_BAD);
  }
}

void nspire_frameskip_begin_frame(void)
{
  int draw;
  s32 late;
  u32 now;

  if (nspire_pace != PACE_NORMAL || !synchronize_flag)
  {
    if (nspire_pace == PACE_UNCAPPED_DRAW_ALL)
      draw = 1;
    else if (nspire_pace == PACE_UNCAPPED_DRAW_NONE)
      draw = 0;
    else if (nspire_pace == PACE_FAST_FORWARD || current_frameskip_type == no_frameskip)
      draw = fast_forward_draw();
    else
    {
      draw = fast_forward_counter == 0;
      fast_forward_counter = (fast_forward_counter + 1) % FAST_FORWARD_DRAW_EVERY;
    }
    next_frame = nspire_ticks();
    next_frame_frac = 0;
  }
  else
  {
    next_frame_frac += FRAME_PERIOD;
    next_frame += next_frame_frac >> 16;
    next_frame_frac &= 0xFFFF;
    now = nspire_ticks();
    late = (s32)(now - next_frame);

    switch (current_frameskip_type)
    {
    case auto_frameskip:
      draw = late <= 0 || skipped_frames >= frameskip_value;
      break;
    case manual_frameskip:
      manual_counter = (manual_counter + 1) % (frameskip_value + 1);
      if (random_skip)
        draw = manual_counter == rand_gen() % (frameskip_value + 1);
      else
        draw = manual_counter == 0;
      break;
    default:
      draw = 1;
      break;
    }

    if (late < 0)
    {
      nspire_wait_until(next_frame);
      nspire_perf_totals.idle += nspire_ticks() - now;
    }
    else if (late > (s32)FRAME_TICKS * 6)
    {
      /* Too far behind to catch up: carry on from now. */
      next_frame = now;
      next_frame_frac = 0;
    }
  }

  skipped_frames = draw ? 0 : skipped_frames + 1;
  skip_next_frame = !draw;
}

void nspire_frameskip_end_frame(u32 output_ticks)
{
  u32 now = nspire_ticks();

  nspire_perf_totals.frames++;
  nspire_perf_totals.render += nspire_render_ticks;
  nspire_render_ticks = 0;
  if (!skip_next_frame)
  {
    nspire_perf_totals.drawn++;
    nspire_perf_totals.output += output_ticks;
  }

  /* Frames drawn per second (game frames in fast forward with frameskip
   * off), over half a second. */
  if (!skip_next_frame || counts_game_frames())
  {
    if (!fps_started)
    {
      fps_started = 1;
      fps_start = now;
      fps_frames = 0;
    }
    else if (++fps_frames && now - fps_start >= NSPIRE_TICK_HZ / 2)
    {
      fps_shown = (fps_frames * NSPIRE_TICK_HZ + (now - fps_start) / 2) / (now - fps_start);
      fps_start = now;
      fps_frames = 0;
    }
  }
  update_counters(now);
}
