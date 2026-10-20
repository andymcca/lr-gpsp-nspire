/* Whether 0x900B0000 is the CX clock. On a CX II that address is the ADC.
 * The fields are the ones nOver reads. With the 6 MHz PLL (bit 8 clear):
 * base = 6 * bits 15-20, the CPU divider is bits 1-7 times 2, and bits 21-22
 * equal to 1 halve that divider. A stock CX is 132 MHz (base 264, divider 2). */

#include "common.h"
#include "nspire_clock.h"

#define CX_R1_SHIFT 1
#define CX_R1_BITS  0x7Fu
#define CX_B1_SHIFT 8
#define CX_B2_SHIFT 15
#define CX_B2_BITS  0x3Fu
#define CX_B3_SHIFT 21
#define CX_B3_BITS  3u

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

/* A CX II on USB runs at 288 MHz and otherwise at 396. A CX stays under that. */
#define CX2_MIN_MHZ 260u

int nspire_clock_kind_cx2(unsigned load, unsigned current, unsigned measured_mhz)
{
  unsigned decoded = nspire_cx_cpu_mhz(load);
  int regs = load == current && decoded >= 96u && decoded <= 246u;
  int near = measured_mhz && decoded &&
             measured_mhz * 50u >= decoded * 49u &&
             measured_mhz * 50u <= decoded * 51u;

  /* Matching load and latched words are the CX power block. A fast
   * measurement wins when they disagree, which an ADC read can. */
  if (regs && (!measured_mhz || near))
    return 0;
  if (measured_mhz >= CX2_MIN_MHZ)
    return 1;
  if (measured_mhz)
    return 0;
  return -1;
}
