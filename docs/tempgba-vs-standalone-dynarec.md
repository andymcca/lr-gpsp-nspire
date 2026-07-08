# TempGBA-Master vs nspire-libretro-standalone: partial flush and block reuse

This note compares **TempGBA-Master** (`source/cpu.c`, metadata-based partial invalidation and checksum-based writable block reuse) with **nspire-libretro-standalone** (gpsp-partial-flush style SMC mirror + optional classic full flush). It also sketches whether porting TempGBA’s approach is worth the cost on a **TI-Nspire–class** host (low-MHz ARM9, small RAM, small translation cache).

---

## 1. Partial flush: two designs

### TempGBA: parallel **metadata** arrays

Writable regions use **sidecar metadata** aligned with GBA addressable words: `iwram_metadata`, `ewram_metadata`, `vram_metadata`. During translation, macros such as `smc_write_*_yes()` set bits on the **fourth u16** of each 4-byte slot (`… | 3`) to record:

- Thumb vs ARM code at that GBA word
- Whether an **unconditional branch** ends a run (so invalidation can stop the scan correctly)

**Partial invalidation** (`partial_clear_metadata` → `partial_clear_metadata_thumb` / `partial_clear_metadata_arm`):

- Resolves `metadata` from region + offset.
- If no “code” bits in `metadata[3]`, returns immediately.
- Otherwise walks **left** and **right** in the metadata ring (with **wrap** at region boundaries), clearing tag words according to ARM vs Thumb rules until stop conditions (including “unconditional branch in Thumb/ARM”).

**Regions:** IWRAM (`0x03`), EWRAM (`0x02`), and **VRAM (`0x06`)** are all handled in the same framework.

**Source:** `TempGBA-master/source/cpu.c` — `smc_write_*`, `partial_clear_metadata`, `partial_clear_metadata_thumb`, `partial_clear_metadata_arm`.

### nspire-libretro-standalone: **SMC mirror** + halfword chain

Following **gpsp-partial-flush** / lr-gpsp-amcc ideas:

- A **u16 mirror** beside real IW/EW (`iwram` low mirror; `ewram + 0x40000`) stores **SMC tags** linked to RAM JIT allocation (`ram_block_tag`, `get_ram_tag`, etc.).
- **`partial_flush_ram_inner`** clears the written cell, optionally registers a **translation gate** PC, then walks **left** until `0` or sentinel `SMC_TAG_UB16` (`0xFFFF`), and **right** until `0`.
- **DMA** path uses aligned `u16` checks and `partial_flush_ram_full_dma`; CPU stub path uses the same invalidation entry points (plus optional **classic** `flush_dynarec_caches` policy).

**Regions in the inner walker:** effectively **IWRAM and EWRAM** only in `partial_flush_ram_inner` (no VRAM in that routine).

**Source:** `nspire-libretro-standalone/libretro-gpsp/cpu_threaded.c` — `partial_flush_ram_inner`, `partial_flush_ram_full`, `partial_flush_ram_full_dma`; stub `libretro-gpsp/arm/arm_stub.S`; doc `docs/partial-flush-smc-fix.md`.

### Comparison table

| Topic | TempGBA metadata | Nspire standalone (mirror + partial) |
|--------|------------------|----------------------------------------|
| **Extra RAM** | Large linear metadata per region (IW/EW/VRAM) | Mirror u16s + tag table at end of RAM JIT cache |
| **VRAM SMC partial** | Yes, unified with IW/EW | Not in `partial_flush_ram_inner`; VRAM uses different store path |
| **ARM vs Thumb** | Explicit bits in metadata | Mirror tags + translator state; UB sentinel for left scan |
| **Walk / wrap** | Metadata index wrap per region | Mirror buffer wrap for halfword chain |
| **Complexity** | More state in `scan_block` / smc macros | Fewer concepts; closer to upstream libretro + partial-flush fork |
| **Coupling** | Metadata must stay consistent with every emit path | Mirror updated on translate; stub/DMA must agree |

---

## 2. Is it worth **switching** to TempGBA’s metadata approach?

### Benefits you would get

1. **VRAM self-modifying code** could use the **same** partial-invalidation machinery as IW/EW, if you still care about rare SMC-in-VRAM cases (many games do not execute code from VRAM).
2. **Richer invariants** in metadata (Thumb vs ARM, unconditional branch) can make the **invalidation walk** easier to reason about than a single halfword tag chain — *if* you fully ported all writers (`smc_write_*`, scan_block, flush paths) and kept them bug-for-bug free.

### Costs and risks

