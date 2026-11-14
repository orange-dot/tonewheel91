# Patchbench (docs/patchbench-plan.md): a sound-design desk over the Mamut
# Analog voice.  Included from the top-level Makefile after workbench.mk,
# whose UI flags, theme and Nuklear unit it reuses.

PB_BUILD := $(BUILD)/pb-obj
PB_CORE_MODULES := pb_fields pb_derive pb_seq pb_undo pb_state pb_fft pb_probe pb_engine pb_bounce pb_host
PB_UI_MODULES := pb_widgets pb_modules pb_views pb_rack
PB_CORE_OBJS := $(PB_CORE_MODULES:%=$(PB_BUILD)/%.o)
PB_RT_OBJ := $(PB_BUILD)/pb_rt.o
PB_HOST_OBJS := $(BUILD)/ma_patch_file.o $(BUILD)/host_parse.o $(BUILD)/wav.o $(BUILD)/live_io.o $(WB_BUILD)/wb_hash.o
PB_LINK := $(PB_CORE_OBJS) $(PB_RT_OBJ) $(PB_HOST_OBJS) $(MA_RUNTIME_OBJS)

$(PB_BUILD):
	mkdir -p $@

$(PB_CORE_OBJS) $(PB_RT_OBJ): $(PB_BUILD)/%.o: workbench/pb/%.c | $(PB_BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(PB_UI_MODULES:%=$(PB_BUILD)/%.o): $(PB_BUILD)/%.o: workbench/pb/%.c | $(PB_BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WB_UI_CFLAGS) -DWB_REPO='"$(CURDIR)"' -c $< -o $@

$(BUILD)/patchbench: workbench/pb/pb_app.c $(PB_UI_MODULES:%=$(PB_BUILD)/%.o) $(PB_LINK) \
    $(WB_BUILD)/wb_theme.o $(WB_BUILD)/wb_layout.o $(WB_BUILD)/wb_nk_impl.o $(WB_BUILD)/wb_patch.o \
    $(WB_BUILD)/wb_session.o $(WB_BUILD)/wb_format.o | $(BUILD)
	@test -n "$(SDL3_LIBS)" || { echo "SDL3 development files not found: install SDL3-devel (pkg-config sdl3)"; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WB_UI_CFLAGS) -DWB_REPO='"$(CURDIR)"' workbench/pb/pb_app.c \
	    $(PB_UI_MODULES:%=$(PB_BUILD)/%.o) $(WB_BUILD)/wb_theme.o $(WB_BUILD)/wb_layout.o $(WB_BUILD)/wb_nk_impl.o \
	    $(WB_BUILD)/wb_patch.o $(WB_BUILD)/wb_session.o $(WB_BUILD)/wb_format.o \
	    $(PB_LINK) $(LDFLAGS) -o $@ $(SDL3_LIBS) -lasound -lpthread -lm $(LDLIBS)

$(BUILD)/test_patchbench: workbench/pb/test/pb_test.c $(PB_LINK) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(PB_LINK) $(LDFLAGS) -o $@ -lasound -lpthread -lm $(LDLIBS)

$(BUILD)/test_patchbench_rt: workbench/pb/test/pb_rt_test.c $(PB_LINK) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(PB_LINK) $(LDFLAGS) -o $@ -lasound -lpthread -lm $(LDLIBS)

patchbench: $(BUILD)/patchbench

# The audio thread's object may reference only the engines and pure libm.
check-patchbench-symbols: $(PB_RT_OBJ) $(PB_BUILD)/pb_seq.o
	PB_AUDIT_LINK="$(CC) $(AUDIT_LDFLAGS)" sh workbench/pb/test/check_rt_symbols.sh $(PB_RT_OBJ) $(PB_BUILD)/pb_seq.o

test-patchbench: $(BUILD)/test_patchbench $(BUILD)/test_patchbench_rt $(if $(TEST_EXTRA),check-patchbench-symbols)
	$(BUILD)/test_patchbench
	$(BUILD)/test_patchbench_rt

# Headless UI smoke: every module, both themes, 1920x1080 and 2560x1080,
# scripted drags; the patch file must not change without input.
smoke-patchbench: $(BUILD)/patchbench
	SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software WB_HOME=$(BUILD)/pb-smoke-home \
	    $(BUILD)/patchbench --smoke patches/mamutanalog/prizma.mapatch

test: test-patchbench

.PHONY: patchbench test-patchbench check-patchbench-symbols smoke-patchbench

# Operator check (opens the sound card, silent): set_patch cost and a live
# run with mailbox traffic and a pool swap.  PB_LIVE_SECONDS, WB_AUDIO_DEVICE.
$(BUILD)/pb_live_check: workbench/pb/test/pb_live_check.c $(PB_LINK) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(PB_LINK) $(LDFLAGS) -o $@ -lasound -lpthread -lm $(LDLIBS)
check-patchbench-live: $(BUILD)/pb_live_check
	$(BUILD)/pb_live_check $(or $(PB_LIVE_SECONDS),20) $(or $(WB_AUDIO_DEVICE),default)
.PHONY: check-patchbench-live

# Live-path soak (operator check, silent): DSP and wake-up percentiles, xruns,
# migrations and CPU frequency. PB_SOAK_SECONDS, WB_AUDIO_DEVICE, and the
# PB_SOAK_* / PB_LIVE_* switches of workbench/pb/test/pb_live_soak.c.
$(BUILD)/pb_live_soak: workbench/pb/test/pb_live_soak.c $(PB_LINK) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(PB_LINK) $(LDFLAGS) -o $@ -lasound -lpthread -lm $(LDLIBS)
check-patchbench-soak: $(BUILD)/pb_live_soak
	$(BUILD)/pb_live_soak $(or $(PB_SOAK_SECONDS),60) $(or $(WB_AUDIO_DEVICE),default)
.PHONY: check-patchbench-soak
