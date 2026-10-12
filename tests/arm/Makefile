# Tests of gpSP's ARM-only code, built for the calculator's CPU and run
# under qemu-arm (tests/arm/run.sh builds and runs them):
#   blend_test    the one-pass blended pictures (nspire_blend.c) against the
#                 old path (notaz's scalers in nspire/upscale_aspect.s)
#   fill_test     video.cc's backdrop fill (nspire/scanline_fill16.S) at
#                 every start pixel: it crashed on odd ones
#   dynarec_test  the core with its ARM dynarec on rom/ram_code.S: code in
#                 RAM branching between its blocks and rewritten, with either
#                 "Dynarec RAM / SMC" setting
#   badjump_test  the core on rom/bad_jump.S, which jumps where no code can
#                 run (as a damaged Advance Wars does): it has to stop there
#   replay        the core playing a real game from a save state (run.sh
#                 runs it on Pokemon Emerald when $GBA_GAMES has it)
# The calculator's files are compiled as for the calculator (nspire-gcc,
# -O3, without -flto so they link here); the core is compiled with the
# calculator's flags by arm-none-eabi-gcc with the PC tests' stand-in os.h
# (Ndless's headers clash with it). The test programs use newlib's
# semihosting library. Needs the Ndless toolchain (. ~/nspire/build/env.sh).

ROOT  := ../..
CORE  := $(ROOT)/libretro-gpsp
COMM  := $(CORE)/libretro/libretro-common
BUILD := build
ZLIB  := $(shell dirname $$(command -v nspire-gcc))/../thirdparty/zlib
LIBZ  := $(shell dirname $$(command -v nspire-gcc))/../lib/libz.a

ARCH  := -marm -mcpu=arm926ej-s
CALC_FLAGS := -O3 -std=c99 -msoft-float -funsigned-char -fno-common $(ARCH) -fomit-frame-pointer \
              -DNSPIRE_BUILD -DNSPIRE_LIBRETRO -DNSPIRE_NO_AUDIO -DARM_ARCH \
              -I$(CORE) -I$(CORE)/libretro -I$(CORE)/libretro/libretro-common/include \
              -I$(ROOT) -I$(ROOT)/nspire -Wall
TEST_FLAGS := -O2 $(ARCH) -Wall

# The core as the calculator's Makefile builds it (minus -flto).
CORE_DEFS  := -DNSPIRE_BUILD -DNSPIRE_LIBRETRO -DNSPIRE_NO_AUDIO -DNSPIRE_HOST -DARM_ARCH \
              -DHAVE_DYNAREC=1 -DSMALL_TRANSLATION_CACHE -DROM_BUFFER_SIZE=2
CORE_INCS  := -Ishim -I../host/include -I$(CORE) -I$(CORE)/libretro -I$(COMM)/include \
              -I$(ROOT) -I$(ROOT)/nspire -I$(ZLIB)
CORE_FLAGS := -O3 -msoft-float -funsigned-char -fno-common $(ARCH) -fomit-frame-pointer \
              -ffast-math -D_GNU_SOURCE $(CORE_DEFS) $(CORE_INCS) -w

CORE_C := main gba_memory savestate sound cheats memmap serial gbp rfu serial_proto gba_cc_lut \
          cpu_threaded
COMM_C := compat/compat_posix_string compat/compat_strl compat/fopen_utf8 encodings/encoding_utf \
          file/file_path file/file_path_io streams/file_stream string/stdstring time/rtime \
          vfs/vfs_implementation
CORE_FRONT := nspire_stubs nspire_zip nspire_gbz nspire_video_renderer_dispatch
CORE_OBJS := $(addprefix $(BUILD)/core_,$(addsuffix .o,$(CORE_C))) $(BUILD)/core_cpu.o \
             $(addprefix $(BUILD)/comm_,$(addsuffix .o,$(notdir $(COMM_C)))) \
             $(addprefix $(BUILD)/front_,$(addsuffix .o,$(CORE_FRONT))) \
             $(BUILD)/bios_data.o $(BUILD)/arm_stub.o $(BUILD)/video_blend.o $(BUILD)/scanline_fill16.o \
             $(BUILD)/video_cc_renderer.o $(BUILD)/old_video_renderer.o $(BUILD)/core_stubs.o \
             $(BUILD)/newlib_shim.o

