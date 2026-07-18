CC     ?= gcc
BUILD  := build
WARN   := -Wall -Wextra -Wpedantic
CFLAGS := -std=c23 -O2 $(WARN) -ffp-contract=off

# The core stays freestanding-clean; drivers and tests are hosted.
CORE_CFLAGS := $(CFLAGS) -ffreestanding

CORE_OBJS := $(BUILD)/generator.o $(BUILD)/midi.o $(BUILD)/organ.o $(BUILD)/scanner.o $(BUILD)/drive.o $(BUILD)/rotary.o

all: $(BUILD)/test $(BUILD)/exhibit_phase $(BUILD)/exhibit_contacts $(BUILD)/exhibit_taper $(BUILD)/exhibit_percussion $(BUILD)/exhibit_scanner $(BUILD)/exhibit_drive $(BUILD)/exhibit_rotary $(BUILD)/render_midi $(BUILD)/tw91

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c src/tonewheel.h | $(BUILD)
	$(CC) $(CORE_CFLAGS) -c $< -o $@

$(BUILD)/wav.o: driver/wav.c driver/wav.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test: test/test.c $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) test/test.c $(CORE_OBJS) -o $@ -lm

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

$(BUILD)/render_midi: driver/render_midi.c $(BUILD)/wav.o $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/render_midi.c $(BUILD)/wav.o $(CORE_OBJS) -o $@

$(BUILD)/tw91: driver/main.c $(CORE_OBJS) src/tonewheel.h | $(BUILD)
	$(CC) $(CFLAGS) driver/main.c $(CORE_OBJS) -o $@ -lasound

test: $(BUILD)/test
	./$(BUILD)/test

exhibit: $(BUILD)/exhibit_phase $(BUILD)/exhibit_contacts $(BUILD)/exhibit_taper $(BUILD)/exhibit_percussion $(BUILD)/exhibit_scanner $(BUILD)/exhibit_drive $(BUILD)/exhibit_rotary
	./$(BUILD)/exhibit_phase
	./$(BUILD)/exhibit_contacts
	./$(BUILD)/exhibit_taper
	./$(BUILD)/exhibit_percussion
	./$(BUILD)/exhibit_scanner
	./$(BUILD)/exhibit_drive
	./$(BUILD)/exhibit_rotary

clean:
	rm -rf $(BUILD)

.PHONY: all test exhibit clean
