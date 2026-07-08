# Partial dynarec flush (SMC): Mario Golf graphics bug and fixes

This document explains a correctness bug seen when **partial RAM translation flush** is enabled on the Ndless gpSP port, how it was diagnosed, and the code changes that fix it.

## Background: what “partial flushing” is

gpSP uses a **dynamic recompiler (dynarec)** that translates Game Boy Advance ARM/Thumb code into host code. When the game **writes to IWRAM or EWRAM** that may contain **translated code** (self-modifying code, or simply loading a new routine over an old one), the emulator must **invalidate** the affected translated blocks so the CPU does not execute **stale host code** against **new RAM contents**.

Two strategies exist:

1. **Full flush** — invalidate all RAM translations (`flush_translation_cache_ram()`). Safe but expensive.
2. **Partial flush** — walk a **mirror “SMC tag” region** next to work RAM, clear only the linked halfword chain around the written address, and resume execution after re-looking up the PC (`partial_flush_ram_full` / `partial_flush_ram_full_dma` in `cpu_threaded.c`). Based on ideas from [gpsp-partial-flush](https://github.com/andymcca/gpsp-partial-flush/) and related work.

Partial flush is triggered from:

- **ARM store stubs** in `temp/arm/arm_stub.S` when a store completes to IW/EW RAM and the SMC sentinel at the mirrored offset is non-zero.
- **DMA** writes to IW/EW RAM in `gba_memory.c` when the mirrored SMC cell indicates code may live there.

This is **not** related to PPU “dirty regions”, VRAM partial updates, or display flushing. It is purely **CPU translation cache** invalidation.

## Symptom: Mario Golf (and similar “wrong art” bugs)

With partial flushing enabled:

- During **certain scenes** (e.g. background when **taking a shot**), graphics looked **wrong** — wrong tiles, wrong “textures”, or a corrupted-looking backdrop.
- **Moving the character** (left/right) often caused a **partial re-render** that looked **more** wrong until further movement or time passed.

With partial flushing **disabled** (or falling back to a **full** RAM flush), the problem **went away**. That pinned the bug to **partial SMC invalidation**, not the video path or ROM.

### Why it looked like “wrong textures”

The PPU was fine; **CPU state** was wrong. Stale JIT still ran after RAM had changed, so the game computed **bad addresses** or **bad indices** for tilemaps, scroll, or DMA — which surfaces as **wrong background art**, not a single bad texel in isolation.

---

## Bug 1: `0xFFFF` used for two different meanings (primary Mario Golf fix)

### How RAM tags work (short)

For EWRAM/IWRAM, the recompiler maintains **16-bit tags** in a **mirror region**:

- EWRAM tags live in the **second** 256 KiB of the host `ewram[]` buffer (`ewram + 0x40000`), parallel to the visible first 256 KiB.
- IWRAM tags live in the **first** 32 KiB of `iwram[]`; game-visible IWRAM is mapped to the **second** 32 KiB (`iwram + 0x8000`).

Tag values include:

- `0x0000` — no / cleared translation metadata at that halfword.
- `0x0101` (`CODE_TAG_BLOCK16`) — interior “this is inside a translated block”.
- Values **`> 0x0101`** — **block headers**; each maps through `get_ram_tag()` to a `ramtag_type` with `offset_arm` / `offset_thumb` into `ram_translation_cache`.

New block headers are allocated from a descending counter **`ram_block_tag`**, stepping by 2 so tags stay **odd** (comment in code: both bytes non-zero for Thumb-friendly patterns).

### The collision

`partial_flush_ram_inner()` scans **left** from the written SMC cell and clears halfwords until it reads **`0` or `0xFFFF`**:

```3542:3551:c:\Users\Owner\Downloads\nspire\gpsp-master\nspire-libretro-standalone\libretro-gpsp\cpu_threaded.c
  while (1)
  {
    smc_data -= 2;
    if (smc_data < smc_data_area)
      smc_data = smc_data_area_end - 2;
    v = *(u16 *)smc_data;
    if (v == 0 || v == (u16)SMC_TAG_UB16)
      break;
    *(u16 *)smc_data = 0;
  }
```

`SMC_TAG_UB16` is defined next to the partial-flush comment block:

```3465:3465:c:\Users\Owner\Downloads\nspire\gpsp-master\nspire-libretro-standalone\libretro-gpsp\cpu_threaded.c
#define SMC_TAG_UB16 0xFFFFu
```

`0xFFFF` is documented in this tree as a **left-chain terminator** (“UB sentinel”): **do not clear past this halfword**.

Separately, **`INITIAL_TOP_TAG` was `0xFFFF`**, so the **very first** RAM block translated in a session received a **real block header** of **`0xFFFF`** — the **same bit pattern** as the terminator.

So when a flush started **inside** that first block (after DMA or stores overwrote interior instructions):

1. The algorithm cleared interior links (`0x0101`, etc.) moving **left** toward the real **header**.
2. It read **`0xFFFF`** at the **actual block start** and interpreted it as **“stop; do not clear”** — matching the sentinel rule.
3. The **header was never cleared**, and the associated **`ramtag_type` offsets were never invalidated**.
4. The next `block_lookup_translate_*` still saw a **valid** tag and returned **old host code** for RAM that had **already changed** → subtle corruption until other paths retranslated.

That is fully consistent with “**partial flush only**” and intermittent recovery when enough **new** code paths were compiled.

### Fix: reserve `0xFFFF` for the terminator only

**Change:** set `INITIAL_TOP_TAG` to **`0xFFFD`** instead of **`0xFFFF`**.

- First allocated header is now **`0xFFFD`**, then `0xFFFB`, … — still odd, still `> LAST_TAG_NUM` (`0x0101`).
- **`0xFFFF`** should now only appear where the scan intentionally uses **`SMC_TAG_UB16`** as a **terminator**, not as a live block header.

**File:** `libretro-gpsp/cpu_threaded.c`  
**Symbols:** `INITIAL_TOP_TAG`, comments above `#define LAST_TAG_NUM` / `INITIAL_TOP_TAG`.

```2506:2537:c:\Users\Owner\Downloads\nspire\gpsp-master\nspire-libretro-standalone\libretro-gpsp\cpu_threaded.c
// INITIAL_TOP_TAG must not be 0xFFFF: partial_flush_ram_inner's left scan treats
// halfword 0xFFFF (SMC_TAG_UB16) as "stop without clearing". The first allocated
// block used to get 0xFFFF, so an interior SMC flush left that header + stale
// ramtag offsets (wrong tiles until something forced retranslate). Start at 0xFFFD.

#define LAST_TAG_NUM       0x0101
#define INITIAL_TOP_TAG    0xFFFD
#define CODE_TAG_BLOCK16   0x0101
#define CODE_TAG_BLOCK32   0x01010101

#define VALID_TAG(tagn) (tagn > LAST_TAG_NUM)

#define allocate_tag_arm(location) {   \
  location[0] = ram_block_tag;         \
  /* Could be another thumb inst */    \
  if (!location[1])                    \
    location[1] = CODE_TAG_BLOCK16;    \
  ram_block_tag -= 2;                  \
}

#define allocate_tag_thumb(location) { \
  location[0] = ram_block_tag;         \
  ram_block_tag -= 2;                  \
}

typedef struct
{
  u32 offset_arm;     // Cache offset to the ARM-mode compiled block
  u32 offset_thumb;   // Cache offset to the Thumb-mode compiled block
} ramtag_type;

static u32 ram_block_tag = INITIAL_TOP_TAG;
```

---

## Bug 2: DMA flush gate and 8-bit transfers (secondary hardening)

### How DMA triggers partial flush

DMA into IW/EW RAM only calls `partial_flush_ram_full_dma(dest)` when the **mirrored SMC** cell is **non-zero**, meaning “there may be translated code at this parallel offset.”

Originally the condition used the same **`address8` / `address16` / `address32`** width as the DMA transfer:

```c
// Conceptual old behaviour
if (address8(ewram, (dest & 0x3FFFF) + 0x40000))  // for 8-bit DMA
    partial_flush_ram_full_dma(dest);
```

SMC tags are **`u16`** values. For **8-bit DMA**, probing **one byte** of that `u16` can be **zero** even when the tag is non-zero (example: tag `0x4000` → low byte `0x00`). The code then **skipped** `partial_flush_ram_full_dma`, leaving **stale JIT** after a DMA overlay.

### Fix: always probe an aligned 16-bit halfword

For both IW and EW DMA writes, the gate now uses:

- **EWRAM:** `*(u16 *)(ewram + (((dest & 0x3FFFF) + 0x40000) & ~1u))`
- **IWRAM:** `*(u16 *)(iwram + ((dest & 0x7FFF) & ~1u))`

So the decision matches **tag granularity** and odd destinations still observe the correct halfword.

**File:** `libretro-gpsp/gba_memory.c`  
**Macros:** `dma_write_ewram`, `dma_write_iwram` (inside the `dma_tf_loop` builder).

```1782:1823:c:\Users\Owner\Downloads\nspire\gpsp-master\nspire-libretro-standalone\libretro-gpsp\gba_memory.c
/* SMC tags are u16; 8-bit DMA used to gate on address8() and could miss a tag
 * with a zero low or high byte, skipping partial_flush and leaving stale JIT. */
#define dma_write_iwram(type, tfsize)                                         \
  if (address##tfsize(iwram + 0x8000, type##_ptr & 0x7FFF) !=                  \
      eswap##tfsize(read_value))                                              \
  {                                                                           \
    address##tfsize(iwram + 0x8000, type##_ptr & 0x7FFF) =                    \
      eswap##tfsize(read_value);                                              \
    if (*(u16 *)(iwram + ((type##_ptr & 0x7FFF) & ~1u)))                      \
    {                                                                         \
      partial_flush_ram_full_dma(type##_ptr);                                 \
    }                                                                         \
  }                                                                           \

#define dma_write_ewram(type, tfsize)                                         \
  if (address##tfsize(ewram, type##_ptr & 0x3FFFF) !=                        \
      eswap##tfsize(read_value))                                              \
  {                                                                           \
    address##tfsize(ewram, type##_ptr & 0x3FFFF) = eswap##tfsize(read_value);  \
    if (*(u16 *)(ewram + (((type##_ptr & 0x3FFFF) + 0x40000) & ~1u)))          \
    {                                                                         \
      partial_flush_ram_full_dma(type##_ptr);                                 \
    }                                                                         \
  }                                                                           \
```

---

## Files touched (summary)

| File | Change |
|------|--------|
| `libretro-gpsp/cpu_threaded.c` | `INITIAL_TOP_TAG` `0xFFFF` → `0xFFFD`; comments explaining collision with `SMC_TAG_UB16` left-scan. |
| `libretro-gpsp/gba_memory.c` | SMC non-zero check for DMA to IW/EW RAM uses aligned `u16` read; short comment why. |

---

## Emulator option (Ndless UI): partial vs classic SMC

The standalone menu (**Emulator** submenu) includes **Dynarec RAM / SMC**:

- **Partial SMC (default)** — `nspire_dynarec_ram_policy == 0`: uses `partial_flush_ram_*` and **conservative** RAM branch emission: `gba_branch_emit_pc_relative_link` is true only for in-block targets, BIOS, or cart ROM; branches to other RAM (or elsewhere) use **indirect** dispatch so a partial mirror clear cannot leave a stale PC-relative patch.

- **Classic (full flush on SMC)** — `nspire_dynarec_ram_policy == 1`: SMC entry points call **`flush_dynarec_caches()`** (ROM + RAM translation caches and full SMC mirror wipe), not `flush_translation_cache_ram()` alone. The latter can skip clearing the SMC mirror when `iwram_code_max` / `ewram_code_max` are already zero after a previous flush, leaving **stale tags** while `ram_translation_ptr` resets — that produced a **white screen** if used standalone from the SMC path.

  With classic policy, **`gba_branch_emit_pc_relative_link` is always true** for RAM blocks, so emitted branches match **ROM** behaviour: **direct** PC-relative links for ROM↔ROM, ROM↔RAM, RAM↔ROM, and RAM↔RAM, relying on the full flush to invalidate stale code before any new patch is observed.

The choice is stored in `gpsp.cfg` (36 words on Ndless). Changing it flushes all dynarec caches so existing translations match the new policy.

Implementation summary:

- `nspire_dynarec_ram_policy` in `nspire_stubs.c` / `main.h`.
- `partial_flush_ram_full` / `partial_flush_ram_full_dma` in `cpu_threaded.c` call `flush_dynarec_caches()` when policy is non-zero.
- `gba_branch_emit_pc_relative_link` in `libretro-gpsp/arm/arm_emit.h` (Ndless build; mirrored under `temp/arm/`) expands to `1` when policy is non-zero.
- `nspire_dynarec_ram_policy_menu_hook` + `nspire_emulator_options_apply` handle cache flush when the user toggles the option.

## Verification

- **Mario Golf:** background / shot UI wrong with partial flush before fixes; **correct** after `INITIAL_TOP_TAG` fix (and DMA gate remains safe hardening).
- **Build:** `docker compose run --rm ndless-dev make` from `nspire-libretro-standalone` (per `README.txt`).

---

## Related code references (for navigation)

- Partial flush implementation: `partial_flush_ram_inner`, `partial_flush_ram_full`, `partial_flush_ram_full_dma` — `libretro-gpsp/cpu_threaded.c`.
- Declarations / no-op when no dynarec: `libretro-gpsp/cpu.h` (`HAVE_DYNAREC`).
- CPU store path SMC branch: `temp/arm/arm_stub.S` (`smc_partial_*`, `partial_flush_ram_full`).
- DMA IW/EW writes: `libretro-gpsp/gba_memory.c` (`dma_write_iwram`, `dma_write_ewram`).

---

## Optional follow-ups (not required for this bug)

- **Interpreter / `write_memory*`** paths for regions `0x02` / `0x03` do not call partial flush; normal play uses JIT stores. Cheats or other C-side writers that hit `write_memory*` into IW/EW while dynarec is active could theoretically desync unless they go through the same invalidation rules.
- If you ever need **binary compatibility** of in-memory SMC mirrors across versions, note that **tag values** in saved RAM are not part of the BSON save format for the first 256 KiB of EWRAM alone; dynarec is flushed on state load in this tree.

---

*Document added for the Ndless `nspire-libretro-standalone` gpSP port; issue: partial SMC flush correctness (Mario Golf–class symptoms).*