all: $(BUILD)/blend_test $(BUILD)/fill_test $(BUILD)/dynarec_test $(BUILD)/replay $(BUILD)/ram_code.gba \
     $(BUILD)/badjump_test $(BUILD)/bad_jump_thumb.gba $(BUILD)/bad_jump_arm.gba

$(BUILD):
	mkdir -p $@

$(BUILD)/calc_%.o: $(ROOT)/%.c | $(BUILD)
	nspire-gcc $(CALC_FLAGS) -c $< -o $@

$(BUILD)/upscale_aspect.o: $(ROOT)/nspire/upscale_aspect.s | $(BUILD)
	arm-none-eabi-gcc $(ARCH) -c $< -o $@

$(BUILD)/test_%.o: %.c | $(BUILD)
	arm-none-eabi-gcc $(TEST_FLAGS) -c $< -o $@

# -fno-builtin: GCC would turn the shim's calloc into a call to calloc.
$(BUILD)/newlib_shim.o: newlib_shim.c | $(BUILD)
	arm-none-eabi-gcc $(ARCH) -O2 -fno-builtin -c $< -o $@

$(BUILD)/blend_test: $(BUILD)/test_blend_test.o $(BUILD)/calc_nspire_blend.o $(BUILD)/calc_nspire_turn.o \
                     $(BUILD)/upscale_aspect.o $(BUILD)/newlib_shim.o
	arm-none-eabi-gcc $(ARCH) -specs=rdimon.specs $^ -o $@

$(BUILD)/fill_test: $(BUILD)/test_fill_test.o $(BUILD)/scanline_fill16.o $(BUILD)/newlib_shim.o
	arm-none-eabi-gcc $(ARCH) -specs=rdimon.specs $^ -o $@

# ---- The core ----

$(BUILD)/core_%.o: $(CORE)/%.c | $(BUILD)
	arm-none-eabi-gcc $(CORE_FLAGS) -std=gnu99 -c $< -o $@
$(BUILD)/core_cpu.o: $(CORE)/cpu.cc | $(BUILD)
	arm-none-eabi-g++ $(CORE_FLAGS) -std=gnu++11 -fno-exceptions -fno-rtti -c $< -o $@
define comm_rule
$(BUILD)/comm_$(notdir $(1)).o: $(COMM)/$(1).c | $(BUILD)
	arm-none-eabi-gcc $$(CORE_FLAGS) -std=gnu99 -c $$< -o $$@
endef
$(foreach f,$(COMM_C),$(eval $(call comm_rule,$(f))))
$(BUILD)/front_%.o: $(ROOT)/%.c | $(BUILD)
	arm-none-eabi-gcc $(CORE_FLAGS) -std=gnu99 -c $< -o $@
$(BUILD)/core_stubs.o: core_stubs.c | $(BUILD)
	arm-none-eabi-gcc $(CORE_FLAGS) -std=gnu99 -c $< -o $@
$(BUILD)/bios_data.o: $(CORE)/bios_data.S | $(BUILD)
	arm-none-eabi-gcc $(ARCH) -I$(CORE) -c $< -o $@
$(BUILD)/arm_stub.o: $(CORE)/arm/arm_stub.S | $(BUILD)
	arm-none-eabi-gcc $(ARCH) $(CORE_DEFS) $(CORE_INCS) -c $< -o $@
$(BUILD)/video_blend.o: $(ROOT)/nspire/video_blend.S | $(BUILD)
	arm-none-eabi-gcc $(ARCH) $(CORE_DEFS) $(CORE_INCS) -c $< -o $@
