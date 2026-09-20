# libsk — sokol backend (desktop GL first)
#
# This Makefile builds the LIBRARY. Example programs (desktop + wasm) live under
# examples/ with their own Makefile; the targets below delegate there via
# `$(MAKE) -C examples`, and the examples build depends back on this lib.
#
# Targets:
#   make            build static lib (build/desktop/libsk.a)
#   make examples   build example programs        (-> examples/Makefile)
#   make run        build + run the hello example  (-> examples/Makefile)
#   make wasm[-all] build the web examples         (-> examples/Makefile)
#   make serve      static-serve the web build     (-> examples/Makefile)
#   make webcheck   web build + browser smoke check (-> examples/Makefile)
#   make websize    wasm/JS sizes per web example   (-> examples/Makefile)
#   make shaders    regenerate shdc shader headers
#   make example-shaders   repack examples/shaders/*.glsl (custom material shaders)
#   make check      naming / backend-leak guardrails
#   make test       build and run unit tests        (-> tests/Makefile)
#   make verify     build + check + test + smoke: run before calling a change done
#   make deps       install system build deps (ALSA/GL/X11 dev packages)
#   make parity     librl -> libsk API parity report (LIBRL_DIR=../librl)
#   make HEADLESS=1 headless lib (build/headless/libsk.a): no window, GPU or audio
#   make windows    Windows lib and examples, cross-compiled with MinGW (build/windows,
#                   examples/build/windows/*.exe); WINDOWS=1 on any target (with
#                   HEADLESS=1: build/windows-headless). Tests and smoke run under Wine
#                   (tools/wine.sh): make windows-test, make windows-smoke
#   make web        web lib (build/webgl2/libsk.a); BACKEND=webgpu, WEB_THREADS=0
#                   (build/<backend>-nothreads), WEB_DEBUG=1 (-debug); settings in
#                   mk/web.mk
#   make print-web-flags  compile and link flags for a program using the web lib
#
# Build outputs live in one directory per target: build/{desktop,headless,webgl2,
# webgpu,<backend>-nothreads} (library objects + libsk.a) and
# examples/build/{desktop,headless,webgl2,webgpu,...} (programs, web sites).
#   make smoke      run every example headless for a few seconds (-> examples)
#   make clean

UNAME_S := $(shell uname -s)

CC      ?= cc
AR      ?= ar
STD     := -std=gnu11
WARN    := -Wall -Wextra -Wno-unused-parameter
OPT     := -O2 # -g  # use -g for debug build.  TODO:  Flag for release/debug? 
# HEADLESS=1: sokol's dummy GPU backend, no sokol_app window, no audio device, no
# GL/X11/ALSA link dependencies (see src/sk_platform.c). For tests, CI and tools.
HEADLESS ?= 0
# WEB=1 (set by `make web`): Emscripten, BACKEND and WEB_THREADS as in mk/web.mk.
WEB ?= 0
# WINDOWS=1: cross-compile for Windows with MinGW (OpenGL, like Linux: the shaders
# have no HLSL for Direct3D yet). With HEADLESS=1, the headless library for Windows.
WINDOWS ?= 0
MINGW   ?= x86_64-w64-mingw32-
ifeq ($(WEB),1)
  include mk/web.mk
  CC    := $(EMCC)
  AR    := $(EMAR)
  OPT   := $(WEB_OPT)
  DEFS  := $(WASM_CFLAGS_BACKEND)
else ifeq ($(WINDOWS),1)
  CC    := $(MINGW)gcc
  AR    := $(MINGW)ar
  DEFS  := $(if $(filter 1,$(HEADLESS)),-DSK_HEADLESS -DSOKOL_DUMMY_BACKEND,-DSOKOL_GLCORE)
else ifeq ($(HEADLESS),1)
  DEFS  := -DSK_HEADLESS -DSOKOL_DUMMY_BACKEND
else
  DEFS  := -DSOKOL_GLCORE
endif
# Our headers use -I (full warnings); vendored single-header libs use -isystem so
# their warnings (stb/fontstash/dr/cgltf/sokol) don't drown out ours.
INCS    := -Iinclude -Isrc
INCS    += -isystem deps/sokol -isystem deps/sokol_utils -isystem deps/stb \
           -isystem deps/fontstash -isystem deps/dr -isystem deps/cgltf
