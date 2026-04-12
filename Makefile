# Self-contained TI-Nspire build: all sources live under this directory.
# From here: make

CORE_DIR := libretro-gpsp
COMM_DIR := $(CORE_DIR)/libretro/libretro-common

GCC  := nspire-gcc
GXX  := nspire-g++
GENZ := genzehn

BASE_FLAGS := -DNSPIRE_BUILD -DNSPIRE_LIBRETRO -DARM_ARCH -DHAVE_DYNAREC=1 \
              -DSMALL_TRANSLATION_CACHE -DROM_BUFFER_SIZE=8
INCLUDES   := -I$(CORE_DIR) -I$(CORE_DIR)/libretro -I$(COMM_DIR)/include -I. -Inspire

CFLAGS := -O2 -std=c99 -msoft-float -funsigned-char -fno-common \
          -D_GNU_SOURCE \
          $(BASE_FLAGS) $(INCLUDES) -Wall -Wfatal-errors

CXXFLAGS := -O2 -std=gnu++11 -msoft-float -funsigned-char -fno-common \
            $(BASE_FLAGS) $(INCLUDES) -fno-exceptions -fno-rtti -Wall -Wfatal-errors

GUI_CPPFLAGS := -DCOMPILING_GUI_MODULE

LDFLAGS :=
LDLIBS  := -lm -lz

ZEHNFLAGS := --version 12 --author "gpSP libretro port" --clickpad-support=false \
             --color-support=true --compress --ndless-min 31 --ndless-rev-min 2001

VPATH := $(CORE_DIR):$(CORE_DIR)/arm:$(COMM_DIR)/compat:$(COMM_DIR)/encodings:\
$(COMM_DIR)/file:$(COMM_DIR)/streams:$(COMM_DIR)/string:$(COMM_DIR)/time:\
$(COMM_DIR)/vfs

COMM_OBJS := \
  compat_posix_string.o compat_strl.o fopen_utf8.o encoding_utf.o \
  file_path.o file_path_io.o file_stream.o stdstring.o rtime.o vfs_implementation.o

CORE_OBJS := \
  main.o gba_memory.o savestate.o sound.o cheats.o memmap.o serial.o \
  gbp.o rfu.o serial_proto.o gba_cc_lut.o cpu_threaded.o video.o

CORE_ASM_OBJS := bios_data.o arm_stub.o

CORE_CXX_OBJS := cpu.o

LOCAL_OBJS := \
  nspire_host_main.o nspire_frameskip.o nspire_video_present.o nspire_video_ui.o \
  nspire_input.o nspire_stubs.o nspire_stack_glue.o \
  video_blend.o upscale_aspect.o nspire.o

OBJS := $(COMM_OBJS) $(CORE_OBJS) $(CORE_ASM_OBJS) $(CORE_CXX_OBJS) \
        $(LOCAL_OBJS) gui.o zip.o

.PHONY: all clean

all: gpsp_libretro.tns

gui.o: gui.c
	$(GCC) $(CFLAGS) $(GUI_CPPFLAGS) -c $< -o $@

zip.o: zip.c
	$(GCC) $(CFLAGS) -c $< -o $@

nspire.o: nspire/nspire.c
	$(GCC) $(CFLAGS) -c $< -o $@

video_blend.o: nspire/video_blend.S
	$(GCC) $(CFLAGS) -c $< -o $@

upscale_aspect.o: nspire/upscale_aspect.s
	$(GCC) $(CFLAGS) -c $< -o $@

cpu.o: $(CORE_DIR)/cpu.cc
	$(GXX) $(CXXFLAGS) -c $< -o $@

video.o: $(CORE_DIR)/video.cc
	$(GXX) $(CXXFLAGS) -c $< -o $@

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
	rm -f $(OBJS) gpsp_libretro.elf gpsp_libretro.tns
