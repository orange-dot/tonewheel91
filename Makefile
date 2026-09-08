CC     ?= gcc
FUZZ_CC ?= clang
BUILD  ?= build
WARN   := -Wall -Wextra -Wpedantic
CFLAGS := -std=c23 -O2 $(WARN) -ffp-contract=off
TEST_EXTRA ?= check-core-symbols

# The core stays freestanding-clean; drivers and tests are hosted.
CORE_CFLAGS := $(CFLAGS) -ffreestanding

CORE_OBJS := $(BUILD)/generator.o $(BUILD)/midi.o $(BUILD)/organ.o $(BUILD)/scanner.o $(BUILD)/drive.o $(BUILD)/rotary.o

# The electric-piano line (docs/piano-backlog.md part 1). Sibling core, not
# a fork: ep_voice.c includes epiano.h, which includes tonewheel.h for the
# shared kernels. No organ object depends on it.
EP_OBJS := $(BUILD)/ep_voice.o $(BUILD)/ep_piano.o

# The Mamut Analog line is a sibling core. Keep it out of the organ and EP
# products; the aggregate core test and symbol audit are its first hosts.
MA_OBJS := $(BUILD)/ma_voice.o $(BUILD)/ma_bank.o $(BUILD)/ma_output.o
MA_RUNTIME_OBJS := $(MA_OBJS) $(BUILD)/drive.o
MA1_LONG_LISTENING_SOURCE := driver/exhibit_ma_blues.c
MA1_LONG_LISTENING_WAV := $(BUILD)/ma_blade_runner_blues_expanded.wav

all: $(BUILD)/test $(BUILD)/test_hosted $(BUILD)/test_midi_map $(BUILD)/test_ma_architecture $(BUILD)/exhibit_phase $(BUILD)/exhibit_contacts $(BUILD)/exhibit_taper $(BUILD)/exhibit_percussion $(BUILD)/exhibit_scanner $(BUILD)/exhibit_drive $(BUILD)/exhibit_rotary $(BUILD)/exhibit_wear $(BUILD)/exhibit_depth $(BUILD)/exhibit_warmth $(BUILD)/exhibit_viz $(BUILD)/exhibit_ep_voice $(BUILD)/render_midi $(BUILD)/render_ma_architecture $(BUILD)/tw91 $(BUILD)/ep73 $(BUILD)/patchlab $(BUILD)/exhibit_ma_blues $(BUILD)/exhibit_ma_blues_panic $(BUILD)/exhibit_ma_blade_runner_main_titles $(BUILD)/exhibit_ma_eno_ascent_noir $(BUILD)/exhibit_ma_nin_hurt_noir $(BUILD)/exhibit_ma_blues_ma2_full $(BUILD)/exhibit_ma_blade_runner_main_titles_ma2_full $(BUILD)/exhibit_ma_nin_hurt_noir_ma2_full $(BUILD)/exhibit_ma_raster $(BUILD)/exhibit_ma_bcs

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CORE_CFLAGS) -c $< -o $@

$(BUILD)/ep_voice.o $(BUILD)/ep_piano.o: $(BUILD)/%.o: src/%.c src/epiano.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CORE_CFLAGS) -c $< -o $@

$(BUILD)/ma_voice.o: src/ma_voice.c src/ma_raster_table.h src/ma_internal.h src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CORE_CFLAGS) -c $< -o $@

$(BUILD)/ma_bank.o $(BUILD)/ma_output.o: $(BUILD)/%.o: src/%.c src/ma_internal.h src/mamutanalog.h src/tonewheel.h | $(BUILD)

$(BUILD)/ma_voice_source.o: src/ma_voice.c src/ma_raster_table.h src/ma_internal.h src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CORE_CFLAGS) -DMA_SOURCE_EVIDENCE -c $< -o $@

