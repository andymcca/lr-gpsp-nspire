# Tests of gpSP's ARM-only screen code, built for the calculator's CPU and
# run under qemu-arm (tests/arm/run.sh builds and runs them):
#   blend_test   the one-pass blended pictures (nspire_blend.c) against the
#                old path (notaz's scalers in nspire/upscale_aspect.s)
# The calculator's files are compiled as for the calculator (nspire-gcc,
# -O3, without -flto so they link here); the test programs use newlib's
# semihosting library. Needs the Ndless toolchain (. ~/nspire/build/env.sh).

ROOT  := ../..
CORE  := $(ROOT)/libretro-gpsp
BUILD := build

ARCH  := -marm -mcpu=arm926ej-s
CALC_FLAGS := -O3 -std=c99 -msoft-float -funsigned-char -fno-common $(ARCH) -fomit-frame-pointer \
              -DNSPIRE_BUILD -DNSPIRE_LIBRETRO -DNSPIRE_NO_AUDIO -DARM_ARCH \
              -I$(CORE) -I$(CORE)/libretro -I$(CORE)/libretro/libretro-common/include \
              -I$(ROOT) -I$(ROOT)/nspire -Wall
TEST_FLAGS := -O2 $(ARCH) -Wall

all: $(BUILD)/blend_test

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

clean:
	rm -rf $(BUILD)

.PHONY: all clean
