Highest-impact ideas (video-only)
1. Replace full clean_dcache() with a range D-clean of the displayed framebuffer
You already have warm_cache_op_range(WOP_D_CLEAN, addr, size) in nspire.c, which cleans 32-byte lines in a range.

After the upscale, the minimum that must be coherent for the LCD is the physical buffer that will be shown—the 320×240 RGB565 image you just wrote (i.e. the buffer that upscale_* wrote to, which becomes the new display buffer after the pointer swap). Cleaning ~320×240×2 ≈ 150 KiB is orders of magnitude less work than cleaning all dirty cache lines on the core.

Caveat: Confirm on hardware that no other path relies on a full cache clean at this point (unlikely for pure display, but worth a regression pass). Also only D-clean is needed for “CPU wrote, device reads”; you do not need a full clean_dcache drain for that.

This is usually the single biggest win if clean_dcache() is hot.

2. Merge “repack + upscale” into one pass
Today you touch pixels twice in software: repack into nspire_upscale_src, then upscale reads that and writes nspire_screen. A single loop (or one assembly routine) that:

reads one row (or tile) from gba_screen_pixels in libretro layout,
converts to Nspire RGB565 as needed,
runs the same math as upscale_aspect_* (or dispatches to fast/raw),
cuts memory traffic and cache footprint roughly in half for that stage. This is more work to implement but is the classic “big” optimization.

3. Avoid the mix path when you care about speed
repack_gba_mix_to_upscale_src does extra loads/stores and blending per pixel. Keeping nspire_frame_mix_choice off (and/or not exposing a “pretty but slow” mode as default) is a free win when FPS matters.

4. Tune the scaler choice
You already branch fullscreen → upscale_aspect_fast, scaled_raw → upscale_aspect_raw, else upscale_aspect. The interpolating upscale_aspect path (notaz’s 3:2 work in upscale_aspect.S) is the heavy one; fast/raw are the right targets for performance. Making sure defaults and UI steer users to fast/unblended modes is mostly policy, not new code.

5. Micro-optimize the repack inner loop (if it stays separate)
The inner loop is scalar C. On ARM926 there is no NEON. You can still:

process 2 pixels at a time with careful masks/ORs,
or move the swizzle + row pack into ASM with fewer loads/stores and better pipelining,
or align hot buffers to cache line boundaries to reduce line ping-pong with the scaler.
Smaller than (1) or (2), but still useful if profiling points at repack.

5b. Mosaic / modulo on hot scanlines (addressed in `video.cc`)
Per-pixel `i % mosh` compiled to `__aeabi_uidivmod` on soft-integer ARM926. Replaced with a phase counter in mosaic paths (`mosaic_take_sample` / `mosaic_advance`) so inner loops stay add/cmp/br only. Remaining `uidiv` in `video.s` is mostly setup (e.g. `vcount % mosv` once per affine sprite), not per pixel.

6. Vblank wait
update_at_vblank() spins until the LCD flag is set. That’s latency, not really “CPU work,” but it can interact with pacing. Changing it is risky for tearing; treat as last after real wins from (1)–(2).

What I would do first
Profile (even roughly): time repack, upscale, and clean_dcache separately.
Replace clean_dcache() with warm_cache_op_range(WOP_D_CLEAN, …) covering exactly the buffer the LCD will read after the swap (and verify on device).
If still bound by CPU, fuse repack + upscale (or ASM repack).

---

Ideas from Gemini discussion (PPU / software renderer), with ARM926EJ-S notes

Context: typical ARM926EJ-S has 16 KiB instruction + 16 KiB data L1, no NEON, no hardware FPU; often no L2 or a small external one. The hot path is memory traffic + instruction footprint competing with data (VRAM, tile maps, scanline buffers). The items above (1)–(6) target the **Nspire host** path (LCD, repack, upscale); the ideas below target the **emulated GBA PPU** (`video.cc` / core). They interact with cache: small tables that stay resident beat large LUTs that thrash L1.

### Ideas to avoid or treat as negative

| Idea | Summary | ARM926 / gpsp effectiveness |
|------|---------|-------------------------------|
| Per-scanline checksum on the composed 32-bit line | Hash the line; if unchanged skip blend | **Bad.** You still read every word of the line (bandwidth + D-cache pollution). On a miss-heavy core the read pass often costs as much as cheap 5:5:5 blend math. Busy games always dirty → you pay check + blend. Collision risk if the hash is too weak. |
| Large alpha blend LUT (e.g. 512×512 × 16-bit ≈ 512 KiB) | Two-index lookup for every A+B alpha pixel | **Avoid on ARM926.** Table does not fit L1; random lookups evict code + scanline working set. Per-pixel latency can exceed a few shifts/adds in registers. |
| 64 KiB “555 → 565” colour LUT used inside inner pixel loops | One load per output pixel | **Risky.** 64 KiB ≫ 16 KiB D-cache; inner loop becomes miss-bound. Prefer **barrel-shifter pack** (ORR with shifted fields), which this CPU does cheaply. |
| NEON / wide SIMD tile decode | Parallel 4bpp→16bpp | **N/A** on ARM926EJ-S (no NEON). Any portable “SIMD” note from Gemini does not apply here unless you retarget a different core. |

