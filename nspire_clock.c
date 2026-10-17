/* The CX II's CPU speed, taken from PocketSNES for the Nspire.
 *
 * The power controller (0x90140000) holds the clock multiplier of the 12 MHz
 * base clock in bits 24-29 of 0x90140030 (the OS sets 33, 396 MHz, or 24,
 * 288 MHz while USB is plugged in); memory speeds up with it. Writing the
 * register alone changes nothing. The OS's own switch puts normal memory to
 * sleep (self-refresh), then for each multiplier step writes the register,
 * writes 0x10000100 to power controller register 0x20 and waits for its
 * interrupt, then wakes the memory. nspire/clock_switch_nspire.S does the
 * same from the on-chip SRAM, because normal memory sleeps meanwhile; that
 * is also why the LCD reads the OS's own buffer (on-chip memory) during a
 * switch. Every switch is checked by timing a loop against the 32 kHz timer.
 *
 * Too high a speed can freeze the calculator, so while a game runs at a
 * raised speed, a marker file (gpsp_clock.tns, next to gpSP) names the speed
 * and the game; leaving cleanly removes it, and if it is still there at the
 * next start that speed is put back to normal. */

#include "common.h"
#include "nspire.h"
#include "nspire_clock.h"
#include "nspire_paths.h"
#include "nspire_frontend.h"

#include <stdio.h>
#include <string.h>

#define CLOCK_CONTROL  (*(volatile u32 *)0x90140030)
#define PMU_COMMAND    (*(volatile u32 *)0x90140020)   /* only read here */
#define MEMC_STATE     (*(volatile u32 *)0x90120004)   /* bit 10: normal memory sleeps */
#define CLOCK_MULTIPLIER(value) ((int)(((value) >> 24) & 0x3F))
#define LCD_BASE       (*(volatile u32 *)0xC0000010)
#define LCD_CURRENT    (*(volatile u32 *)0xC000002C)   /* where the LCD reads now */

/* The second SP804 dual timer, at 32768 Hz: the switch gives up on a step
 * that doesn't finish when it runs out. It belongs to the OS (and Ndless's
 * msleep), so it is put back afterwards. */
#define T2_LOAD    (*(volatile u32 *)0x900D0000)
#define T2_CONTROL (*(volatile u32 *)0x900D0008)
#define T2_CLEAR   (*(volatile u32 *)0x900D000C)
#define T2_SPEED   (*(volatile u32 *)0x900D0080)

/* Where the switch runs in the SRAM: past the OS's code (0xA4000000) and
 * the MMU's page table (0xA4004000-0xA4007FFF), in 1 KB that was zero in
 * every dump. What's there is put back after each switch. */
#define SWITCH_SRAM       ((u8 *)0xA4035000)
#define SWITCH_STEP_TICKS 1092   /* a step may take 1/30 s */
extern const u8 clock_switch_code[], clock_switch_code_end[];

/* The OS's LCD buffer is on-chip memory there. */
#define ON_CHIP_LCD_START 0xA8000000u
#define ON_CHIP_LCD_END   0xA8040000u

#define CLOCK_MARKER "gpsp_clock.tns"

static const int multipliers[CPU_SPEEDS] = { 0, 34, 35, 36, 37, 38, 39, 40, 41, 42 };

char nspire_clock_crashed_rom[512];
static int crashed;               /* a raised speed froze the calculator last time */

static u32 os_clock_control;      /* the OS's setting, put back after a raised one */
static int clock_raised;
static u32 clock_mhz;             /* measured after the last switch, 0 if none */
static int failed_multiplier;     /* a raise that didn't happen isn't tried again */
static int shown_multiplier = -1; /* the speed last shown in a message */
static int marker_written;

static struct
{
  u32 before, target, after, result, ticks, mhz;
  const char *refused;   /* why it wasn't tried, or NULL */
} last_switch;

static u32 interrupts_off(void)
{
  u32 old, off;
  __asm__ volatile ("mrs %0, cpsr\n\torr %1, %0, #0xC0\n\tmsr cpsr_c, %1"
                    : "=r" (old), "=r" (off) : : "memory");
  return old;
}

static void interrupts_restore(u32 old)
{
  __asm__ volatile ("msr cpsr_c, %0" : : "r" (old) : "memory");
}

/* The CPU clock in MHz (rounded), from a loop of 4 * 'loops' cycles timed
 * with the 32 kHz timer: on the ARM926EJ-S, SUBS takes 1 cycle and a taken
 * branch 3, and the loop runs from the instruction cache, so memory speed
 * doesn't come into it. */
static u32 measure_mhz(u32 loops)
{
  u64 cycles = (u64)loops * 4;
  u32 cpsr = interrupts_off();
  u32 start = nspire_ticks(), ticks;
  __asm__ volatile ("1: subs %0, %0, #1\n\tbne 1b" : "+r" (loops) : : "cc");
  ticks = nspire_ticks() - start;
  interrupts_restore(cpsr);
  if (ticks == 0)
    return 0;
  return (u32)((cycles * NSPIRE_TICK_HZ + ticks * 500000ull) / ticks / 1000000);
}

