/* Nspire gpSP-style frameskip: auto (relative_frame_count), manual, off — no audio buffer. */

#include "common.h"
#include "main.h"
#include "cpu.h"
#include "nspire_frameskip.h"

static u32 frameskip_counter;

void nspire_frameskip_reset(void)
{
  num_skipped_frames = 0;
  frameskip_counter = 0;
  relative_frame_count = 0;
}

void nspire_frameskip_begin_frame(void)
{
  u32 used_frameskip = frameskip_value;

  skip_next_frame = 0;

  if (!synchronize_flag)
  {
    used_frameskip = 4;
    relative_frame_count = 2;
  }

  relative_frame_count--;

  if (relative_frame_count >= 0)
  {
    if (relative_frame_count > 0 &&
        current_frameskip_type == auto_frameskip &&
        num_skipped_frames < used_frameskip)
    {
      skip_next_frame = 1;
      num_skipped_frames++;
    }
    else
    {
      relative_frame_count = 0;
      num_skipped_frames = 0;
    }
  }
  else
  {
    if (synchronize_flag)
      while ((*(volatile unsigned *)0xC0000020 & 4) == 0) { }
  }

  if (current_frameskip_type == manual_frameskip)
  {
    frameskip_counter = (frameskip_counter + 1) % (used_frameskip + 1);
    if (random_skip)
    {
      if (frameskip_counter != (rand_gen() % (used_frameskip + 1)))
        skip_next_frame = 1;
    }
    else
    {
      if (frameskip_counter)
        skip_next_frame = 1;
    }
  }
}
