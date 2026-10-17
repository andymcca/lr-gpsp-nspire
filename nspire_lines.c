/* Lines of the picture drawn in batches (video.h): see DEVNOTES.md, "Speed
 * (2026-10-16)". update_gba (main.c) hands each line over at its HBlank;
 * the renderer (update_scanline) draws the waiting ones when the game next
 * changes what the picture is made of, or at VBlank. */
#include "common.h"
#include "main.h"
#include "video.h"

/* nspire_lines_waiting lines from first_waiting on are ready to draw;
 * while there are any, the dynarec's
 * stores to palette, VRAM and OAM go through arm_stub.S's *_flush handlers
 * (st_flush_functions), which draw them before storing. Display register
 * writes (write_io_register16), C stores (write_memory) and DMAs into those
 * areas check nspire_lines_waiting themselves. */
u32 nspire_lines_waiting;
static u32 first_waiting;
u32 nspire_batch_lines = 1;   /* 0: each line at its HBlank (tests compare) */

#if defined(HAVE_DYNAREC) && defined(ARM_ARCH)
extern u32 st_lookup_tables[4][17];
extern u32 st_handler_functions[4][17];
extern u32 st_flush_functions[4][3];

/* Table entries 6-8: regions 0x05-0x07 (entry 0 is for addresses > 0x0F). */
static void store_handlers(int flush)
{
  int t, i;
  for (t = 0; t < 4; t++)
    for (i = 0; i < 3; i++)
      st_lookup_tables[t][6 + i] = flush ? st_flush_functions[t][i] : st_handler_functions[t][6 + i];
}
#else
#define store_handlers(flush) ((void)0)
#endif

/* A frame that isn't shown: as before (the renderers return early). */
NSPIRE_HOT void nspire_video_line_ready(u32 vcount)
{
  if (skip_next_frame || !nspire_batch_lines)
  {
    update_scanline();
    return;
  }
  if (!nspire_lines_waiting)
  {
    first_waiting = vcount;
    store_handlers(1);
  }
  nspire_lines_waiting++;
}

/* The renderers read the line number from REG_VCOUNT. */
NSPIRE_HOT void nspire_flush_lines(void)
{
  u32 n = nspire_lines_waiting, vcount = read_ioreg(REG_VCOUNT), i;

  if (!n)
    return;
  nspire_lines_waiting = 0;
  store_handlers(0);
  for (i = 0; i < n; i++)
  {
    write_ioreg(REG_VCOUNT, first_waiting + i);
    update_scanline();
  }
  write_ioreg(REG_VCOUNT, vcount);
}

/* A reset or a loaded state: the waiting lines belong to a picture that
 * isn't there any more. */
void nspire_video_lines_reset(void)
{
  nspire_lines_waiting = 0;
  store_handlers(0);
}