#define QUICK_LOOPS 4000000   /* 16 million cycles: about 40 ms, to within 0.1% */

/* Within 2% of 12 MHz times the multiplier. */
static int clock_matches(u32 mhz, int multiplier)
{
  u32 want = (u32)multiplier * 12;
  return mhz * 50 >= want * 49 && mhz * 50 <= want * 51;
}

static void sync_code(void)
{
  __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4\n\t"   /* drain the write buffer */
                    "mcr p15, 0, %0, c7, c5, 0"        /* invalidate the instruction cache */
                    : : "r" (0) : "memory");
}

/* Points the LCD at the OS's buffer, with what is on the screen copied there
 * first, and waits until it reads from there. Returns 0 if it can't (then
 * normal memory mustn't sleep). */
static int lcd_to_on_chip(void)
{
  u32 os = (u32)os_screen, since;
  int polls;

  if (os < ON_CHIP_LCD_START || os >= ON_CHIP_LCD_END)
    return 0;
  if (LCD_BASE != os)
  {
    /* The OS's buffer turns pixels as they are written into it: a landscape
     * picture shows upright. gpSP's buffers on this LCD hold turned
     * pictures, so the one on screen is turned back as it is copied. */
    const u16 *shown = (const u16 *)LCD_BASE;
    volatile u16 *dst = (volatile u16 *)os;
    u32 x, p, line;
    if (nspire_lcd_portrait)
    {
      for (x = 0; x < 320; x++)
        for (p = 0; p < 240; p++)
          dst[(239 - p) * 320 + x] = shown[x * 240 + p];
      /* Reading the buffer brought it into the data cache; frames written
       * into it later must go to memory, where the LCD reads them. */
      for (line = (u32)shown & ~31u; line < (u32)shown + 320 * 240 * 2; line += 32)
        __asm__ volatile ("mcr p15, 0, %0, c7, c14, 1" : : "r" (line));
    }
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0) : "memory");
    LCD_BASE = os;
  }
  since = nspire_ticks();
  for (polls = 0; polls < 4000000; polls++)
  {
    u32 reading = LCD_CURRENT;
    if (reading >= os && reading < os + 320 * 240 * 2)
      return 1;
    if (nspire_ticks() - since > NSPIRE_TICK_HZ / 10)
      break;
  }
  return 0;
}

/* Switches the clock register to 'value' (only its multiplier may differ
 * from the register's now) the OS's way. Returns the measured clock in MHz,
 * 0 if it can't be measured. */
static u32 switch_clock(u32 value)
{
  static u8 saved[1024];
  u32 size = (u32)(clock_switch_code_end - clock_switch_code);
  u32 now = CLOCK_CONTROL, lcd = LCD_BASE, mhz;
  int running;

  if (now == value)
    return measure_mhz(QUICK_LOOPS);
  last_switch.before = now;
  last_switch.target = value;
  last_switch.result = last_switch.ticks = 0;
  last_switch.refused = NULL;
  if (size > sizeof(saved))
    last_switch.refused = "switch code too big";
  else if ((now & ~(0x3Fu << 24)) != (value & ~(0x3Fu << 24)) || (now & 0xFF) != 0x03)
    last_switch.refused = "clock register in an unknown mode";
  else if ((PMU_COMMAND & ~0x100u) != 0x10000000)
    last_switch.refused = "power controller in an unknown state";
  else if (MEMC_STATE & 0x400)
    last_switch.refused = "memory asleep";
  else if (!lcd_to_on_chip())
    last_switch.refused = "LCD reads normal memory";

  if (!last_switch.refused)
  {
    u32 cpsr = interrupts_off();
    u32 control = T2_CONTROL, load = T2_LOAD, speed = T2_SPEED, start;
    T2_CONTROL = 0;
    T2_SPEED = 0xA;   /* 32768 Hz, like the first timer */
    memcpy(saved, SWITCH_SRAM, size);
    memcpy(SWITCH_SRAM, clock_switch_code, size);
    sync_code();
    start = nspire_ticks();
    last_switch.result = ((u32 (*)(u32, u32))SWITCH_SRAM)(value, SWITCH_STEP_TICKS);
    last_switch.ticks = nspire_ticks() - start;
    memcpy(SWITCH_SRAM, saved, size);
    sync_code();
    T2_CONTROL = 0;
    T2_CLEAR = 1;
    T2_SPEED = speed;
    T2_CONTROL = control & 0x7F;
    T2_LOAD = load;
    T2_CONTROL = control;
    interrupts_restore(cpsr);
  }
  LCD_BASE = lcd;

  mhz = measure_mhz(QUICK_LOOPS);
  /* A step that didn't happen leaves the register ahead of the clock: it is
   * set to the clock the CPU runs at, so the OS's own switches (when it
   * idles) start from the truth. */
  running = (int)((mhz + 6) / 12);
  if (mhz && !clock_matches(mhz, CLOCK_MULTIPLIER(CLOCK_CONTROL)) && clock_matches(mhz, running))
    CLOCK_CONTROL = (CLOCK_CONTROL & ~(0x3Fu << 24)) | (u32)running << 24;
  last_switch.after = CLOCK_CONTROL;
  last_switch.mhz = mhz;
  return mhz;
}