CFLAGS  := $(STD) $(WARN) $(OPT) $(DEFS) $(INCS)

ifeq ($(WEB),1)
  TARGET := $(WEB_DIR)
  DEPS_CHECK :=
else ifeq ($(WINDOWS),1)
  TARGET := windows$(if $(filter 1,$(HEADLESS)),-headless)
  DEPS_CHECK :=
else ifeq ($(HEADLESS),1)
  TARGET := headless
  DEPS_CHECK :=
else
  TARGET := desktop
  DEPS_CHECK := deps-check
endif
# BENCH_DEFS: extra -D flags for benchmark builds (tools/bench), e.g. larger
# starting sokol_gl budgets. They get their own build directory (<target>-bench) so the normal
# libraries, tests and examples never pick them up.
BENCH_DEFS ?=
ifneq ($(strip $(BENCH_DEFS)),)
  TARGET := $(TARGET)-bench
  CFLAGS += $(BENCH_DEFS)
endif
BUILD   := build/$(TARGET)
LIB     := $(BUILD)/libsk.a

SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SRCS))

.PHONY: all examples run clean check test smoke verify wasm wasm-all serve webcheck shaders deps deps-check parity loadbench spritebench shadowbench brdf-lut example-shaders \
        web print-web-flags windows windows-test windows-smoke FORCE

all: $(LIB)

$(BUILD)/obj:
	mkdir -p $(BUILD)/obj

# Rebuild everything when the compile flags change (OPT, WEB_THREADS, ...).
FLAGS_STAMP := $(BUILD)/obj/flags
$(FLAGS_STAMP): FORCE | $(BUILD)/obj
	@echo '$(CC) $(CFLAGS)' | cmp -s - $@ || echo '$(CC) $(CFLAGS)' > $@

# -MD (not -MMD): also track the vendored headers in deps/, which are included
# with -isystem, so updating one rebuilds what uses it.
$(BUILD)/obj/%.o: src/%.c $(FLAGS_STAMP) | $(BUILD)/obj $(DEPS_CHECK)
	$(CC) $(CFLAGS) -MD -MP -c $< -o $@

-include $(OBJS:.o=.d)

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	@echo "built $@"

# --- web library (Emscripten) -----------------------------------------------
# build/<backend>/libsk.a. A program compiles and links against it with matching
# settings; `make print-web-flags` prints them (the public headers need no backend
# defines, but threads must match).
web:
	@$(MAKE) --no-print-directory -j$(NPROC) all WEB=1

print-web-flags:
	@$(MAKE) --no-print-directory -s WEB=1 print-web-flags-inner

print-web-flags-inner:
	@echo "lib:     build/$(WEB_DIR)/libsk.a"
	@echo "cflags:  -Iinclude $(WASM_THREADS)"
	@echo "ldflags: $(WASM_LINK)"

FORCE:

# --- system dependencies -----------------------------------------------------
# sokol needs the platform's ALSA/GL/X11 dev packages (not vendorable). Check up
# front so a missing one fails with the package to install, not a compiler error
# deep inside a sokol header. Order-only prereq above: never forces a rebuild.
deps-check:
	@tools/deps.sh check

deps:
	@tools/deps.sh install

# --- examples (delegated to examples/Makefile) ------------------------------
# Those targets depend on the lib and recurse back here to keep it current, so
# `make examples` / `make wasm` build libsk first if needed. Command-line vars
# (BACKEND=, WASM_EXAMPLE=) propagate to the sub-make automatically.
examples:
	@$(MAKE) -C examples
run wasm wasm-all websize serve serve-tls spritebench-web shadowbench-web loadbench-web webcheck webstart smoke:
	@$(MAKE) -C examples $@

# --- tests (delegated to tests/Makefile) -------------------------------------
test:
	@$(MAKE) --no-print-directory -C tests test

