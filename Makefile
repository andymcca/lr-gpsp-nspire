# Self-contained TI-Nspire build: all sources live under this directory.
# Typical build: see README.txt (docker compose run --rm ndless-dev make).
# With Ndless SDK on PATH: make

CORE_DIR := libretro-gpsp
# Vendored tree may ship a stub libretro-common; use sibling repo copy when needed.
COMM_DIR_LOCAL  := $(CORE_DIR)/libretro/libretro-common
COMM_DIR_PARENT := ../libretro-gpsp/libretro/libretro-common
ifeq ($(wildcard $(COMM_DIR_LOCAL)/include/streams/file_stream.h),)
  COMM_DIR := $(COMM_DIR_PARENT)
else
  COMM_DIR := $(COMM_DIR_LOCAL)
endif

# Vendored core may omit arm/ (dynarec); use sibling ../libretro-gpsp like COMM_DIR.
ARM_EMIT_LOCAL := $(CORE_DIR)/arm/arm_emit.h
ARM_DIR_PARENT := ../libretro-gpsp/arm
ifeq ($(wildcard $(ARM_EMIT_LOCAL)),)
  INCLUDES_ARM := -I../libretro-gpsp
  ARM_VPATH := $(ARM_DIR_PARENT)
else
  INCLUDES_ARM :=
  ARM_VPATH := $(CORE_DIR)/arm
endif

GCC     := nspire-gcc
GXX     := nspire-g++
GENZ    := genzehn
MAKEPRG := make-prg

# These are discovered from the installed SDK wrappers, but can be overridden
# when a developer keeps the SDK or third-party files in a custom location.
NDLESS_ROOT ?= $(patsubst %/,%,$(shell nspire-tools path 2>/dev/null))
NDLESS_TOOLCHAIN ?= $(patsubst %/,%,$(shell nspire-tools _toolchainpath 2>/dev/null))
NDLESS_HOME ?= $(if $(HOME),$(HOME)/.ndless)
NDLESS_INCLUDE_FLAGS := $(if $(and $(strip $(NDLESS_ROOT)),$(wildcard $(NDLESS_ROOT)/include)),-I$(NDLESS_ROOT)/include) \
                        $(if $(wildcard $(NDLESS_HOME)/include),-I$(NDLESS_HOME)/include)
NDLESS_LIB_FLAGS := $(if $(and $(strip $(NDLESS_ROOT)),$(wildcard $(NDLESS_ROOT)/lib)),-L$(NDLESS_ROOT)/lib) \
                    $(if $(wildcard $(NDLESS_HOME)/lib),-L$(NDLESS_HOME)/lib)

BASE_FLAGS := -DNSPIRE_BUILD -DNSPIRE_LIBRETRO -DNSPIRE_NO_AUDIO -DARM_ARCH \
              -DHAVE_DYNAREC=1 -DSMALL_TRANSLATION_CACHE -DROM_BUFFER_SIZE=2
INCLUDES   := -I$(CORE_DIR) -I$(CORE_DIR)/libretro -I$(COMM_DIR)/include \
              $(INCLUDES_ARM) $(NDLESS_INCLUDE_FLAGS) -I. -Inspire

# TI-Nspire CX/CX II: ARM926EJ-S (ARMv5TE), ARM instructions and software float.
# Do not enable VFP/NEON; these processors do not implement those instruction sets.
ARM926FLAGS := -march=armv5te -mtune=arm926ej-s -marm -msoft-float \
               -fomit-frame-pointer -ffast-math

CFLAGS := -O3 -flto -std=c99 -msoft-float -funsigned-char -fno-common \
          $(ARM926FLAGS) \
          -D_GNU_SOURCE \
          $(BASE_FLAGS) $(INCLUDES) -Wall -Wfatal-errors

CXXFLAGS := -O3 -flto -std=gnu++11 -msoft-float -funsigned-char -fno-common \
            $(ARM926FLAGS) \
            $(BASE_FLAGS) $(INCLUDES) -fno-exceptions -fno-rtti -Wall -Wfatal-errors

