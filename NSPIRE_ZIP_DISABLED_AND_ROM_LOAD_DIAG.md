# Nspire: ZIP disabled, static ROM buffer removed, ROM load diagnostics

This note applies to **TI-Nspire standalone** builds (`NSPIRE_LIBRETRO`): `nspire-libretro-standalone` and the sibling `nspire-libretro` Makefile that compiles `../libretro-gpsp`.

## 1. ZIP support removed

**Motivation:** ZIP loading used a **static `.bss` buffer** (`gamepak_rom[]`, size `ROM_BUFFER_SIZE` MiB, typically 8 MiB on Nspire) plus `zip.c` / zlib. Disabling ZIP drops that fixed RAM cost and simplifies failure modes on low-memory models.

**Code / build changes:**

| Area | Change |
|------|--------|
| `libretro-gpsp/gba_memory.c` and `nspire-libretro-standalone/libretro-gpsp/gba_memory.c` | Removed `load_file_zip` / `load_gamepak_from_ram` path; removed `gamepak_rom[]` and `gamepak_ram_buffer_size`. `.zip` / `.zip.tns` paths in `load_gamepak` now show an error on the debug screen and return failure. |
| `libretro-gpsp/gba_memory.h` (both trees) | Dropped `extern` declarations for `gamepak_rom` and `gamepak_ram_buffer_size` under `NSPIRE_LIBRETRO`. |
| `nspire-libretro-standalone/Makefile` | Stopped compiling `zip.o`; removed `-lz` from `LDLIBS` (zlib was only required for ZIP). |
| `nspire-libretro/Makefile` | Same as above; `nspire_rom_load_diag.c` is built from `../nspire-libretro-standalone/`. |
| `nspire_host_main.c` (both Nspire entry points) | `file_ext[]` no longer lists `.zip.tns`. |
| `gui.c` / `nspire-libretro-standalone/gui.c` | In-game load menu wildcards: removed `.zip.tns` for Nspire. |

**User impact:** Use **`.gba.tns`** (or plain `.gba` where applicable) only. To restore ZIP later, re-link `zip.o`, restore `-lz`, revert the `gba_memory.c` / `.h` ZIP block and static buffer, and restore file wildcards.

**Note:** `zip.c` may remain in the repository but is **not** linked into the Nspire `.tns` with the current Makefiles.

## 2. ROM load verbose trace + “press any key”

**New files (standalone root):**

- `nspire_rom_load_diag.h` — declarations for the debug-screen helpers.
- `nspire_rom_load_diag.c` — uses `debug_screen_*` (`nspire_video_ui.c`), `get_gui_input()` (`input.h`), and `delay_us()` from `nspire_host_main.c`.

**Behavior (only when `NSPIRE_LIBRETRO` is defined):**

1. **`load_gamepak`** opens the debug screen and prints the ROM path, then **`1MiB buffers OK: N / ROM_BUFFER_SIZE`** (result of `init_gamepak_buffer()` mallocs).
2. If **N == 0**, prints failure, **press any key**, returns error (avoids dereferencing `gamepak_buffers[0]`).
3. If the path looks like **ZIP**, prints that ZIP is disabled, key, error.
4. **`load_gamepak_raw`** logs rounded size, block counts, `ldblks`, open failures, and whether **swapping** will be needed after the initial map.
5. On **successful** load, after `load_game_config_over` / RTC/rumble setup, prints **ROM load OK** and **press any key** before returning so the game loop only starts after confirmation.

**Include path:** `gba_memory.c` includes `nspire_rom_load_diag.h` under `NSPIRE_LIBRETRO`. The `nspire-libretro` Makefile adds `-I../nspire-libretro-standalone` so the header is found when compiling the shared `../libretro-gpsp/gba_memory.c`.

## 3. Related fix: LRU tail when no buffers allocated

`init_gamepak_buffer()` previously set `gamepak_lru_tail = 32 * gamepak_buffer_count - 1`, which **underflows** when `gamepak_buffer_count == 0`. It now sets `gamepak_lru_tail = 0` in that case.

## 4. Disabling the diagnostic pause (optional future tweak)

To remove the “press any key” gate while keeping logs, replace the final `nspire_rom_load_diag_press_any_key()` call on the success path in `load_gamepak` with `debug_screen_end()` only, or guard the helpers behind a compile-time flag (not implemented in this change).