1. **RAM footprint:** TempGBA-style metadata is **proportional to addressable IW/EW/VRAM code windows** (multiple u16 per GBA word). On Ndless builds with **`SMALL_TRANSLATION_CACHE`** and fixed RAM budgets, this can be **harder to justify** than the compact mirror + `ramtag_type` scheme already wired to libretro’s RAM translation layout.
2. **Porting effort:** You would not drop in `partial_clear_metadata` alone. You must port **all** metadata updates (`smc_write_*`, unconditional-branch marks, flush/clear_metadata_area, savestate, etc.) and keep **arm_emit / cpu_threaded / stub / DMA** aligned. That is a **large** divergence from [libretro/gpsp](https://github.com/libretro/gpsp) and the current partial-flush fork merge strategy.
3. **Correctness churn:** The standalone port already invested in **partial-flush edge cases** (Mario Golf–class bugs, DMA `u16` gate, classic vs partial policies). Re-deriving equivalence on metadata is **high regression risk** for modest gain unless VRAM SMC or a specific title is the target.

### Recommendation

- **Default: no** — for **nspire-libretro-standalone**, staying on the **mirror + partial_flush_ram_inner** model is usually the better tradeoff: **less RAM**, **closer to the libretro partial-flush lineage** you already maintain, and **good enough** for the common case (IW/EW SMC).
- **Consider TempGBA-style metadata** only if you have a **concrete, reproducible** failure or profile win: e.g. a commercial title that **modifies and executes** code from **VRAM** and is broken or too slow with full flush / without VRAM partial. Even then, a **smaller scoped** fix (e.g. VRAM-only side metadata) might beat a full TempGBA port.

**Bottom line:** TempGBA’s metadata approach is **more expressive** and **unifies VRAM** with IW/EW for partial flush; the benefit is **real but niche**. For most GBA titles and for Nspire **memory and maintenance** constraints, the **marginal benefit rarely pays** the full switch cost.

---

## 3. TempGBA **checksum / block reuse** (writable RAM)

### Mechanism (summary)

After `scan_block` for **writable** translation, TempGBA:

1. Computes **`block_checksum_arm` / `block_checksum_thumb`** — a small 16-bit fingerprint over the block’s opcode array (not sufficient alone for identity).
2. Indexes **`writable_checksum_hash[checksum & (WRITABLE_HASH_SIZE - 1)]`** with **`WRITABLE_HASH_SIZE = 65536`** (`cpu.h`).
3. Walks a linked list of **`ReuseHeader`** (`PC`, `GBACodeSize`, `Next`); on candidate match, verifies **`memcmp(opcodes, Header+1, GBACodeSize) == 0`** before reusing native code.
4. On reuse, updates metadata as if the block were freshly compiled (`smc_write_*_yes`, `unconditional_branch_write_*_yes`, etc.) and returns the existing native entry point.

**Source:** `TempGBA-master/source/cpu.c` — `translate_block_builder`, `block_checksum_arm` / `block_checksum_thumb`; `TempGBA-master/source/cpu.h` — `WRITABLE_HASH_SIZE`.

### Nspire / ARM926 angle

- **Reuse hit:** Skips most of **emit + backpatch** for that basic block — often a **large** win on a **slow** CPU.
- **Reuse miss:** Pays checksum + hash bucket walk + **`memcmp`** up to block size + allocation of **`ReuseHeader` + opcode copy** on first compile. Still usually cheaper than full JIT **if** hits are frequent (same RAM code re-bound after partial flush, repeated templates).
- **RAM:** A **64K-entry** pointer table is **~256 KiB** before lists and opcode copies — likely **too heavy** for default Ndless builds unless you **shrink** the hash and/or gate the feature.

**Exploration path for standalone:** prototype behind a flag with **small** `WRITABLE_HASH_SIZE` (e.g. 4096), measure **RAM + fps + translate time** on a few ROMs; only then consider opcode storage vs “checksum + full compare only on PC collision” variants.

---

## 4. References (paths in this workspace)

| Piece | Location |
|--------|----------|
| TempGBA metadata + partial clear + reuse | `TempGBA-master/source/cpu.c`, `TempGBA-master/source/cpu.h` |
| Standalone partial flush | `nspire-libretro-standalone/libretro-gpsp/cpu_threaded.c` |
| SMC / partial flush fix notes | `nspire-libretro-standalone/docs/partial-flush-smc-fix.md` |
| Upstream libretro (full flush, no partial) | [libretro/gpsp](https://github.com/libretro/gpsp) |

---

*Document added for planning; behavior of the emulator is defined by the C/assembly sources, not this file.*