$(BUILD)/scanline_fill16.o: $(ROOT)/nspire/scanline_fill16.S | $(BUILD)
	arm-none-eabi-gcc $(ARCH) -c $< -o $@
# The renderers renamed as in the calculator's Makefile.
$(BUILD)/video_cc_renderer.o: $(CORE)/video.cc | $(BUILD)
	arm-none-eabi-g++ $(CORE_FLAGS) -std=gnu++11 -fno-exceptions -fno-rtti \
	-Dupdate_scanline=video_cc_update_scanline \
	-Dvideo_reload_counters=video_cc_video_reload_counters \
	-Dgba_screen_pixels=video_cc_gba_screen_pixels \
	-Daffine_reference_x=video_cc_affine_reference_x \
	-Daffine_reference_y=video_cc_affine_reference_y -c $< -o $@
$(BUILD)/old_video_renderer.o: $(ROOT)/old_video/video.c | $(BUILD)
	arm-none-eabi-gcc $(CORE_FLAGS) -std=gnu99 -DARM_ARCH_BLENDING_OPTS \
	-Dupdate_scanline=old_video_update_scanline \
	-Dvideo_reload_counters=old_video_video_reload_counters \
	-Dgba_screen_pixels=old_video_gba_screen_pixels \
	-Daffine_reference_x=old_video_affine_reference_x \
	-Daffine_reference_y=old_video_affine_reference_y \
	-Dlayer_order=old_video_layer_order -Dlayer_count=old_video_layer_count -c $< -o $@

$(BUILD)/dynarec_test.o: dynarec_test.c | $(BUILD)
	arm-none-eabi-gcc $(CORE_FLAGS) -std=gnu99 -c $< -o $@
$(BUILD)/replay.o: replay.c | $(BUILD)
	arm-none-eabi-gcc $(CORE_FLAGS) -std=gnu99 -c $< -o $@
$(BUILD)/badjump_test.o: badjump_test.c | $(BUILD)
	arm-none-eabi-gcc $(CORE_FLAGS) -std=gnu99 -c $< -o $@

$(BUILD)/dynarec_test: $(BUILD)/dynarec_test.o $(CORE_OBJS)
	arm-none-eabi-g++ $(ARCH) -specs=rdimon.specs $^ $(LIBZ) -lm -o $@
$(BUILD)/replay: $(BUILD)/replay.o $(CORE_OBJS)
	arm-none-eabi-g++ $(ARCH) -specs=rdimon.specs $^ $(LIBZ) -lm -o $@
$(BUILD)/badjump_test: $(BUILD)/badjump_test.o $(CORE_OBJS)
	arm-none-eabi-g++ $(ARCH) -specs=rdimon.specs $^ $(LIBZ) -lm -o $@

# The test ROM: ARM code at the cartridge's address, as a plain image.
$(BUILD)/ram_code.gba: rom/ram_code.S | $(BUILD)
	arm-none-eabi-gcc $(ARCH) -nostdlib -Wl,-Ttext=0x08000000 -Wl,-e,_start $< -o $(BUILD)/ram_code.elf
	arm-none-eabi-objcopy -O binary $(BUILD)/ram_code.elf $@
$(BUILD)/bad_jump_thumb.gba: rom/bad_jump.S | $(BUILD)
	arm-none-eabi-gcc $(ARCH) -nostdlib -Wl,-Ttext=0x08000000 -Wl,-e,_start -Wa,--defsym,THUMB=1 $< -o $(BUILD)/bad_jump_thumb.elf
	arm-none-eabi-objcopy -O binary $(BUILD)/bad_jump_thumb.elf $@
$(BUILD)/bad_jump_arm.gba: rom/bad_jump.S | $(BUILD)
	arm-none-eabi-gcc $(ARCH) -nostdlib -Wl,-Ttext=0x08000000 -Wl,-e,_start -Wa,--defsym,THUMB=0 $< -o $(BUILD)/bad_jump_arm.elf
	arm-none-eabi-objcopy -O binary $(BUILD)/bad_jump_arm.elf $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