# Everything to run before calling a change done (AGENTS.md): library, examples,
# guardrails, unit tests and the headless smoke test. Add `make webcheck` (and
# BACKEND=webgpu) when touching rendering, assets or web code.
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
verify:
	@$(MAKE) --no-print-directory -j$(NPROC) all
	@$(MAKE) --no-print-directory -C examples -j$(NPROC)
	@$(MAKE) --no-print-directory check
	@$(MAKE) --no-print-directory test
	@$(MAKE) --no-print-directory smoke
	@if command -v $(MINGW)gcc >/dev/null 2>&1; then \
	    echo "verify: Windows build (MinGW)"; \
	    $(MAKE) --no-print-directory -s windows || exit 1; \
	fi
	@echo "verify: PASS"

# --- Windows (cross-compiled with MinGW) ----------------------------------------
# The library and examples as .exe files (examples/build/windows). The unit tests and
# the headless smoke run go through Wine (tools/wine.sh: wine64/wine, or Steam's
# Proton). make verify builds `windows` when MinGW is installed, so Windows code keeps
# compiling; running it needs Wine, and its window, GL and audio need real Windows.
windows:
	@$(MAKE) --no-print-directory -j$(NPROC) all WINDOWS=1
	@$(MAKE) --no-print-directory -C examples -j$(NPROC) WINDOWS=1

windows-test:
	@$(MAKE) --no-print-directory -C tests test WINDOWS=1

windows-smoke:
	@$(MAKE) --no-print-directory -C examples smoke WINDOWS=1

# --- loading benchmark (tools/bench) -----------------------------------------
# Worst frame while loading Sponza and FlightHelmet, background vs synchronous.
# Downloads the models on first use. Headless by default (CPU work only);
# DESKTOP=1 uses the desktop build, with real GPU uploads, in a window.
loadbench:
	@tools/bench/fetch_assets.sh
ifeq ($(KTX),1)
	@for m in Sponza/Sponza FlightHelmet/FlightHelmet; do \
	    [ -f examples/assets/bench/$$m.ktx.gltf ] || tools/compress_textures.sh --gltf examples/assets/bench/$$m.gltf \
	        >/dev/null || exit 1; \
	done
endif
ifeq ($(DESKTOP),1)
	@$(MAKE) --no-print-directory -j$(NPROC) all
	@$(CC) $(STD) -O2 -Iinclude tools/bench/loadbench.c build/desktop/libsk.a \
	    $$($(MAKE) --no-print-directory -s -C examples print-ldlibs) -o build/desktop/loadbench
	@SK_LOADBENCH_KTX=$(KTX) build/desktop/loadbench 2>/dev/null
else
	@$(MAKE) --no-print-directory -j$(NPROC) all HEADLESS=1
	@$(CC) $(STD) -O2 -Iinclude tools/bench/loadbench.c build/headless/libsk.a -ldl -lm -lpthread \
	    -o build/headless/loadbench
	@build/headless/loadbench 2>/dev/null
endif

# --- generated data -----------------------------------------------------------
# The split-sum BRDF table image-based lighting reads, baked into
# src/data/sk_brdf_lut.h by the library's own function (tools/gen_brdf_lut.c).
brdf-lut:
	@$(MAKE) --no-print-directory -j$(NPROC) all HEADLESS=1
	@$(CC) $(STD) -O2 -Iinclude -Isrc -isystem deps/sokol tools/gen_brdf_lut.c build/headless/libsk.a \
	    -ldl -lm -lpthread -o build/headless/gen_brdf_lut
	@build/headless/gen_brdf_lut src/data/sk_brdf_lut.h

# --- sprite benchmark (tools/bench) ------------------------------------------
# Sprite-heavy scenes (a grid, a perspective field with mixed facings, 3D and 2D
# particles): frame time and CPU split into update / scene / submit, with sokol_gl's
# vertex and command use. Headless by default (CPU only); DESKTOP=1 uses the desktop
# build with vsync off. `make spritebench-web` builds it as a web page (examples/Makefile).
SPRITEBENCH_INCS := -Iinclude -isystem deps/sokol
spritebench:
ifeq ($(DESKTOP),1)
	@$(MAKE) --no-print-directory -j$(NPROC) all
	@libs="$$($(MAKE) --no-print-directory -s -C examples print-ldlibs)"; \
	$(CC) $(STD) -O2 -DSOKOL_GLCORE $(SPRITEBENCH_INCS) tools/bench/spritebench.c build/desktop/libsk.a \
	    $$libs -lm -o build/desktop/spritebench
	@build/desktop/spritebench 2>/dev/null