# Assembly gets target/feature flags and include paths, but not C/C++ flags such
# as -std=c99 or LTO. All C/C++ recipes also emit header dependencies below.
ASFLAGS := -O3 $(ARM926FLAGS) $(BASE_FLAGS) $(INCLUDES) -Wall -Wfatal-errors
DEPFLAGS := -MMD -MP

GUI_CPPFLAGS := -DCOMPILING_GUI_MODULE

LDFLAGS := -O3 -flto $(ARM926FLAGS) $(NDLESS_LIB_FLAGS)
LDLIBS  := -lz -lm

ZEHNFLAGS := --version 12 --author "gpSP libretro port" --clickpad-support=false \
             --touchpad-support=true --color-support=true --240x320-support=true \
             --compress --ndless-min 31 --ndless-rev-min 2001

VPATH := $(CORE_DIR):$(ARM_VPATH):$(COMM_DIR)/compat:$(COMM_DIR)/encodings:\
$(COMM_DIR)/file:$(COMM_DIR)/streams:$(COMM_DIR)/string:$(COMM_DIR)/time:\
$(COMM_DIR)/vfs

COMM_OBJS := \
  compat_posix_string.o compat_strl.o fopen_utf8.o encoding_utf.o \
  file_path.o file_path_io.o file_stream.o stdstring.o rtime.o vfs_implementation.o

CORE_OBJS := \
  main.o gba_memory.o savestate.o sound.o cheats.o memmap.o serial.o \
  gbp.o rfu.o serial_proto.o gba_cc_lut.o cpu_threaded.o

CORE_ASM_OBJS := bios_data.o arm_stub.o

CORE_CXX_OBJS := cpu.o

LOCAL_OBJS := \
  nspire_host_main.o nspire_frameskip.o nspire_video_present.o nspire_video_ui.o \
  nspire_input.o nspire_stubs.o nspire_backup.o nspire_stack_glue.o \
  nspire_video_renderer_dispatch.o video_cc_renderer.o old_video_renderer.o \
  video_blend.o upscale_aspect.o scanline_fill16.o nspire_upscale_from_gba.o nspire_turn.o nspire.o nspire_rom_load_diag.o \
  nspire_gbz.o nspire_states.o nspire_config.o nspire_clock.o clock_switch_nspire.o nspire_keys.o \
  nspire_brightness.o nspire_game_list.o nspire_blend.o \
  nspire_zip.o nspire_loading.o nspire_save_manager.o nspire_paths.o \
  nspire_selfstore.o nspire_rom_convert.o nspire_rom_manager.o

OBJS := $(COMM_OBJS) $(CORE_OBJS) $(CORE_ASM_OBJS) $(CORE_CXX_OBJS) \
        $(LOCAL_OBJS) gui.o

$(OBJS): | check-sdk

# Same flags as CXXFLAGS but without LTO so -S yields readable per-TU assembly.
CXXFLAGS_NO_LTO := $(filter-out -flto,$(CXXFLAGS))

.PHONY: all clean video-cc-asm check-sdk

all: check-sdk gpsp_libretro.tns

