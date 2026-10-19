/* The CX (not CX II) clock word at 0x900B0000.
 *
 * The fields are the ones nOver programs. With the 6 MHz PLL (bit 8 clear):
 * base = 6 * bits 15-20, the CPU divider is bits 1-7 times 2, and bits 21-22
 * equal to 1 halve that divider. The bus divider is bits 12-14 plus 1.
 * A stock CX is 132 MHz (base 264, divider 2). Divider 1 makes the CPU equal
 * to the base, up to 378 MHz, which is not stable; this only builds 180 to
 * 234 MHz and keeps the bus at or under 66 MHz. */

#include "common.h"
#include "nspire_clock.h"

#define CX_R1_SHIFT 1
#define CX_R1_BITS  0x7Fu
#define CX_B1_SHIFT 8
#define CX_R2_SHIFT 12
#define CX_R2_BITS  7u
#define CX_B2_SHIFT 15
#define CX_B2_BITS  0x3Fu
#define CX_B3_SHIFT 21
#define CX_B3_BITS  3u

#define CX_OWNED_BITS (((CX_R1_BITS) << CX_R1_SHIFT) | (1u << CX_B1_SHIFT) | \
  ((CX_R2_BITS) << CX_R2_SHIFT) | ((CX_B2_BITS) << CX_B2_SHIFT) | \
  ((CX_B3_BITS) << CX_B3_SHIFT))

/* nOver's warning line for the CX bus. The menu speeds stay under it. */
#define CX_AHB_LIMIT 66u

unsigned nspire_cx_cpu_mhz(unsigned config)
{
  unsigned b1 = (config >> CX_B1_SHIFT) & 1u;
  unsigned b2 = (config >> CX_B2_SHIFT) & CX_B2_BITS;
  unsigned b3 = (config >> CX_B3_SHIFT) & CX_B3_BITS;
  unsigned r1 = (config >> CX_R1_SHIFT) & CX_R1_BITS;
  unsigned base = b1 ? 48u : 6u * b2;
  unsigned div = r1 * 2u;

  if (b3 == 1u)
    div /= 2u;
  if (!base || !div)
    return 0;
  return base / div;
}

unsigned nspire_cx_ahb_mhz(unsigned config)
{
  unsigned cpu = nspire_cx_cpu_mhz(config);
  unsigned div = ((config >> CX_R2_SHIFT) & CX_R2_BITS) + 1u;

  if (!cpu)
    return 0;
  return cpu / div;
}

unsigned nspire_cx_clock_word(unsigned original, unsigned target_mhz)
{
  unsigned b2, div, r2, word;

  if ((original >> CX_B1_SHIFT) & 1u)
    return 0;
  if (!nspire_cx_cpu_mhz(original))
    return 0;
  if (target_mhz < 180u || target_mhz > 234u || target_mhz % 6u)
    return 0;

  b2 = target_mhz / 6u;
  div = (target_mhz + CX_AHB_LIMIT - 1u) / CX_AHB_LIMIT;
  if (div < 2u)
    div = 2u;
  if (div > 8u)
    return 0;
  r2 = div - 1u;

  /* Divider 1 (bits 21-22 = 1, CPU field 1): the CPU runs at the base. */
  word = (original & ~CX_OWNED_BITS) | (1u << CX_R1_SHIFT) | (r2 << CX_R2_SHIFT) |
         (b2 << CX_B2_SHIFT) | (1u << CX_B3_SHIFT);
  if (nspire_cx_cpu_mhz(word) != target_mhz || nspire_cx_ahb_mhz(word) > CX_AHB_LIMIT)
    return 0;
  return word;
}