else
	@$(MAKE) --no-print-directory -j$(NPROC) all HEADLESS=1
	@$(CC) $(STD) -O2 -DSK_HEADLESS -DSOKOL_DUMMY_BACKEND $(SPRITEBENCH_INCS) tools/bench/spritebench.c \
	    build/headless/libsk.a -ldl -lm -lpthread -o build/headless/spritebench
	@build/headless/spritebench 2>/dev/null
endif

# --- shadow benchmark (tools/bench) ------------------------------------------
# What a casting light costs a frame: the same scene with no shadows, one light at two
# map sizes, two lights, and one where nothing receives. Headless by default (CPU
# only, and no GPU at all); DESKTOP=1 uses the desktop build with vsync off.
# `make shadowbench-web` builds it as a web page (examples/Makefile).
shadowbench:
ifeq ($(DESKTOP),1)
	@$(MAKE) --no-print-directory -j$(NPROC) all
	@libs="$$($(MAKE) --no-print-directory -s -C examples print-ldlibs)"; \
	$(CC) $(STD) -O2 -DSOKOL_GLCORE $(SPRITEBENCH_INCS) tools/bench/shadowbench.c build/desktop/libsk.a \
	    $$libs -lm -o build/desktop/shadowbench
	@build/desktop/shadowbench 2>/dev/null
else
	@$(MAKE) --no-print-directory -j$(NPROC) all HEADLESS=1
	@$(CC) $(STD) -O2 -DSK_HEADLESS -DSOKOL_DUMMY_BACKEND $(SPRITEBENCH_INCS) tools/bench/shadowbench.c \
	    build/headless/libsk.a -ldl -lm -lpthread -o build/headless/shadowbench
	@build/headless/shadowbench 2>/dev/null
endif

# --- shaders (sokol-shdc) ---------------------------------------------------
# Regenerates the committed *.glsl.h (GL core, WebGL2, WebGPU) from annotated
# GLSL, each backend behind #if defined(SOKOL_<backend>) (--ifdef) so a build only
# carries its own; include them through src/internal/sk_shaders.h. The binary is
# gitignored; fetch it once:
#   curl -sSL -o tools/sokol-shdc \
#     https://raw.githubusercontent.com/floooh/sokol-tools-bin/master/bin/linux/sokol-shdc \
#     && chmod +x tools/sokol-shdc
SHDC       := tools/sokol-shdc
SHDC_SLANG := glsl410:glsl300es:wgsl
SHADERS    := src/shaders/sk_model.glsl src/shaders/sk_sprite.glsl src/shaders/sk_depth.glsl

shaders:
	@for s in $(SHADERS); do \
	    echo "  shdc: $$s"; \
	    $(SHDC) -i $$s -o $$s.h -l $(SHDC_SLANG) --ifdef || exit 1; \
	done

# The custom material shaders of examples/shaders.c (examples/shaders/*.glsl), packed
# by tools/shaderpack.py into the committed examples/assets/shaders/*.skshader.
# Rebuild them after changing one, or shaders/sk.glsl.
EXAMPLE_SHADERS := $(wildcard examples/shaders/*.glsl)
example-shaders:
	@for s in $(EXAMPLE_SHADERS); do \
	    python3 tools/shaderpack.py $$s -o examples/assets/shaders/$$(basename $${s%.glsl}).skshader || exit 1; \
	done

# Enforce project invariants: no backend (sokol) leakage into the public
# surface, and the naming conventions in AGENTS.md.
check:
	@tools/check_no_backend_leak.sh
	@tools/check_naming.sh
	@tools/check_modules.sh

# librl -> libsk API parity: every librl function is matched, mapped as ported /
# dropped / todo in tools/parity.map, or the report fails. PARITY_FLAGS=--strict
# also fails on remaining todos. Needs a librl checkout (LIBRL_DIR, default ../librl).
LIBRL_DIR ?= ../librl
parity:
	@LIBRL_DIR=$(LIBRL_DIR) tools/parity.sh $(PARITY_FLAGS)

clean:
	rm -rf build
	@$(MAKE) -C examples clean
	@$(MAKE) -C tests clean
