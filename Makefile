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
#   make shaders    regenerate shdc shader headers
#   make check      naming / backend-leak guardrails
#   make test       build and run unit tests        (-> tests/Makefile)
#   make verify     build + check + test + smoke: run before calling a change done
#   make deps       install system build deps (ALSA/GL/X11 dev packages)
#   make parity     librl -> libsk API parity report (LIBRL_DIR=../librl)
#   make HEADLESS=1 headless lib (build/headless/libsk.a): no window, GPU or audio
#
# Build outputs live in one directory per target: build/desktop, build/headless
# (library objects + libsk.a) and examples/build/{desktop,headless,webgl2,webgpu}.
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
ifeq ($(HEADLESS),1)
  DEFS  := -DSK_HEADLESS -DSOKOL_DUMMY_BACKEND
else
  DEFS  := -DSOKOL_GLCORE
endif
# Our headers use -I (full warnings); vendored single-header libs use -isystem so
# their warnings (stb/fontstash/dr/cgltf/sokol) don't drown out ours.
INCS    := -Iinclude -Isrc
INCS    += -isystem deps/sokol -isystem deps/stb -isystem deps/fontstash \
           -isystem deps/dr -isystem deps/cgltf
CFLAGS  := $(STD) $(WARN) $(OPT) $(DEFS) $(INCS)

ifeq ($(HEADLESS),1)
  TARGET := headless
  DEPS_CHECK :=
else
  TARGET := desktop
  DEPS_CHECK := deps-check
endif
BUILD   := build/$(TARGET)
LIB     := $(BUILD)/libsk.a

SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SRCS))

.PHONY: all examples run clean check test smoke verify wasm wasm-all serve webcheck shaders deps deps-check parity

all: $(LIB)

$(BUILD)/obj:
	mkdir -p $(BUILD)/obj

$(BUILD)/obj/%.o: src/%.c | $(BUILD)/obj $(DEPS_CHECK)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	@echo "built $@"

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
run wasm wasm-all serve webcheck smoke:
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
	@echo "verify: PASS"

# --- shaders (sokol-shdc) ---------------------------------------------------
# Regenerates the committed *.glsl.h (GL core, WebGL2, WebGPU) from annotated
# GLSL. The binary is gitignored; fetch it once:
#   curl -sSL -o tools/sokol-shdc \
#     https://raw.githubusercontent.com/floooh/sokol-tools-bin/master/bin/linux/sokol-shdc \
#     && chmod +x tools/sokol-shdc
SHDC       := tools/sokol-shdc
SHDC_SLANG := glsl410:glsl300es:wgsl
SHADERS    := src/shaders/sk_model.glsl

shaders:
	@for s in $(SHADERS); do \
	    echo "  shdc: $$s"; \
	    $(SHDC) -i $$s -o $$s.h -l $(SHDC_SLANG) || exit 1; \
	done

# Enforce project invariants: no backend (sokol) leakage into the public
# surface, and the naming conventions in AGENTS.md.
check:
	@tools/check_no_backend_leak.sh
	@tools/check_naming.sh

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
