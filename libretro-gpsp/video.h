/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef VIDEO_H
#define VIDEO_H

void update_scanline(void);
void video_reload_counters(void);

extern s32 affine_reference_x[2];
extern s32 affine_reference_y[2];

extern u16* gba_screen_pixels;

#ifdef NSPIRE_BUILD
/* Lines drawn in batches (nspire_lines.c): a line is
 * drawn when the game next changes what the picture is made of (display
 * registers, palette, VRAM, OAM, a DMA into them) or at VBlank, not at its
 * own HBlank, so the renderer and the game's code don't push each other out
 * of the instruction cache every line. Writers call nspire_video_write()
 * first: the waiting lines still see the old data. */
extern u32 nspire_lines_waiting;
extern u32 nspire_batch_lines;
void nspire_video_line_ready(u32 vcount);
void nspire_flush_lines(void);
void nspire_video_lines_reset(void);
#define nspire_video_write()                                                  \
  do { if (nspire_lines_waiting) nspire_flush_lines(); } while (0)
#endif

#endif
