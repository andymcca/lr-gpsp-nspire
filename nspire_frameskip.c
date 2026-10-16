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
/* Fast forward with frameskip off: the game runs as fast as it can (or up
 * to its top speed, nspire_ff_max), and is kept at twice its speed or more
 * (or at the top speed) by drawing fewer pictures when drawing them all
 * would slow it down: without a top speed, frames are due every 0.48 GBA
 * frame times (124 a second), and at least one in FAST_FORWARD_MAX_SKIP + 1
 * frames is drawn. */
#define FAST_FORWARD_PERIOD (FRAME_PERIOD / 25u * 12u)
#define FAST_FORWARD_MAX_SKIP 7

/* The names of fast forward's top speeds (nspire_ff_max, in
 * nspire_config.c): 1.1x to 10.0x in tenths, then unlimited. */
const char *const nspire_ff_max_names[NSPIRE_FF_CHOICES] =
{
  "1.1x", "1.2x", "1.3x", "1.4x", "1.5x", "1.6x", "1.7x", "1.8x", "1.9x",
  "2.0x", "2.1x", "2.2x", "2.3x", "2.4x", "2.5x", "2.6x", "2.7x", "2.8x",
  "2.9x", "3.0x", "3.1x", "3.2x", "3.3x", "3.4x", "3.5x", "3.6x", "3.7x",
  "3.8x", "3.9x", "4.0x", "4.1x", "4.2x", "4.3x", "4.4x", "4.5x", "4.6x",
  "4.7x", "4.8x", "4.9x", "5.0x", "5.1x", "5.2x", "5.3x", "5.4x", "5.5x",
  "5.6x", "5.7x", "5.8x", "5.9x", "6.0x", "6.1x", "6.2x", "6.3x", "6.4x",
  "6.5x", "6.6x", "6.7x", "6.8x", "6.9x", "7.0x", "7.1x", "7.2x", "7.3x",
  "7.4x", "7.5x", "7.6x", "7.7x", "7.8x", "7.9x", "8.0x", "8.1x", "8.2x",
  "8.3x", "8.4x", "8.5x", "8.6x", "8.7x", "8.8x", "8.9x", "9.0x", "9.1x",
  "9.2x", "9.3x", "9.4x", "9.5x", "9.6x", "9.7x", "9.8x", "9.9x", "10.0x",
  "unlimited"
};

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

/* Fast forward counts game frames, against the GBA's 60 a second (so 2x
 * reads 120/60). */
static int counts_game_frames(void)
{
  return nspire_pace == PACE_FAST_FORWARD || (nspire_pace == PACE_NORMAL && !synchronize_flag);
}

/* Frames drawn per second at full speed with this frameskip; game frames
 * at normal speed in fast forward. */
static u32 fps_expected(void)
{
  u32 n = 0;
  if (!counts_game_frames() && current_frameskip_type == manual_frameskip)
    n = frameskip_value > 99 ? 99 : frameskip_value;
  return (5973u + 50u * (n + 1u)) / (100u * (n + 1u));
}

/* 16.16 timer ticks per frame at fast forward's top speed, or 0 without
 * one (the speed test's fast forward has none). */
static u32 fast_forward_limit(void)
{
  if (nspire_pace != PACE_NORMAL || nspire_ff_max >= NSPIRE_FF_UNLIMITED)
    return 0;
  return (u32)((u64)FRAME_PERIOD * 10u / (nspire_ff_max + 11u));
}

/* Fast forward's pace: ff_next is when this frame is due, at the top speed
 * or (without one) FAST_FORWARD_PERIOD. With a top speed it's only a cap:
 * a frame that's early waits for it, a late one just goes (no pictures are
 * left out to catch up, and at most a frame's lag is made up: the owner
 * wants every picture when the game can't reach the speed chosen). Without one, at most a
 * frame's lead or 4 frames' lag is kept, and pictures make way while the
 * game is behind. Returns how far ahead of it the game is (ticks). */
static s32 fast_forward_pace(void)
{
  u32 limit = fast_forward_limit();
  u32 now = nspire_ticks();
  s32 ahead;

  if (!ff_started)
  {
    ff_started = 1;
    ff_last_picture = now - FRAME_TICKS * 2;
    ff_next = now;
    ff_next_frac = 0;
    skipped_frames = 0;
  }
  ff_next_frac += limit ? limit : FAST_FORWARD_PERIOD;
  ff_next += ff_next_frac >> 16;
  ff_next_frac &= 0xFFFF;
  ahead = (s32)(ff_next - now);
  if (limit)
  {
    if (ahead > 0)
    {
      nspire_wait_until(ff_next);
      nspire_perf_totals.idle += nspire_ticks() - now;
    }
    else if (ahead < -(s32)FRAME_TICKS)
      ff_next = now - FRAME_TICKS;   /* up to a frame's lag made up, not more */
    return 0;
  }
  if (ahead > (s32)FRAME_TICKS)
    ff_next = now + FRAME_TICKS;
  else if (ahead < -(s32)(FRAME_TICKS * 4))
    ff_next = now - FRAME_TICKS * 4;
  return ahead;
}

/* Fast forward with frameskip off: whether this frame is drawn. A picture
 * is drawn once the LCD has taken the last one (nspire_flip_taken: it takes
 * one per refresh, so pictures drawn faster would never be shown), or two
 * refreshes after it whatever the LCD says; and not while the game is
 * behind its pace, unless FAST_FORWARD_MAX_SKIP frames in a row went
 * undrawn. */
static int fast_forward_draw(s32 ahead)
{
  u32 now = nspire_ticks();
  int draw = (nspire_flip_taken() || now - ff_last_picture >= FRAME_TICKS * 2) &&
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
    else
    {
      s32 ahead = fast_forward_pace();
      if (nspire_pace == PACE_FAST_FORWARD || current_frameskip_type == no_frameskip)
        draw = fast_forward_draw(ahead);
      else
      {
        draw = fast_forward_counter == 0;
        fast_forward_counter = (fast_forward_counter + 1) % FAST_FORWARD_DRAW_EVERY;
      }
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