static u32 clock_now_mhz(void)
{
  return clock_mhz ? clock_mhz : (u32)CLOCK_MULTIPLIER(CLOCK_CONTROL) * 12;
}

/* 0 puts back the calculator's own speed. Returns the clock in MHz. */
static u32 set_multiplier(int multiplier)
{
  u32 value;

  if (!is_cx2 || multiplier < 0 || multiplier > 63)
    return 0;
  if (!multiplier)
  {
    if (clock_raised)
    {
      clock_raised = 0;
      clock_mhz = switch_clock(os_clock_control);
    }
    return clock_now_mhz();
  }
  if (!clock_raised)
    os_clock_control = CLOCK_CONTROL;
  value = (os_clock_control & ~(0x3Fu << 24)) | (u32)multiplier << 24;
  if (!clock_raised || value != CLOCK_CONTROL)
    clock_mhz = switch_clock(value);
  clock_raised = 1;
  return clock_now_mhz();
}

static void marker_path(char *path, u32 size)
{
  nspire_save_file(path, size, CLOCK_MARKER);
}

void nspire_clock_check_crash(void)
{
  char path[600], line[600];
  FILE *f;

  marker_path(path, sizeof(path));
  f = fopen(path, "r");
  if (!f)
    return;
  line[0] = 0;
  if (fgets(line, sizeof(line), f) && fgets(line, sizeof(line), f))
    line[strcspn(line, "\r\n")] = 0;
  else
    line[0] = 0;
  fclose(f);
  remove(path);
  snprintf(nspire_clock_crashed_rom, sizeof(nspire_clock_crashed_rom), "%s", line);
  nspire_cpu_speed = 0;
  crashed = 1;
}

void nspire_clock_game(void)
{
  int multiplier = nspire_cpu_speed < CPU_SPEEDS ? multipliers[nspire_cpu_speed] : 0;
  u32 mhz;

  if (crashed)
  {
    crashed = 0;
    nspire_osd_message("A raised CPU speed froze it last time: back to normal");
    shown_multiplier = 0;
  }
  if (!is_cx2)
    return;
  if (multiplier != failed_multiplier)
    failed_multiplier = 0;
  if (multiplier && multiplier == failed_multiplier)
    multiplier = 0;

  if (multiplier && !marker_written)
  {
    char path[600];
    FILE *f;
    marker_path(path, sizeof(path));
    f = fopen(path, "w");
    if (!f)
      multiplier = 0;   /* no marker, no raised speed */
    else
    {
      fprintf(f, "%d\n%s\n", multiplier, gamepak_filename);
      fclose(f);
      marker_written = 1;
    }
  }

  mhz = set_multiplier(multiplier);
  if (multiplier && mhz && !clock_matches(mhz, multiplier))
  {
    /* The clock didn't get there: back to normal, and say so. */
    failed_multiplier = multiplier;
    mhz = set_multiplier(0);
    nspire_osd_message("CPU speed not raised (still %u MHz)", (unsigned)mhz);
  }
  else if (multiplier != shown_multiplier && (multiplier || shown_multiplier > 0) && mhz)
    nspire_osd_message(multiplier ? "CPU %u MHz" : "CPU %u MHz (normal)", (unsigned)mhz);
  shown_multiplier = multiplier;
}

void nspire_clock_normal(void)
{
  set_multiplier(0);
}

void nspire_clock_left_game(void)
{
  set_multiplier(0);
  if (marker_written)
  {
    char path[600];
    marker_path(path, sizeof(path));
    remove(path);
    marker_written = 0;
  }
}

u32 nspire_cpu_mhz(void)
{
  return measure_mhz(20000000);   /* 80 million cycles */
}

void nspire_clock_report(char *text, u32 size)
{
  u32 len;

  if (!is_cx2)
  {
    snprintf(text, size, "clock register: not a CX II");
    return;
  }
  snprintf(text, size, "clock register %08lx (x%d)", (unsigned long)CLOCK_CONTROL,
           CLOCK_MULTIPLIER(CLOCK_CONTROL));
  if (!last_switch.target)
    return;
  len = (u32)strlen(text);
  if (last_switch.refused)
    snprintf(text + len, size - len, "; last switch %08lx -> %08lx not tried: %s, %lu MHz",
             (unsigned long)last_switch.before, (unsigned long)last_switch.target,
             last_switch.refused, (unsigned long)last_switch.mhz);
  else
    snprintf(text + len, size - len,
             "; last switch %08lx -> %08lx: %lu steps%s%s%s%s, register %08lx, %lu MHz",
             (unsigned long)last_switch.before, (unsigned long)last_switch.target,
             (unsigned long)(last_switch.result & 0xFF),
             last_switch.result & 0x100 ? ", memory didn't sleep" : "",
             last_switch.result & 0x200 ? ", a step timed out" : "",
             last_switch.result & 0x400 ? ", memory didn't wake" : "",
             last_switch.result & 0x800 ? ", power interrupt stuck" : "",
             (unsigned long)last_switch.after, (unsigned long)last_switch.mhz);
}