# Verify the real Ndless toolchain and zlib before any object compiles. This
# prerequisite is attached to every object too, so `make -j` cannot race ahead.
check-sdk:
	@set -eu; \
	for tool in "$(GCC)" "$(GXX)" "$(GENZ)" "$(MAKEPRG)" nspire-tools; do \
	  command -v "$$tool" >/dev/null 2>&1 || { echo "ERROR: required Ndless tool '$$tool' is not on PATH." >&2; exit 1; }; \
	done; \
	toolchain="$(NDLESS_TOOLCHAIN)"; \
	if [ -z "$$toolchain" ] || [ ! -x "$$toolchain/arm-none-eabi-gcc" ]; then \
	  command -v arm-none-eabi-gcc >/dev/null 2>&1 || { echo "ERROR: arm-none-eabi-gcc is missing; install/build the Ndless ARM toolchain first." >&2; exit 1; }; \
	fi; \
	if [ -z "$$toolchain" ] || [ ! -x "$$toolchain/arm-none-eabi-g++" ]; then \
	  command -v arm-none-eabi-g++ >/dev/null 2>&1 || { echo "ERROR: arm-none-eabi-g++ is missing; install/build the Ndless C++ toolchain first." >&2; exit 1; }; \
	fi; \
	root="$(NDLESS_ROOT)"; \
	[ -n "$$root" ] || { echo "ERROR: nspire-tools could not locate the Ndless SDK root." >&2; exit 1; }; \
	if [ -r "$$root/include/zlib.h" ] || [ -r "$(NDLESS_HOME)/include/zlib.h" ]; then :; \
	else echo "ERROR: zlib.h missing from $$root/include and $(NDLESS_HOME)/include; build/install the Ndless third-party zlib headers." >&2; exit 1; fi; \
	if [ -r "$$root/lib/libz.a" ] || [ -r "$(NDLESS_HOME)/lib/libz.a" ]; then :; \
	else echo "ERROR: libz.a missing from $$root/lib and $(NDLESS_HOME)/lib; build/install the Ndless third-party zlib library." >&2; exit 1; fi; \
	echo "Ndless tools and zlib are present (SDK: $$root)."

# Inspect video.cc codegen (ARM926EJ-S): docker compose run --rm ndless-dev make video-cc-asm
video-cc-asm: $(CORE_DIR)/video.cc
	$(GXX) $(CXXFLAGS_NO_LTO) -S -fverbose-asm -o video.cc.asm $<

gui.o: gui.c
	$(GCC) $(CFLAGS) $(DEPFLAGS) $(GUI_CPPFLAGS) -c $< -o $@

nspire.o: nspire/nspire.c
	$(GCC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

video_blend.o: nspire/video_blend.S
	$(GCC) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

upscale_aspect.o: nspire/upscale_aspect.s
	$(GCC) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

scanline_fill16.o: nspire/scanline_fill16.S
	$(GCC) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

clock_switch_nspire.o: nspire/clock_switch_nspire.S
	$(GCC) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

cpu.o: $(CORE_DIR)/cpu.cc
	$(GXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

video_cc_renderer.o: $(CORE_DIR)/video.cc
	$(GXX) $(CXXFLAGS) $(DEPFLAGS) \
	-Dupdate_scanline=video_cc_update_scanline \
	-Dvideo_reload_counters=video_cc_video_reload_counters \
	-Dgba_screen_pixels=video_cc_gba_screen_pixels \
	-Daffine_reference_x=video_cc_affine_reference_x \
	-Daffine_reference_y=video_cc_affine_reference_y \
	-c $< -o $@

old_video_renderer.o: old_video/video.c
	$(GCC) $(CFLAGS) $(DEPFLAGS) \
	-DARM_ARCH_BLENDING_OPTS \
	-Dupdate_scanline=old_video_update_scanline \
	-Dvideo_reload_counters=old_video_video_reload_counters \
	-Dgba_screen_pixels=old_video_gba_screen_pixels \
	-Daffine_reference_x=old_video_affine_reference_x \
	-Daffine_reference_y=old_video_affine_reference_y \
	-Dlayer_order=old_video_layer_order \
	-Dlayer_count=old_video_layer_count \
	-c $< -o $@

%.o: %.c
	$(GCC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

%.o: %.cc
	$(GXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

%.o: %.S
	$(GCC) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

%.o: %.s
	$(GCC) $(ASFLAGS) $(DEPFLAGS) -c $< -o $@

gpsp_libretro.elf: $(OBJS)
	$(GXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

gpsp_libretro.tns.zehn: gpsp_libretro.elf
	$(GENZ) $(ZEHNFLAGS) --input $< --output $@

gpsp_libretro.tns: gpsp_libretro.tns.zehn
	$(MAKEPRG) $< $@

-include $(OBJS:.o=.d)

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) gpsp_libretro.elf gpsp_libretro.tns gpsp_libretro.tns.zehn video.cc.asm
