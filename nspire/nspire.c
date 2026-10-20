#include "common.h"
#include "nspire_hw.h"

/* Three screen buffers take turns: one is shown, one may still be waiting to
 * be shown at the LCD's next refresh, and the next frame is drawn into the
 * third (nspire_screen). */
#define SCREEN_PIXELS (320 * 240)
#define SCREENS 3
static u16 screens[SCREENS][SCREEN_PIXELS] __attribute__ ((aligned (32)));
static int drawing_screen;

void* nspire_displayed_screen;
void* nspire_screen;
u16 nspire_landscape[SCREEN_PIXELS] __attribute__ ((aligned (32)));
u16* os_screen;
u32 LCDTiming0;
u32 LCDTiming1;
u32 LCDControl;

int nspire_lcd_portrait;

/* The first SP804 dual timer runs free at 32768 Hz (as in PocketSNES and
 * nSDL); its settings are put back on exit. */
static u32 saved_timer_load, saved_timer_control, saved_timer_clock;
static int timer_works;
static u32 fallback_ticks;

/* The CX II LCD scans 320 lines of 240 pixels, 16-bit 5:6:5 (PL111 timing 2
 * gives the clocks per line, timing 1 the lines). Not in Ndless's
 * compatibility mode, which needs the build marked --240x320-support true. */
static int nspire_lcd_is_portrait(void)
{
	u32 clocks_per_line = ((HW_READ(HW_LCD_TIMING2) >> 16) & 0x3FF) + 1;
	u32 lines = (HW_READ(HW_LCD_TIMING1) & 0x3FF) + 1;
	u32 bpp = (HW_READ(HW_LCD_CONTROL) >> 1) & 7;
	return clocks_per_line == 240 && lines == 320 && bpp == 6;
}

/* Writes back and drops a range's cached lines. */
static void flush_dcache_range(void *start, u32 size)
{
	uintptr_t addr = (uintptr_t)start & ~(uintptr_t)31, end = (uintptr_t)start + size;
	for (; addr < end; addr += 32)
		HW_FLUSH_DCACHE_LINE(addr);
	HW_DRAIN_WRITE_BUFFER();
}

static void timer_init(void)
{
	u32 first, i;

	saved_timer_load = HW_READ(HW_TIMER_LOAD);
	saved_timer_control = HW_READ(HW_TIMER_CONTROL);
	saved_timer_clock = HW_READ(HW_TIMER_CLOCK);
	HW_WRITE(HW_TIMER_CONTROL, 0);
	HW_WRITE(HW_TIMER_CLOCK, 0xA);          /* 32768 Hz */
	HW_WRITE(HW_TIMER_LOAD, 0xFFFFFFFF);
	HW_WRITE(HW_TIMER_CONTROL, 0x82);       /* on, free running, 32-bit, no interrupt */

	/* At 32 kHz the timer moves dozens of times during this loop. */
	first = HW_READ(HW_TIMER_VALUE);
	for (i = 0; i < 400000 && !timer_works; i++)
		timer_works = HW_READ(HW_TIMER_VALUE) != first;
}

static void timer_restore(void)
{
	HW_WRITE(HW_TIMER_CONTROL, 0);
	HW_WRITE(HW_TIMER_CLOCK, saved_timer_clock);
	HW_WRITE(HW_TIMER_LOAD, saved_timer_load);
	HW_WRITE(HW_TIMER_CONTROL, saved_timer_control);
}

u32 nspire_ticks(void)
{
	if (!timer_works)
		return fallback_ticks;
	return ~HW_READ(HW_TIMER_VALUE);   /* the timer counts down */
}

void nspire_wait_until(u32 tick)
{
	if (!timer_works)
	{
		/* Without a timer, time only moves by waiting: games run unthrottled. */
		if ((s32)(tick - fallback_ticks) > 0)
			fallback_ticks = tick;
		return;
	}
	/* Never more than a second, whatever was asked. */
	if ((s32)(tick - nspire_ticks()) > (s32)NSPIRE_TICK_HZ)
		return;
	while ((s32)(tick - nspire_ticks()) > 0)
		;
}

