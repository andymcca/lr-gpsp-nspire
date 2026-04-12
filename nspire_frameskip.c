/* Frameskip for Ndless host: fixed interval matches libretro; auto modes use wall clock. */

#include "common.h"
#include "main.h"
#include "cpu.h"
#include "nspire_frameskip.h"

extern void get_ticks_us(u64 *ticks_return);

#define NSPIRE_FS_MAX 30

static u64 fs_t0_us;
static u32 virtual_fc;
static u32 fs_fixed_counter;
static u32 fs_fixed_rand_phase;
static u32 auto_burst_skips;

static u32 wall_frame_index(u64 now_us)
{
  if (!fs_t0_us)
    fs_t0_us = now_us;
  return (u32)((now_us - fs_t0_us) * 60ULL / 1000000ULL);
}

static u32 max_auto_burst(void)
{
  u32 m;

  if (current_frameskip_type == auto_frameskip)
  {
    m = frameskip_value;
    if (m == 0 || m > NSPIRE_FS_MAX)
      m = NSPIRE_FS_MAX;
    return m;
  }

  /* auto_threshold: higher threshold % => allow more consecutive skips */
  m = 1 + (101U - frameskip_threshold) * NSPIRE_FS_MAX / 100U;
  if (m > NSPIRE_FS_MAX)
    m = NSPIRE_FS_MAX;
  if (m < 1)
    m = 1;
  return m;
}

void nspire_frameskip_reset(void)
{
  fs_t0_us = 0;
  virtual_fc = 0;
  fs_fixed_counter = 0;
  fs_fixed_rand_phase = 0;
  auto_burst_skips = 0;
}

void nspire_frameskip_begin_frame(void)
{
  u64 now;

  get_ticks_us(&now);
  skip_next_frame = 0;

  if (current_frameskip_type == no_frameskip)
    return;

  if (current_frameskip_type == fixed_interval_frameskip)
  {
    u32 interval = frameskip_interval;
    if (interval > NSPIRE_FS_MAX)
      interval = NSPIRE_FS_MAX;

    if (random_skip)
    {
      u32 period = interval + 1;
      fs_fixed_rand_phase = (fs_fixed_rand_phase + 1) % period;
      if (fs_fixed_rand_phase != (rand_gen() % period))
        skip_next_frame = 1;
    }
    else if (interval == 0)
      skip_next_frame = 0;
    else
    {
      if (fs_fixed_counter < interval)
      {
        skip_next_frame = 1;
        fs_fixed_counter++;
      }
      else
      {
        skip_next_frame = 0;
        fs_fixed_counter = 0;
      }
    }
    return;
  }

  /* auto_frameskip and auto_threshold_frameskip */
  {
    u32 real_fc = wall_frame_index(now);
    u32 burst_cap = max_auto_burst();

    virtual_fc++;
    if (real_fc >= virtual_fc)
    {
      if (real_fc > virtual_fc && auto_burst_skips < burst_cap)
      {
        skip_next_frame = 1;
        auto_burst_skips++;
      }
      else
      {
        virtual_fc = real_fc;
        auto_burst_skips = 0;
      }
    }
    else
      auto_burst_skips = 0;
  }
}
