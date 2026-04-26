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

GCC  := nspire-gcc
GXX  := nspire-g++
GENZ := genzehn

BASE_FLAGS := -DNSPIRE_BUILD -DNSPIRE_LIBRETRO -DNSPIRE_NO_AUDIO -DARM_ARCH \
              -DHAVE_DYNAREC=1 -DSMALL_TRANSLATION_CACHE -DROM_BUFFER_SIZE=2
INCLUDES   := -I$(CORE_DIR) -I$(CORE_DIR)/libretro -I$(COMM_DIR)/include \
              $(INCLUDES_ARM) -I. -Inspire

# TI-Nspire CX (etc.): ARM926EJ-S, ARMv5TE (+ Thumb, integer DSP). No VFP/NEON.
ARM926FLAGS := -march=armv5te -mtune=arm926ej-s -fomit-frame-pointer -ffast-math

CFLAGS := -O3 -flto -std=c99 -msoft-float -funsigned-char -fno-common \
          $(ARM926FLAGS) \
          -D_GNU_SOURCE \
          $(BASE_FLAGS) $(INCLUDES) -Wall -Wfatal-errors

CXXFLAGS := -O3 -flto -std=gnu++11 -msoft-float -funsigned-char -fno-common \
            $(ARM926FLAGS) \
            $(BASE_FLAGS) $(INCLUDES) -fno-exceptions -fno-rtti -Wall -Wfatal-errors

GUI_CPPFLAGS := -DCOMPILING_GUI_MODULE

LDFLAGS := -O3 -flto $(ARM926FLAGS)
LDLIBS  := -lm

ZEHNFLAGS := --version 12 --author "gpSP libretro port" --clickpad-support=false \
             --color-support=true --compress --ndless-min 31 --ndless-rev-min 2001

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
  video_blend.o upscale_aspect.o scanline_fill16.o nspire_upscale_from_gba.o nspire.o nspire_rom_load_diag.o

OBJS := $(COMM_OBJS) $(CORE_OBJS) $(CORE_ASM_OBJS) $(CORE_CXX_OBJS) \
        $(LOCAL_OBJS) gui.o

# Same flags as CXXFLAGS but without LTO so -S yields readable per-TU assembly.
CXXFLAGS_NO_LTO := $(filter-out -flto,$(CXXFLAGS))

.PHONY: all clean video-cc-asm

all: gpsp_libretro.tns

# Inspect video.cc codegen (ARM926EJ-S): docker compose run --rm ndless-dev make video-cc-asm
video-cc-asm: $(CORE_DIR)/video.cc
	$(GXX) $(CXXFLAGS_NO_LTO) -S -fverbose-asm -o video.cc.asm $<

gui.o: gui.c
	$(GCC) $(CFLAGS) $(GUI_CPPFLAGS) -c $< -o $@

nspire.o: nspire/nspire.c
	$(GCC) $(CFLAGS) -c $< -o $@

video_blend.o: nspire/video_blend.S
	$(GCC) $(CFLAGS) -c $< -o $@

upscale_aspect.o: nspire/upscale_aspect.s
	$(GCC) $(CFLAGS) -c $< -o $@

scanline_fill16.o: nspire/scanline_fill16.S
	$(GCC) $(CFLAGS) -c $< -o $@

cpu.o: $(CORE_DIR)/cpu.cc
	$(GXX) $(CXXFLAGS) -c $< -o $@

video_cc_renderer.o: $(CORE_DIR)/video.cc
	$(GXX) $(CXXFLAGS) \
	-Dupdate_scanline=video_cc_update_scanline \
	-Dvideo_reload_counters=video_cc_video_reload_counters \
	-Dgba_screen_pixels=video_cc_gba_screen_pixels \
	-Daffine_reference_x=video_cc_affine_reference_x \
	-Daffine_reference_y=video_cc_affine_reference_y \
	-c $< -o $@

old_video_renderer.o: old_video/video.c
	$(GCC) $(CFLAGS) \
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
	$(GCC) $(CFLAGS) -c $< -o $@

%.o: %.cc
	$(GXX) $(CXXFLAGS) -c $< -o $@

%.o: %.S
	$(GCC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(GCC) $(CFLAGS) -c $< -o $@

gpsp_libretro.elf: $(OBJS)
	$(GXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

gpsp_libretro.tns: gpsp_libretro.elf
	$(GENZ) $(ZEHNFLAGS) --input $< --output $@

clean:
	rm -f $(OBJS) gpsp_libretro.elf gpsp_libretro.tns video.cc.asm
