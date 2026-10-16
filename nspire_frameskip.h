#ifndef NSPIRE_FRAMESKIP_H
#define NSPIRE_FRAMESKIP_H

/* Frame pacing and frameskip (nspire_frameskip.c). Frames are paced with
 * the 32768 Hz timer at the GBA's own rate (59.73 a second); the LCD shows
 * each frame at its next refresh (nspire_flip). */

enum nspire_pace_mode
{
  PACE_NORMAL,              /* real speed, frameskip as set */
  PACE_UNCAPPED_DRAW_ALL,   /* speed test: as fast as it goes, every frame drawn */
  PACE_UNCAPPED_DRAW_NONE,  /* speed test: as fast as it goes, nothing drawn */
  PACE_FAST_FORWARD         /* speed test: fast forward with frameskip off */
};
extern enum nspire_pace_mode nspire_pace;

void nspire_frameskip_reset(void);
/* Before a frame: decides whether it is drawn (sets skip_next_frame) and
 * waits until it is due. */
void nspire_frameskip_begin_frame(void);
/* After a frame: counts it, and the time it took to show if drawn. */
void nspire_frameskip_end_frame(u32 output_ticks);

/* Frame timing totals since 'start', for the counters and the speed test. */
struct nspire_perf
{
  u32 start, frames, drawn, render, output, idle;
};
/* Per frame, in tenths: frames per second and milliseconds. */
struct nspire_perf_stats
{
  u32 game_fps, drawn_fps, cpu_ms, render_ms, output_ms, idle_ms;
};
extern struct nspire_perf nspire_perf_totals;
struct nspire_perf_stats nspire_perf_since(const struct nspire_perf *since, u32 now);

/* The detailed counter's lines are kept and written to gpsp_perf.txt.tns
 * (next to the program) when the menu opens or the game is left. */
void nspire_perf_log_flush(void);

#endif
