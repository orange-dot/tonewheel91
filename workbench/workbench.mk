# Workbench (docs/workbench-plan.md). Included from the top-level Makefile.
# desk/ is hosted C with no UI and no ALSA; cli/ and ui/ both build on it.

WB_BUILD := $(BUILD)/wb-obj
WB_DESK_MODULES := wb_hash wb_session wb_format wb_undo wb_root wb_render wb_ledger wb_smf wb_orch wb_import wb_analysis wb_engine wb_cache wb_take wb_patch
WB_DESK_OBJS := $(WB_DESK_MODULES:%=$(WB_BUILD)/%.o)
WB_DESK_HEADERS := $(wildcard workbench/desk/*.h)
WB_HOST_OBJS := $(BUILD)/ma_patch_file.o $(BUILD)/host_parse.o $(BUILD)/smf.o $(BUILD)/wav.o \
    $(BUILD)/smf_schedule.o $(BUILD)/midi_map.o $(BUILD)/ep_midi_map.o
# The desk renders through the Mamut core from W2 on; link it from the start.
WB_LINK := $(WB_DESK_OBJS) $(WB_HOST_OBJS) $(MA_RUNTIME_OBJS) $(CORE_OBJS:$(BUILD)/drive.o=) $(EP_OBJS)

$(WB_BUILD):
	mkdir -p $@

$(WB_DESK_OBJS): $(WB_BUILD)/%.o: workbench/desk/%.c | $(WB_BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DWB_REPO='"$(CURDIR)"' -c $< -o $@

$(BUILD)/wb: workbench/cli/wb_main.c $(WB_LINK) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) workbench/cli/wb_main.c $(WB_LINK) $(LDFLAGS) -o $@ -lm $(LDLIBS)

$(BUILD)/test_workbench: workbench/test/wb_test.c workbench/test/wb_test_w2w3.c $(WB_LINK) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) workbench/test/wb_test.c $(WB_LINK) $(LDFLAGS) -o $@ -lm $(LDLIBS)

WB_FUZZ_MODULES := wb_hash wb_session wb_format
$(BUILD)/fuzz_mws: workbench/test/fuzz_mws.c $(WB_FUZZ_MODULES:%=workbench/desk/%.c) driver/ma_patch_file.c driver/host_parse.c | $(BUILD)
	$(FUZZ_CC) -std=c23 -O1 -g -fsanitize=fuzzer,address,undefined workbench/test/fuzz_mws.c \
	    $(WB_FUZZ_MODULES:%=workbench/desk/%.c) driver/ma_patch_file.c driver/host_parse.c -lm -o $@

wb: $(BUILD)/wb

# ---- live audio (W7) -------------------------------------------------------
WB_AUDIO_OBJS := $(WB_BUILD)/wb_live.o $(WB_BUILD)/wb_live_rt.o
WB_AUDIO_HEADERS := $(wildcard workbench/audio/*.h)

$(WB_AUDIO_OBJS): $(WB_BUILD)/%.o: workbench/audio/%.c | $(WB_BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/test_live: workbench/test/live_test.c $(WB_AUDIO_OBJS) $(BUILD)/live_io.o $(WB_LINK) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) workbench/test/live_test.c $(WB_AUDIO_OBJS) $(BUILD)/live_io.o $(WB_LINK) \
	    $(LDFLAGS) -o $@ $(SDL3_LIBS) -lasound -lpthread -lm $(LDLIBS)

# The Workbench joins `make test` (plan §2.2: after W2).
test: test-workbench

check-workbench-symbols: $(WB_BUILD)/wb_live_rt.o
	sh workbench/test/check_rt_symbols.sh $(WB_BUILD)/wb_live_rt.o

# Operator parity tools: the Hurt orchestra exhibit's patches as .mapatch,
# and the exhibit itself, both built from this tree (plan §9).
WB_EXHIBIT_LINK := driver/ma3_show.c $(BUILD)/ma_dark_lead.o $(BUILD)/smf.o $(BUILD)/wav.o \
    $(BUILD)/host_parse.o $(MA_RUNTIME_OBJS)
$(BUILD)/export_hurt_orch: workbench/tools/export_hurt_orch.c driver/exhibit_ma3_hurt_orch.c $(BUILD)/ma_patch_file.o $(WB_EXHIBIT_LINK) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-function workbench/tools/export_hurt_orch.c \
	    $(BUILD)/ma_patch_file.o $(WB_EXHIBIT_LINK) $(LDFLAGS) -o $@ -lm $(LDLIBS)
$(BUILD)/wb-parity/exhibit_ma3_hurt_orch: driver/exhibit_ma3_hurt_orch.c $(WB_EXHIBIT_LINK) | $(BUILD)
	mkdir -p $(BUILD)/wb-parity
	$(CC) $(CPPFLAGS) $(CFLAGS) driver/exhibit_ma3_hurt_orch.c $(WB_EXHIBIT_LINK) $(LDFLAGS) -o $@ -lm $(LDLIBS)

parity-hurt: $(BUILD)/wb $(BUILD)/export_hurt_orch $(BUILD)/wb-parity/exhibit_ma3_hurt_orch
	BUILD=$(BUILD) sh workbench/test/parity_hurt.sh

parity-hurt-full: $(BUILD)/wb $(BUILD)/export_hurt_orch $(BUILD)/wb-parity/exhibit_ma3_hurt_orch
	BUILD=$(BUILD) systemd-inhibit --what=sleep:idle --who=parity-hurt --why="Hurt full parity" \
	    sh workbench/test/parity_hurt.sh --full

# The CLI test drives a scratch WB_HOME; it never touches the real root.
# The audio-thread audit runs with the core audit (TEST_EXTRA); sanitizer
# builds clear both, since their instrumentation adds runtime references.
test-workbench: $(BUILD)/test_workbench $(BUILD)/wb $(BUILD)/test_live $(if $(TEST_EXTRA),check-workbench-symbols)
	$(BUILD)/test_workbench
	$(BUILD)/test_live
	WB=$(BUILD)/wb sh workbench/test/wb_cli.sh

fuzz-mws: $(BUILD)/fuzz_mws
	mkdir -p $(BUILD)/fuzz-mws-corpus
	cp workbench/test/fixtures/*.mws $(BUILD)/fuzz-mws-corpus/
	ASAN_OPTIONS=detect_leaks=1 $(BUILD)/fuzz_mws -max_total_time=$${FUZZ_SECONDS:-60} \
	    -max_len=16384 $(BUILD)/fuzz-mws-corpus

# ---- UI (W0): SDL3 + Nuklear --------------------------------------------
# SDL3 comes from the system (SDL3-devel); Nuklear is vendored and unmodified
# under workbench/third_party, included as system headers so its own
# warnings stay out of the Workbench build.
SDL3_CFLAGS ?= $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL3_LIBS ?= $(shell pkg-config --libs sdl3 2>/dev/null)
WB_UI_CFLAGS := $(SDL3_CFLAGS) -isystem workbench/third_party
WB_UI_MODULES := wb_layout wb_theme wb_jobs wb_views
WB_UI_OBJS := $(WB_UI_MODULES:%=$(WB_BUILD)/%.o) $(WB_BUILD)/wb_nk_impl.o
WB_UI_HEADERS := $(wildcard workbench/ui/*.h) $(wildcard workbench/third_party/*.h)

$(WB_UI_MODULES:%=$(WB_BUILD)/%.o): $(WB_BUILD)/%.o: workbench/ui/%.c | $(WB_BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WB_UI_CFLAGS) -c $< -o $@

# Vendored implementation unit: optimized, warnings off.
$(WB_BUILD)/wb_nk_impl.o: workbench/ui/wb_nk_impl.c | $(WB_BUILD)
	$(CC) $(CPPFLAGS) -std=c23 -O2 -w $(WB_UI_CFLAGS) -c $< -o $@

$(BUILD)/workbench: workbench/ui/wb_app.c $(WB_UI_OBJS) $(WB_AUDIO_OBJS) $(BUILD)/live_io.o $(WB_LINK) $(BUILD)/wb | $(BUILD)
	@test -n "$(SDL3_LIBS)" || { echo "SDL3 development files not found: install SDL3-devel (pkg-config sdl3)"; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WB_UI_CFLAGS) workbench/ui/wb_app.c $(WB_UI_OBJS) $(WB_AUDIO_OBJS) $(BUILD)/live_io.o \
	    $(WB_LINK) $(LDFLAGS) -o $@ $(SDL3_LIBS) -lasound -lpthread -lm $(LDLIBS)

workbench: $(BUILD)/workbench

# Headless UI smoke: every preset, tab, theme and console state drawn once
# with the dummy video driver, on the Hurt v2 fixture session.
smoke-workbench: $(BUILD)/workbench
	SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software $(BUILD)/workbench --smoke workbench/test/fixtures/hurt-v2.mws

.PHONY: check-workbench-symbols parity-hurt parity-hurt-full wb workbench test-workbench smoke-workbench fuzz-mws