### Write-time dirty tracking and skipping work

| Idea | Summary | ARM926 / gpsp effectiveness |
|------|---------|-------------------------------|
| VRAM / palette / OAM dirty regions | On `write_vram` / palette / OAM handlers, mark blocks (e.g. 64 B tile or 1 KiB) or global flags | **Variable.** GBA puts **tile data** and **screen blocks** (tile IDs) in VRAM; scroll in I/O. Correctness requires flagging map writes, char writes, scroll, window, and layer enable changes. This fork does **not** appear to use a tile dirty bitmap in the core (unlike some forks). **Gain:** best on static HUD / menus (skip whole layer loops). **Cost:** flag checks every line + bookkeeping; **0% or small loss** on full-screen motion (F-Zero–style). Flag maintenance must stay tight (stores on **game** writes only; rare vs PPU reads). |
| Per-layer or “clean layer” skip with top/sub compositing | Skip decoding a BG if dirty false; reuse prior line buffer | **Moderate complexity.** gpSP-style renderers keep **top + sub** pixels for blending; you cannot skip a layer blindly if it participates in alpha without either cached intermediates or correct dependency order. Opaque-top early-out per **x** is still valid where hardware allows “done after two layers.” |
| Tile cache + `tile_cache_dirty[]` | Decode 8×8 once; invalidate on VRAM touch | **High potential** for repeated tiles, **but** extra RAM + cache footprint for the cache. Keep the **working tile cache** small and hot; avoid huge structures that push **video.cc** / scanline code out of I-cache. |

### Lookup tables and precompute (sizes from Gemini, sanity-checked)

| Idea | Typical size | ARM926 / gpsp effectiveness |
|------|--------------|------------------------------|
| **Fade / brightness “single target” palette** | ~512 × 16-bit ≈ 1 KiB when only global BLDY/brightness-style effect applies | **Good fit for L1.** Rebuild when `BLDCNT` / `BLDY` / relevant targets change—not per pixel. Does **not** replace full **A vs B alpha** (that needs two pixel indices; full matrix is huge—see avoid list). Stock libretro-style paths still do per-pixel blend math in many cases; worth profiling whether **palette_ram_converted** / effects dominate. |
| **Affine step table per scanline** | ~240 × 4 B ≈ 960 B per layer for fixed stepping along a line | **Good.** Replaces repeated mul/add in the inner loop with sequential loads (predictable, small footprint). Multiple layers still ≪ 16 KiB if kept compact. Aligns with “ARM926 slow at multiply” guidance. |
| **Mosaic x→source_x table** | ~240 × 2 B ≈ 480 B | **Low priority here:** §5b already replaced per-pixel `%` with a **phase counter** in `video.cc` mosaic paths; a LUT is an alternative, not clearly better once division is gone. |
| **Per-scanline sprite attribute / priority buffer** | ~240 B | **Small win possible:** one byte per column for priority / semi-transparent / window flags, filled in an OAM prescan. Reduces bit tests in the hottest compositor path; cost is an extra pass + D-cache line for the buffer (usually acceptable). |

### Architectural “big wins” (visibility vs cost on Nspire class hardware)

| Idea | Notes |
|------|--------|
| GPU-accelerated layer compositing (GLES/Vulkan) | Large visible win on SoCs with a usable 2D/3D pipeline; **not** a given on calculator-class targets where you are already **CPU + framebuffer**. |
| Async PPU on second core | Hides PPU time if the OS exposes a stable second thread and you can serialize safely; **synchronization and determinism** cost is non-trivial. |
| Block linking in the JIT | **Already in gpSP**; not a new video lever. |
| HLE BIOS (div, sqrt, etc.) | **Not video**, but can be a **visible** FPS change in math-heavy titles; orthogonal to PPU cache. |

### Front-to-back vs gpSP’s two-pixel model

Pure front-to-back Z-style compositing does not map 1:1 to GBA priorities + **A/B blending**. What remains useful is **early-out** when the **top** pixel is opaque and no further layer can show through for that **x**, and **layer skipping** driven by dirty/enable state. Expect **incremental** gains, not a full “skip all back layers” win unless effects and windows are off.

### Cache interaction with what you already do

Items (1)–(2) in the first section **reduce host** memory traffic for LCD upload. PPU ideas add **their own** reads/writes (dirty arrays, tile cache, step tables). On ARM926 the rule of thumb: **keep hot structures + inner loop under ~16 KiB combined where possible**; prefer **register / shift math** over **large random LUTs**; prefer **write-time flags** over **read-time checksums** on scanlines.
