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
MA_OBJS := $(BUILD)/ma_voice.o

all: $(BUILD)/test $(BUILD)/test_hosted $(BUILD)/test_midi_map $(BUILD)/exhibit_phase $(BUILD)/exhibit_contacts $(BUILD)/exhibit_taper $(BUILD)/exhibit_percussion $(BUILD)/exhibit_scanner $(BUILD)/exhibit_drive $(BUILD)/exhibit_rotary $(BUILD)/exhibit_wear $(BUILD)/exhibit_depth $(BUILD)/exhibit_warmth $(BUILD)/exhibit_viz $(BUILD)/exhibit_ep_voice $(BUILD)/render_midi $(BUILD)/tw91 $(BUILD)/ep73

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CORE_CFLAGS) -c $< -o $@

$(BUILD)/ep_voice.o $(BUILD)/ep_piano.o: $(BUILD)/%.o: src/%.c src/epiano.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CORE_CFLAGS) -c $< -o $@

$(BUILD)/ma_voice.o: src/ma_voice.c src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CORE_CFLAGS) -c $< -o $@

$(BUILD)/ma_voice_source.o: src/ma_voice.c src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CORE_CFLAGS) -DMA_SOURCE_EVIDENCE -c $< -o $@

$(BUILD)/wav.o: driver/wav.c driver/wav.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/host_parse.o: driver/host_parse.c driver/host_parse.h | $(BUILD)
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

$(BUILD)/test_hosted: test/hosted.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(BUILD)/live_io.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/hosted.c $(BUILD)/wav.o $(BUILD)/smf.o $(BUILD)/host_parse.o $(BUILD)/live_io.o $(LDFLAGS) -o $@ -lasound $(LDLIBS)

$(BUILD)/test_midi_map: test/midi_map.c $(BUILD)/midi_map.o $(BUILD)/ep_midi_map.o $(CORE_OBJS) $(EP_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/midi_map.c $(BUILD)/midi_map.o $(BUILD)/ep_midi_map.o $(CORE_OBJS) $(EP_OBJS) $(LDFLAGS) -o $@ $(LDLIBS)

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

$(BUILD)/tw91: driver/main.c $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/midi_map.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/main.c $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/midi_map.o $(CORE_OBJS) $(LDFLAGS) -o $@ -lasound $(LDLIBS)

$(BUILD)/ep73: driver/ep73.c $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/ep_midi_map.o $(EP_OBJS) $(CORE_OBJS) src/epiano.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/ep73.c $(BUILD)/live_io.o $(BUILD)/host_parse.o $(BUILD)/ep_midi_map.o $(EP_OBJS) $(CORE_OBJS) $(LDFLAGS) -o $@ -lasound $(LDLIBS)

$(BUILD)/fuzz_smf: test/fuzz_smf.c driver/smf.c driver/smf.h | $(BUILD)
	$(FUZZ_CC) -std=c23 -O1 -g -fsanitize=fuzzer,address,undefined test/fuzz_smf.c driver/smf.c -o $@

$(BUILD)/derive_ma_constants: driver/derive_ma_constants.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/derive_ma_constants.c $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_osc: driver/exhibit_ma_osc.c $(BUILD)/ma_voice_source.o src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMA_SOURCE_EVIDENCE driver/exhibit_ma_osc.c $(BUILD)/ma_voice_source.o $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/exhibit_ma_voice: driver/exhibit_ma_voice.c $(BUILD)/wav.o $(MA_OBJS) src/mamutanalog.h src/tonewheel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma_voice.c $(BUILD)/wav.o $(MA_OBJS) $(LDFLAGS) -o $@ -lm $(LDLIBS)

check-core-symbols: $(CORE_OBJS) $(EP_OBJS) $(MA_OBJS)
	sh test/check_core_symbols.sh "$(CC)" "$(BUILD)/core-combined.o" $(CORE_OBJS) $(EP_OBJS) $(MA_OBJS)

test: $(BUILD)/test $(BUILD)/test_hosted $(BUILD)/test_midi_map $(TEST_EXTRA)
	$(BUILD)/test
	$(BUILD)/test_hosted
	$(BUILD)/test_midi_map

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

audition-ma1-5: $(BUILD)/exhibit_ma_voice
	./$(BUILD)/exhibit_ma_voice

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

.PHONY: all test test-clang sanitize analyze fuzz-smf derive-ma-constants exhibit-ma1-osc audition-ma1-5 check-core-symbols exhibit warmth warmth-ref ao28-ref viz clean
