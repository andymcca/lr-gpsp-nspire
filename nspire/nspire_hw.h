/* Hardware access for the frontend: the calculator's registers and cache
 * operations, or, in the PC test build (NSPIRE_HOST), a simulated LCD,
 * timer and keypad (tests/host/host_hw.c). */
#ifndef NSPIRE_HW_H
#define NSPIRE_HW_H

#include <stdint.h>

/* Registers used here (PL111 LCD controller, first SP804 timer, keypad). */
#define HW_LCD_TIMING0   0xC0000000u
#define HW_LCD_TIMING1   0xC0000004u
#define HW_LCD_TIMING2   0xC0000008u
#define HW_LCD_BASE      0xC0000010u
#define HW_LCD_CONTROL   0xC0000018u
#define HW_LCD_RAW_IRQ   0xC0000020u   /* bit 2: vertical compare (once a refresh) */
#define HW_LCD_IRQ_CLEAR 0xC0000028u
#define HW_LCD_CURRENT   0xC000002Cu   /* the address the LCD reads now */
#define HW_TIMER_LOAD    0x900C0000u
#define HW_TIMER_VALUE   0x900C0004u
#define HW_TIMER_CONTROL 0x900C0008u
#define HW_TIMER_CLOCK   0x900C0080u
#define HW_KEYPAD        0x900E0010u   /* four 32-bit words of key bits */
/* CX II LCD backlight: the OS writes 0 (brightest) to 225 (darkest) here
 * (Hackspire, "Memory-mapped I/O ports on CX II", 90130000). */
#define HW_BACKLIGHT     0x90130014u

#ifdef NSPIRE_HOST

uint32_t host_hw_read(uint32_t reg);
void host_hw_write(uint32_t reg, uint32_t value);
/* Addresses are pointers, 64-bit on a PC. */
uintptr_t host_hw_lcd_base(void);
void host_hw_set_lcd_base(uintptr_t base);
uintptr_t host_hw_lcd_current(void);

#define HW_READ(reg)          host_hw_read(reg)
#define HW_WRITE(reg, value)  host_hw_write((reg), (uint32_t)(value))
#define HW_LCD_BASE_GET()     host_hw_lcd_base()
#define HW_LCD_BASE_SET(p)    host_hw_set_lcd_base((uintptr_t)(p))
#define HW_LCD_CURRENT_GET()  host_hw_lcd_current()
#define HW_DRAIN_WRITE_BUFFER()       ((void)0)
#define HW_CLEAN_DCACHE_LINE(addr)    ((void)(addr))
#define HW_FLUSH_DCACHE_LINE(addr)    ((void)(addr))
#define HW_INVALIDATE_ICACHE_LINE(addr) ((void)(addr))
#define HW_INVALIDATE_ICACHE()        ((void)0)

#else

#define HW_READ(reg)          (*(volatile uint32_t *)(reg))
#define HW_WRITE(reg, value)  (*(volatile uint32_t *)(reg) = (uint32_t)(value))
#define HW_LCD_BASE_GET()     ((uintptr_t)HW_READ(HW_LCD_BASE))
#define HW_LCD_BASE_SET(p)    HW_WRITE(HW_LCD_BASE, (uintptr_t)(p))
#define HW_LCD_CURRENT_GET()  ((uintptr_t)HW_READ(HW_LCD_CURRENT))
#define HW_DRAIN_WRITE_BUFFER() \
  __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0) : "memory")
#define HW_CLEAN_DCACHE_LINE(addr) \
  __asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" : : "r" (addr))
#define HW_FLUSH_DCACHE_LINE(addr) \
  __asm__ volatile ("mcr p15, 0, %0, c7, c14, 1" : : "r" (addr))
#define HW_INVALIDATE_ICACHE_LINE(addr) \
  __asm__ volatile ("mcr p15, 0, %0, c7, c5, 1" : : "r" (addr))
#define HW_INVALIDATE_ICACHE() \
  __asm__ volatile ("mcr p15, 0, %0, c7, c5, 0" : : "r" (0))

#endif

#endif
