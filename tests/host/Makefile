# PC tests (run.sh runs them all):
#   core_tests      the core with its interpreter (the dynarec is ARM only):
#                   compressed ROMs, save states, in-game saves
#   frontend_tests  frame turning, key bindings, the settings files
#   gpsp-host       all of gpSP's frontend (menus, file list, game loop) on a
#                   simulated calculator (host_hw.c), driven by scripts

ROOT  := ../..
CORE  := $(ROOT)/libretro-gpsp
COMM  := $(CORE)/libretro/libretro-common
BUILD := build

DEFS  := -DNSPIRE_BUILD -DNSPIRE_LIBRETRO -DNSPIRE_NO_AUDIO -DNSPIRE_HOST -DROM_BUFFER_SIZE=8
INCS  := -Iinclude -I$(CORE) -I$(CORE)/libretro -I$(COMM)/include -I$(ROOT) -I$(ROOT)/nspire
# The core and gpSP's old menu code have many warnings; they stay on for the
# new frontend files.
CORE_FLAGS  := -O1 -g -w $(DEFS) $(INCS)
FRONT_FLAGS := -O1 -g -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
               -Wno-old-style-declaration $(DEFS) $(INCS)
OLD_FLAGS   := -O1 -g -std=gnu99 -w $(DEFS) $(INCS)

CORE_C   := main gba_memory savestate sound cheats memmap serial gbp rfu serial_proto gba_cc_lut
COMM_C   := compat/compat_posix_string compat/compat_strl compat/fopen_utf8 encodings/encoding_utf \
            file/file_path file/file_path_io streams/file_stream string/stdstring time/rtime \
            vfs/vfs_implementation

CORE_OBJS := $(addprefix $(BUILD)/core_,$(addsuffix .o,$(CORE_C))) $(BUILD)/core_cpu.o \
             $(addprefix $(BUILD)/comm_,$(addsuffix .o,$(notdir $(COMM_C)))) $(BUILD)/bios_data.o

# The unit tests: the parts that don't touch the calculator, and stubs.
UNIT_FRONT := nspire_gbz nspire_stubs nspire_backup nspire_states nspire_turn nspire_keys nspire_config \
              nspire_brightness nspire_game_list nspire_blend nspire_zip
UNIT_OBJS  := $(CORE_OBJS) $(BUILD)/core_video.o \
              $(addprefix $(BUILD)/front_,$(addsuffix .o,$(UNIT_FRONT))) $(BUILD)/test_host_stubs.o

# gpsp-host: the whole frontend, with the renderers renamed as on the calculator.
HOST_FRONT := nspire_gbz nspire_stubs nspire_backup nspire_states nspire_turn nspire_keys nspire_config \
              nspire_input nspire_video_ui nspire_video_present nspire_frameskip \
              nspire_video_renderer_dispatch nspire_upscale_from_gba nspire_brightness \
              nspire_game_list nspire_blend nspire_zip nspire_loading nspire_save_manager
HOST_OBJS  := $(CORE_OBJS) $(BUILD)/video_cc_renderer.o $(BUILD)/old_video_renderer.o \
              $(addprefix $(BUILD)/front_,$(addsuffix .o,$(HOST_FRONT))) \
              $(BUILD)/host_main.o $(BUILD)/host_gui.o $(BUILD)/host_nspire.o \
              $(BUILD)/test_host_hw.o $(BUILD)/test_host_platform.o

all: $(BUILD)/core_tests $(BUILD)/frontend_tests $(BUILD)/gpsp-host

$(BUILD):
	mkdir -p $@

$(BUILD)/core_%.o: $(CORE)/%.c | $(BUILD)
	gcc $(CORE_FLAGS) -c $< -o $@
$(BUILD)/core_%.o: $(CORE)/%.cc | $(BUILD)
	g++ $(CORE_FLAGS) -std=gnu++11 -c $< -o $@

define comm_rule
$(BUILD)/comm_$(notdir $(1)).o: $(COMM)/$(1).c | $(BUILD)
	gcc $(CORE_FLAGS) -c $$< -o $$@
endef
$(foreach f,$(COMM_C),$(eval $(call comm_rule,$(f))))

$(BUILD)/bios_data.o: $(CORE)/bios_data.S | $(BUILD)
	gcc -c $< -I$(CORE) -o $@

$(BUILD)/video_cc_renderer.o: $(CORE)/video.cc | $(BUILD)
	g++ $(CORE_FLAGS) -std=gnu++11 \
	-Dupdate_scanline=video_cc_update_scanline \
	-Dvideo_reload_counters=video_cc_video_reload_counters \
	-Dgba_screen_pixels=video_cc_gba_screen_pixels \
	-Daffine_reference_x=video_cc_affine_reference_x \
	-Daffine_reference_y=video_cc_affine_reference_y \
	-c $< -o $@

$(BUILD)/old_video_renderer.o: $(ROOT)/old_video/video.c | $(BUILD)
	gcc $(OLD_FLAGS) \
	-Dupdate_scanline=old_video_update_scanline \
	-Dvideo_reload_counters=old_video_video_reload_counters \
	-Dgba_screen_pixels=old_video_gba_screen_pixels \
	-Daffine_reference_x=old_video_affine_reference_x \
	-Daffine_reference_y=old_video_affine_reference_y \
	-Dlayer_order=old_video_layer_order \
	-Dlayer_count=old_video_layer_count \
	-c $< -o $@

$(BUILD)/front_%.o: $(ROOT)/%.c | $(BUILD)
	gcc $(FRONT_FLAGS) -c $< -o $@
$(BUILD)/host_main.o: $(ROOT)/nspire_host_main.c | $(BUILD)
	gcc $(FRONT_FLAGS) -Dmain=gpsp_main -c $< -o $@
$(BUILD)/host_gui.o: $(ROOT)/gui.c | $(BUILD)
	gcc $(OLD_FLAGS) -DCOMPILING_GUI_MODULE -c $< -o $@
$(BUILD)/host_nspire.o: $(ROOT)/nspire/nspire.c | $(BUILD)
	gcc $(FRONT_FLAGS) -c $< -o $@
$(BUILD)/test_%.o: %.c | $(BUILD)
	gcc $(FRONT_FLAGS) -c $< -o $@

$(BUILD)/core_tests: $(UNIT_OBJS) $(BUILD)/test_core_tests.o
	g++ $^ -o $@ -lz

$(BUILD)/frontend_tests: $(UNIT_OBJS) $(BUILD)/test_frontend_tests.o
	g++ $^ -o $@ -lz

# gui.c's menus are GCC nested functions, whose trampolines need an
# executable stack. The wrapped functions cost virtual time (host_hw.c).
HOST_WRAP := execute_arm update_scanline nspire_turn_raw nspire_turn_1x nspire_turn_rect_555 \
             nspire_turn_rect_565 nspire_blend_turn_aspect nspire_blend_turn_full \
             filestream_read zip_read
$(BUILD)/gpsp-host: $(HOST_OBJS)
	g++ $^ -o $@ -lz -lm -z execstack $(addprefix -Wl$(comma)--wrap=,$(HOST_WRAP))

comma := ,

clean:
	rm -rf $(BUILD)

.PHONY: all clean
