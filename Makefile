CC     ?= gcc
BUILD  := build
WARN   := -Wall -Wextra -Wpedantic
CFLAGS := -std=c23 -O2 $(WARN) -ffp-contract=off

# The core stays freestanding-clean; drivers and tests are hosted.
CORE_CFLAGS := $(CFLAGS) -ffreestanding

CORE_OBJS := $(BUILD)/generator.o $(BUILD)/midi.o $(BUILD)/organ.o $(BUILD)/scanner.o $(BUILD)/drive.o $(BUILD)/rotary.o

# The electric-piano line (docs/piano-backlog.md part 1). Sibling core, not
# a fork: ep_voice.c includes epiano.h, which includes tonewheel.h for the
# shared kernels. No organ object depends on it.
EP_OBJS := $(BUILD)/ep_voice.o $(BUILD)/ep_piano.o

all: $(BUILD)/test $(BUILD)/exhibit_phase $(BUILD)/exhibit_contacts $(BUILD)/exhibit_taper $(BUILD)/exhibit_percussion $(BUILD)/exhibit_scanner $(BUILD)/exhibit_drive $(BUILD)/exhibit_rotary $(BUILD)/exhibit_wear $(BUILD)/exhibit_depth $(BUILD)/exhibit_warmth $(BUILD)/exhibit_viz $(BUILD)/exhibit_ep_voice $(BUILD)/render_midi $(BUILD)/tw91 $(BUILD)/ep73

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c src/tonewheel.h | $(BUILD)
	$(CC) $(CORE_CFLAGS) -c $< -o $@

$(BUILD)/ep_voice.o $(BUILD)/ep_piano.o: $(BUILD)/%.o: src/%.c src/epiano.h src/tonewheel.h | $(BUILD)
	$(CC) $(CORE_CFLAGS) -c $< -o $@

$(BUILD)/wav.o: driver/wav.c driver/wav.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/viz.o: driver/viz.c driver/viz.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test: test/test.c $(CORE_OBJS) $(EP_OBJS) src/tonewheel.h src/epiano.h | $(BUILD)
	$(CC) $(CFLAGS) test/test.c $(CORE_OBJS) $(EP_OBJS) -o $@ -lm

$(BUILD)/exhibit_phase: driver/exhibit_phase.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_phase.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_contacts: driver/exhibit_contacts.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_contacts.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_taper: driver/exhibit_taper.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_taper.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_percussion: driver/exhibit_percussion.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_percussion.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_scanner: driver/exhibit_scanner.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_scanner.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_drive: driver/exhibit_drive.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_drive.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_rotary: driver/exhibit_rotary.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_rotary.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_wear: driver/exhibit_wear.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_wear.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_depth: driver/exhibit_depth.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_depth.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_warmth: driver/exhibit_warmth.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_warmth.c $(BUILD)/wav.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_viz: driver/exhibit_viz.c $(BUILD)/viz.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_viz.c $(BUILD)/viz.o $(CORE_OBJS) -o $@ -lm

$(BUILD)/exhibit_ep_voice: driver/exhibit_ep_voice.c $(BUILD)/wav.o $(EP_OBJS) $(CORE_OBJS) src/epiano.h src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/exhibit_ep_voice.c $(BUILD)/wav.o $(EP_OBJS) $(CORE_OBJS) -o $@ -lm

$(BUILD)/render_midi: driver/render_midi.c $(BUILD)/wav.o $(EP_OBJS) $(CORE_OBJS) src/tonewheel.h src/epiano.h | $(BUILD)
	$(CC) $(CFLAGS) driver/render_midi.c $(BUILD)/wav.o $(EP_OBJS) $(CORE_OBJS) -o $@

$(BUILD)/tw91: driver/main.c $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/main.c $(CORE_OBJS) -o $@ -lasound

$(BUILD)/ep73: driver/ep73.c $(EP_OBJS) $(CORE_OBJS) src/epiano.h src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/ep73.c $(EP_OBJS) $(CORE_OBJS) -o $@ -lasound

test: $(BUILD)/test
	./$(BUILD)/test

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

.PHONY: all test exhibit warmth warmth-ref ao28-ref viz clean