void nspire_init()
{
	int i;

	os_screen = (u16*)HW_LCD_BASE_GET();
	LCDTiming0 = HW_READ(HW_LCD_TIMING0);
	LCDTiming1 = HW_READ(HW_LCD_TIMING1);
	LCDControl = HW_READ(HW_LCD_CONTROL);
	timer_init();

	/* The CPU never reads the screen buffers on a portrait LCD, and its data
	 * cache only fills on reads, so they are kept out of the cache and only
	 * the write buffer needs draining before the LCD reads them. */
	memset(screens, 0, sizeof(screens));
	for (i = 0; i < SCREENS; i++)
		flush_dcache_range(screens[i], sizeof(screens[i]));
	drawing_screen = 1;
	nspire_displayed_screen = screens[0];
	nspire_screen = screens[1];

	nspire_lcd_portrait = nspire_lcd_is_portrait();
	if (nspire_lcd_portrait)
	{
		/* Keep the OS's portrait timings and 5:6:5 format; only the buffer
		 * address changes. Frames are written turned (nspire_turn.c). */
		set_display_buffer(nspire_displayed_screen);
		return;
	}
	HW_WRITE(HW_LCD_CONTROL, 0);
	HW_WRITE(HW_LCD_TIMING0, 0x251A004C);
	HW_WRITE(HW_LCD_TIMING1, 0x110300EF);
	set_display_buffer(nspire_displayed_screen);
	u32 newControl = (LCDControl & ~0x90E) | 0x008;
	HW_WRITE(HW_LCD_CONTROL, newControl);
	HW_WRITE(HW_LCD_CONTROL, newControl | 0x800);
}

void nspire_restore()
{
	if (nspire_lcd_portrait)
		set_display_buffer(os_screen);
	else
	{
		HW_WRITE(HW_LCD_CONTROL, 0);
		set_display_buffer(os_screen);
		HW_WRITE(HW_LCD_TIMING0, LCDTiming0);
		HW_WRITE(HW_LCD_TIMING1, LCDTiming1);
		HW_WRITE(HW_LCD_CONTROL, LCDControl & ~0x800);
		HW_WRITE(HW_LCD_CONTROL, LCDControl);
	}
	timer_restore();
	enableAlignmentExceptions();
	/* The OS's file list doesn't see files gpSP moves or writes (zgpSP_saves,
	 * states): without this it kept showing files and folders that were gone. */
	refresh_osscr();
}

void set_display_buffer(void* buffer)
{
	HW_LCD_BASE_SET(buffer);
}

#if defined(NSPIRE_LIBRETRO)
extern u32 nspire_lcd_cache_clean_full;
extern u32 nspire_tear_free;
#endif

/* Shows the frame drawn into nspire_screen and moves nspire_screen on to the
 * next buffer. The LCD controller takes a new address at its next refresh.
 * With tear-free frames on, gpSP also waits until the panel is no longer
 * reading the buffer about to be drawn into (at most 1/25 s). That wait is
 * what keeps a fast frame from being scanned mid-draw; on a CX it can run
 * out the whole 1/25 s, so the menu defaults it off there. */
void nspire_flip(void)
{
	uintptr_t start, end;
	u32 since;
	int polls;

	if (nspire_lcd_portrait)
		HW_DRAIN_WRITE_BUFFER();
	else
		nspire_lcd_fb_cache_clean_for_display();
	set_display_buffer(nspire_screen);
	nspire_displayed_screen = nspire_screen;
	drawing_screen = (drawing_screen + 1) % SCREENS;
	nspire_screen = screens[drawing_screen];

	if (!nspire_tear_free)
		return;

	start = (uintptr_t)nspire_screen;
	end = start + sizeof(screens[0]);
	since = nspire_ticks();
	for (polls = 0; polls < 1000000; polls++)
	{
		uintptr_t reading = HW_LCD_CURRENT_GET();
		if (reading < start || reading >= end || nspire_ticks() - since > NSPIRE_TICK_HZ / 25)
			break;
	}
}