$(BUILD)/wav.o: driver/wav.c driver/wav.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/ma_dark_lead.o: driver/ma_dark_lead.c driver/ma_dark_lead.h src/mamutanalog.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/ma_architecture_score.o: driver/ma_architecture_score.c driver/ma_architecture_score.h src/mamutanalog.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/ma_architecture_render.o: driver/ma_architecture_render.c driver/ma_architecture_render.h driver/ma_architecture_score.h driver/ma_dark_lead.h driver/wav.h src/mamutanalog.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/ma_architecture_cli.o: driver/ma_architecture_cli.c driver/ma_architecture_cli.h driver/ma_architecture_render.h driver/host_parse.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/host_parse.o: driver/host_parse.c driver/host_parse.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/ma_patch_file.o: driver/ma_patch_file.c driver/ma_patch_file.h driver/host_parse.h src/mamutanalog.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/smf.o: driver/smf.c driver/smf.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/live_io.o: driver/live_io.c driver/live_io.h driver/host_parse.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/midi_map.o: driver/midi_map.c driver/midi_map.h driver/midi_owner.h src/tonewheel.h src/epiano.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/ep_midi_map.o: driver/ep_midi_map.c driver/midi_map.h driver/midi_owner.h src/tonewheel.h src/epiano.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/viz.o: driver/viz.c driver/viz.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/test: test/test.c $(CORE_OBJS) $(EP_OBJS) $(MA_OBJS) src/tonewheel.h src/epiano.h src/mamutanalog.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/test.c $(CORE_OBJS) $(EP_OBJS) $(MA_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/test_hosted: test/hosted.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(BUILD)/ma_patch_file.o $(BUILD)/live_io.o $(MA_RUNTIME_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/hosted.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(BUILD)/ma_patch_file.o $(BUILD)/live_io.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lasound $(LDLIBS)

$(BUILD)/test_midi_map: test/midi_map.c $(BUILD)/midi_map.o $(BUILD)/ep_midi_map.o $(CORE_OBJS) $(EP_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/midi_map.c $(BUILD)/midi_map.o $(BUILD)/ep_midi_map.o $(CORE_OBJS) $(EP_OBJS) $(LDFLAGS) -o $@ $(LDLIBS)

$(BUILD)/test_ma_architecture: test/ma_architecture.c $(BUILD)/ma_architecture_score.o $(BUILD)/ma_architecture_render.o $(BUILD)/ma_architecture_cli.o $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/ma_architecture.c $(BUILD)/ma_architecture_score.o $(BUILD)/ma_architecture_render.o $(BUILD)/ma_architecture_cli.o $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_phase: driver/exhibit_phase.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_phase.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_contacts: driver/exhibit_contacts.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_contacts.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_taper: driver/exhibit_taper.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_taper.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_percussion: driver/exhibit_percussion.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_percussion.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_scanner: driver/exhibit_scanner.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_scanner.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_drive: driver/exhibit_drive.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_drive.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_rotary: driver/exhibit_rotary.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_rotary.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_wear: driver/exhibit_wear.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_wear.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_depth: driver/exhibit_depth.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_depth.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_warmth: driver/exhibit_warmth.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_warmth.c $(BUILD)/wav.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_viz: driver/exhibit_viz.c $(BUILD)/viz.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_viz.c $(BUILD)/viz.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ep_voice: driver/exhibit_ep_voice.c $(BUILD)/wav.o $(EP_OBJS) $(CORE_OBJS) src/epiano.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ep_voice.c $(BUILD)/wav.o $(EP_OBJS) $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/render_midi: driver/render_midi.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(BUILD)/midi_map.o $(BUILD)/ep_midi_map.o $(EP_OBJS) $(CORE_OBJS) src/tonewheel.h src/epiano.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/render_midi.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(BUILD)/midi_map.o $(BUILD)/ep_midi_map.o $(EP_OBJS) $(CORE_OBJS) $(LDFLAGS) -o $@ $(LDLIBS)

$(BUILD)/render_ma_architecture: driver/render_ma_architecture.c $(BUILD)/ma_architecture_score.o $(BUILD)/ma_architecture_render.o $(BUILD)/ma_architecture_cli.o $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/render_ma_architecture.c $(BUILD)/ma_architecture_score.o $(BUILD)/ma_architecture_render.o $(BUILD)/ma_architecture_cli.o $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/tw91: driver/main.c $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/midi_map.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/main.c $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/midi_map.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lasound $(LDLIBS)

$(BUILD)/ep73: driver/ep73.c $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/ep_midi_map.o $(EP_OBJS) $(CORE_OBJS) src/epiano.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/ep73.c $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/ep_midi_map.o $(EP_OBJS) $(CORE_OBJS) $(LDFLAGS) -o $@ -lasound $(LDLIBS)

$(BUILD)/patchlab: driver/ma_patchlab.c driver/ma_patch_file.h $(BUILD)/ma_patch_file.o $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/wav.o $(BUILD)/midi.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/ma_patchlab.c $(BUILD)/ma_patch_file.o $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/wav.o $(BUILD)/midi.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lasound -lm $(LDLIBS)

$(BUILD)/fuzz_smf: test/fuzz_smf.c driver/smf.c driver/smf.h | $(BUILD)
	$(FUZZ_CC) -std=c23 -O1 -g -fsanitize=fuzzer,address,undefined test/fuzz_smf.c driver/smf.c -o $@

$(BUILD)/derive_ma_constants: driver/derive_ma_constants.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/derive_ma_constants.c $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_osc: driver/exhibit_ma_osc.c $(BUILD)/ma_voice_source.o $(BUILD)/drive.o src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMA_SOURCE_EVIDENCE driver/exhibit_ma_osc.c $(BUILD)/ma_voice_source.o $(BUILD)/drive.o $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_voice: driver/exhibit_ma_voice.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_voice.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_identity: driver/exhibit_ma_identity.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_identity.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_patches: driver/exhibit_ma_patches.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_patches.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_blues: driver/exhibit_ma_blues.c $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_blues.c $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_blues_panic: driver/exhibit_ma_blues_panic.c $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_blues_panic.c $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_blade_runner_main_titles: driver/exhibit_ma_blade_runner_main_titles.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_blade_runner_main_titles.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_eno_ascent_noir: driver/exhibit_ma_eno_ascent_noir.c $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(BUILD)/smf.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_eno_ascent_noir.c $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(BUILD)/smf.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_nin_hurt_noir: driver/exhibit_ma_nin_hurt_noir.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_nin_hurt_noir.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_blues_ma2_full: driver/exhibit_ma_blues_ma2_full.c $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_blues_ma2_full.c $(BUILD)/ma_dark_lead.o $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_blade_runner_main_titles_ma2_full: driver/exhibit_ma_blade_runner_main_titles_ma2_full.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_blade_runner_main_titles_ma2_full.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_nin_hurt_noir_ma2_full: driver/exhibit_ma_nin_hurt_noir_ma2_full.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_nin_hurt_noir_ma2_full.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_hurt_organ: driver/exhibit_ma_hurt_organ.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_OBJS) $(CORE_OBJS) src/mamutanalog.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(MA_OBJS) $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

.PHONY: audition-ma-hurt-organ
all: $(BUILD)/exhibit_ma_hurt_organ
audition-ma-hurt-organ: $(BUILD)/exhibit_ma_hurt_organ
	./$(BUILD)/exhibit_ma_hurt_organ

$(BUILD)/exhibit_organ_hurt: driver/exhibit_organ_hurt.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(CORE_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

.PHONY: audition-organ-hurt
all: $(BUILD)/exhibit_organ_hurt
audition-organ-hurt: $(BUILD)/exhibit_organ_hurt
	./$(BUILD)/exhibit_organ_hurt

$(BUILD)/exhibit_ma_chopin: driver/exhibit_ma_chopin.c $(BUILD)/wav.o $(BUILD)/smf.o $(MA_RUNTIME_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/wav.o $(BUILD)/smf.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

all: $(BUILD)/exhibit_ma_chopin
.PHONY: check-ma-chopin audition-ma-chopin
check-ma-chopin: $(BUILD)/exhibit_ma_chopin
	./$(BUILD)/exhibit_ma_chopin --check
audition-ma-chopin: $(BUILD)/exhibit_ma_chopin
	./$(BUILD)/exhibit_ma_chopin --render -o $(BUILD)/ma_chopin_op28_4

$(BUILD)/exhibit_ma_raster: driver/exhibit_ma_raster.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_raster.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_bcs: driver/exhibit_ma_bcs.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_bcs.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_output: driver/exhibit_ma_output.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_output.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/bench_ma: driver/bench_ma.c $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/bench_ma.c $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ $(LDLIBS)

.PHONY: bench-ma
bench-ma: $(BUILD)/bench_ma
	$(BUILD)/bench_ma

$(BUILD)/exhibit_ma_character: driver/exhibit_ma_character.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_character.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

.PHONY: audition-ma2-4
audition-ma2-4: $(BUILD)/exhibit_ma_character
	$(BUILD)/exhibit_ma_character

$(BUILD)/exhibit_ma_stereo: driver/exhibit_ma_stereo.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_stereo.c $(BUILD)/wav.o $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

.PHONY: audition-ma2-5
audition-ma2-5: $(BUILD)/exhibit_ma_stereo
	$(BUILD)/exhibit_ma_stereo

check-core-symbols: $(CORE_OBJS) $(EP_OBJS) $(MA_OBJS)
	sh test/check_core_symbols.sh "$(CC)" "$(BUILD)/core-combined.o" $(CORE_OBJS) $(EP_OBJS) $(MA_OBJS)

$(BUILD)/test_ma_character: test/ma_character.c $(MA_RUNTIME_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/ma_character.c $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/test_ma_stereo: test/ma_stereo.c $(MA_RUNTIME_OBJS) src/ma_internal.h src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/ma_stereo.c $(MA_RUNTIME_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

test: $(BUILD)/test $(BUILD)/test_hosted $(BUILD)/test_midi_map $(BUILD)/test_ma_architecture $(BUILD)/test_ma_character $(BUILD)/test_ma_stereo $(TEST_EXTRA)
	$(BUILD)/test
	$(BUILD)/test_hosted
	$(BUILD)/test_midi_map
	$(BUILD)/test_ma_architecture
	$(BUILD)/test_ma_character
	$(BUILD)/test_ma_stereo

test-clang:
	$(MAKE) BUILD=build/clang CC=clang test

sanitize:
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD=build/sanitize CC=clang TEST_EXTRA= CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' test

analyze:
	$(MAKE) BUILD=build/analyze CC=gcc CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fanalyzer' all

fuzz-smf: $(BUILD)/fuzz_smf renders/ep73-d5.mid
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/fuzz_smf \
		-seed_inputs=renders/ep73-d5.mid -runs=1000

# MA0 development tool. Its reviewed output is pasted into the constants
# contract; neither the core build nor make test generates source code.
derive-ma-constants: $(BUILD)/derive_ma_constants
	./$(BUILD)/derive_ma_constants

exhibit-ma1-osc: $(BUILD)/exhibit_ma_osc
	./$(BUILD)/exhibit_ma_osc

# The MA1 listening arm deliberately reuses the longer Blues study.  It does
# not render implicitly: audition-ma-blues is the explicit regeneration step.
exhibit-ma1: $(BUILD)/exhibit_ma_blues $(MA1_LONG_LISTENING_SOURCE)
	test -f $(MA1_LONG_LISTENING_WAV)
	sha256sum $(MA1_LONG_LISTENING_SOURCE) $(MA1_LONG_LISTENING_WAV)

audition-ma1-5: $(BUILD)/exhibit_ma_voice
	./$(BUILD)/exhibit_ma_voice

audition-ma1-6: $(BUILD)/exhibit_ma_identity
	./$(BUILD)/exhibit_ma_identity

audition-ma1-6p: $(BUILD)/exhibit_ma_patches
	./$(BUILD)/exhibit_ma_patches

audition-ma1-6r: $(BUILD)/patchlab $(BUILD)/exhibit_ma_patches
	./$(BUILD)/exhibit_ma_patches
	./$(BUILD)/patchlab --render Tepih $(BUILD)/ma1-6r_patchlab_tepih.wav
	./$(BUILD)/patchlab --render Lead $(BUILD)/ma1-6r_patchlab_lead.wav
	./$(BUILD)/patchlab --render Dubina $(BUILD)/ma1-6r_patchlab_dubina.wav

audition-ma-blues: $(BUILD)/exhibit_ma_blues
	./$(BUILD)/exhibit_ma_blues

audition-ma-blues-panic: $(BUILD)/exhibit_ma_blues_panic
	./$(BUILD)/exhibit_ma_blues_panic

exhibit-ma-blade-runner-main-titles: $(BUILD)/exhibit_ma_blade_runner_main_titles
	./$(BUILD)/exhibit_ma_blade_runner_main_titles

audition-ma-eno-ascent-noir: $(BUILD)/exhibit_ma_eno_ascent_noir
	./$(BUILD)/exhibit_ma_eno_ascent_noir

audition-ma-nin-hurt-noir: $(BUILD)/exhibit_ma_nin_hurt_noir
	./$(BUILD)/exhibit_ma_nin_hurt_noir -d 180 -o $(BUILD)/ma_nin_hurt_noir_180s.wav

audition-ma-blues-ma2-full: $(BUILD)/exhibit_ma_blues_ma2_full
	./$(BUILD)/exhibit_ma_blues_ma2_full

audition-ma-blade-runner-main-titles-ma2-full: $(BUILD)/exhibit_ma_blade_runner_main_titles_ma2_full
	./$(BUILD)/exhibit_ma_blade_runner_main_titles_ma2_full

audition-ma-nin-hurt-noir-ma2-full: $(BUILD)/exhibit_ma_nin_hurt_noir_ma2_full
	./$(BUILD)/exhibit_ma_nin_hurt_noir_ma2_full -d 253 -o $(BUILD)/ma_nin_hurt_noir_ma2_full.wav

audition-ma2-dig: $(BUILD)/exhibit_ma_raster
	./$(BUILD)/exhibit_ma_raster

audition-ma2-bcs: $(BUILD)/exhibit_ma_bcs
	./$(BUILD)/exhibit_ma_bcs

audition-ma-architecture-preview: $(BUILD)/render_ma_architecture
	./$(BUILD)/render_ma_architecture -d 180 -o $(BUILD)/mamut_architecture_180s.wav

audition-ma-architecture: $(BUILD)/render_ma_architecture
	./$(BUILD)/render_ma_architecture -o $(BUILD)/mamut_architecture_v1.wav

audition-ma1-7: $(BUILD)/exhibit_ma_output
	./$(BUILD)/exhibit_ma_output

exhibit: $(BUILD)/exhibit_phase $(BUILD)/exhibit_contacts $(BUILD)/exhibit_taper $(BUILD)/exhibit_percussion $(BUILD)/exhibit_scanner $(BUILD)/exhibit_drive $(BUILD)/exhibit_rotary $(BUILD)/exhibit_wear $(BUILD)/exhibit_depth $(BUILD)/exhibit_ep_voice
	./$(BUILD)/exhibit_phase
	./$(BUILD)/exhibit_contacts
	./$(BUILD)/exhibit_taper
	./$(BUILD)/exhibit_percussion
	./$(BUILD)/exhibit_scanner
	./$(BUILD)/exhibit_drive
	./$(BUILD)/exhibit_rotary
	./$(BUILD)/exhibit_wear
	./$(BUILD)/exhibit_depth
	./$(BUILD)/exhibit_ep_voice

# Dev-side warmth referee (docs/warmth-evidence.md). ngspice is never a
# build dependency: warmth-ref is run by hand when recalibrating, and
# warmth only reads whatever sweep files a prior warmth-ref left behind.
warmth-ref:
	@command -v ngspice >/dev/null || { echo "ngspice not installed; the reference sweep is optional dev tooling"; exit 1; }
	mkdir -p $(BUILD)/spice
	ngspice -b driver/spice/stage1.cir -o $(BUILD)/spice/run.log
	ngspice -b driver/spice/curve.cir -o $(BUILD)/spice/curve.log

# Circuit-true full AO-28 preamplifier sweep (docs/ao28-netlist.md). Reads
# the real schematic values; heavier and slower than stage1. Same optional,
# by-hand posture -- never a build dependency.
ao28-ref:
	@command -v ngspice >/dev/null || { echo "ngspice not installed; the AO-28 sweep is optional dev tooling"; exit 1; }
	mkdir -p $(BUILD)/spice
	ngspice -b driver/spice/ao28.cir -o $(BUILD)/spice/ao28.log

warmth: $(BUILD)/exhibit_warmth
	./$(BUILD)/exhibit_warmth
	@echo
	./$(BUILD)/exhibit_warmth onset-c 0.5
	@echo
	@echo "  spice reference sweep (stage1.cir; source amplitude in volts):"
	@echo "       f0   amp      gain   h2/h1    h3/h1    h4/h1    h5/h1     thd%   vk_mean   vg_mean"
	@for a in 0.02 0.05 0.1 0.15 0.2 0.3 0.5 0.7 1.0 1.5 2.0 3.0; do ./$(BUILD)/exhibit_warmth spice $(BUILD)/spice/lvl_240_$$a.txt 240 $$a; done
	./$(BUILD)/exhibit_warmth spice $(BUILD)/spice/freq_120.txt 120 0.3
	./$(BUILD)/exhibit_warmth spice $(BUILD)/spice/freq_480.txt 480 0.3
	@echo
	./$(BUILD)/exhibit_warmth spice-onset $(BUILD)/spice/onset_240_0.5.txt 240 0.5

# Engine state as pictures (docs/viz-evidence.md). The PNGs are checked-in
# evidence, so they land in docs/viz/, not build/.
viz: $(BUILD)/exhibit_viz
	mkdir -p docs/viz
	./$(BUILD)/exhibit_viz

clean:
	rm -rf $(BUILD)

.PHONY: all test test-clang sanitize analyze fuzz-smf derive-ma-constants exhibit-ma1 exhibit-ma1-osc audition-ma1-5 audition-ma1-6 audition-ma1-6p audition-ma1-6r audition-ma1-7 audition-ma-blues audition-ma-blues-panic exhibit-ma-blade-runner-main-titles audition-ma-eno-ascent-noir audition-ma-nin-hurt-noir audition-ma-blues-ma2-full audition-ma-blade-runner-main-titles-ma2-full audition-ma-nin-hurt-noir-ma2-full audition-ma2-dig audition-ma2-bcs audition-ma-architecture-preview audition-ma-architecture check-core-symbols exhibit warmth warmth-ref ao28-ref viz clean