int nspire_screen_index(void)
{
	return drawing_screen;
}

/* Waits for the LCD's next vertical compare interrupt (menus run at the LCD's
 * pace). Frames are shown by nspire_flip. */
void update_at_vblank(void)
{
	u32 since = nspire_ticks();
	HW_WRITE(HW_LCD_IRQ_CLEAR, 4);
	while ((HW_READ(HW_LCD_RAW_IRQ) & 4) == 0)
	{
		if (nspire_ticks() - since > NSPIRE_TICK_HZ / 20)
			break;
	}
	HW_WRITE(HW_LCD_IRQ_CLEAR, 4);
}

void nspire_lcd_fb_cache_clean_for_display(void)
{
#if defined(NSPIRE_LIBRETRO)
	if (nspire_lcd_cache_clean_full)
		clean_dcache();
	else
#endif
		warm_cache_op_range(WOP_D_CLEAN, nspire_screen, NSPIRE_LCD_FB_SIZE_BYTES);
}

//#define VERY_SAFE_CACHE

int warm_cache_op_range(int op, void *addr, unsigned long size)
{
	void *end_addr = addr + size;
	switch (op)
	{
	case WOP_D_CLEAN:
#ifndef VERY_SAFE_CACHE
		addr = (void*)((uintptr_t)addr & ~(uintptr_t)0x1F);
		while(addr < end_addr)
		{
			HW_CLEAN_DCACHE_LINE(addr);
			addr += 32;
		}
		HW_DRAIN_WRITE_BUFFER();
#else
		clean_dcache();
#endif
		return 0;
	case WOP_I_INVALIDATE:
#ifndef VERY_SAFE_CACHE
		addr = (void*)((uintptr_t)addr & ~(uintptr_t)0x1F);
		while(addr < end_addr)
		{
			HW_INVALIDATE_ICACHE_LINE(addr);
			addr += 32;
		}
#else
		HW_INVALIDATE_ICACHE();
#endif
		return 0;
	}
	return -1;
}

/* Code written as data, anywhere (gui.c's trampolines on the stack): the
 * whole data cache is written back and the instruction cache emptied. */
void nspire_sync_written_code(void)
{
	clean_dcache();
	HW_INVALIDATE_ICACHE();
}

void nspire_jit_cache_sync(void *base, void *end)
{
	unsigned long size;
	char *b;
	char *e;

	if (!base || !end || end <= base)
		return;
	b = (char *)base;
	e = (char *)end;
	size = (unsigned long)(e - b);
	warm_cache_op_range(WOP_D_CLEAN, b, size);
	warm_cache_op_range(WOP_I_INVALIDATE, b, size);
}

unsigned months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

struct tm* localtime(const time_t* input_time)
{
	unsigned int time = *input_time;

	static struct tm tm_result;
	tm_result.tm_isdst = 0;

	tm_result.tm_sec = time % 60;
	time /= 60;

	tm_result.tm_min = time % 60;
	time /= 60;

	tm_result.tm_hour = time % 24;
	time /= 24;

	tm_result.tm_wday = (time + 4) % 7;

	unsigned leap = 0;
	tm_result.tm_year = 70 + (time / (365*4 + 1))*4;
	time %= (365*4 + 1);
	if (time >= (365*3 + 1))
	{
		time -= (365*3 + 1);
		tm_result.tm_year += 3;
	}
	else if (time >= 365*2)
	{
		time -= 365*2;
		tm_result.tm_year += 2;
		leap = 1;
	}
	else if (time >= 365)
	{
		time -= 365;
		tm_result.tm_year++;
	}
	tm_result.tm_yday = time;
	if (leap && time >= 31 + 29 - 1)
	{
		if (time == 31 + 29 - 1)
		{
			tm_result.tm_mon = 1;
			tm_result.tm_mday = 29;
			return &tm_result;
		}
		time--;
	}

	tm_result.tm_mon = 0;
	while(time >= months[tm_result.tm_mon])
		time -= months[tm_result.tm_mon++];
	tm_result.tm_mday = time + 1;
	return &tm_result;
}
