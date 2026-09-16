# libsk — sokol backend (desktop GL first)
#
# This Makefile builds the LIBRARY. Example programs (desktop + wasm) live under
# examples/ with their own Makefile; the targets below delegate there via
# `$(MAKE) -C examples`, and the examples build depends back on this lib.
#
# Targets:
#   make            build static lib (lib/libsk.a)
#   make examples   build example programs        (-> examples/Makefile)
#   make run        build + run the hello example  (-> examples/Makefile)
#   make wasm[-all] build the web examples         (-> examples/Makefile)
#   make serve      static-serve the web build     (-> examples/Makefile)
#   make shaders    regenerate shdc shader headers
#   make check      naming / backend-leak guardrails
#   make deps       install system build deps (ALSA/GL/X11 dev packages)
#   make clean

UNAME_S := $(shell uname -s)

CC      ?= cc
AR      ?= ar
STD     := -std=gnu11
WARN    := -Wall -Wextra -Wno-unused-parameter
OPT     := -O2 # -g  # use -g for debug build.  TODO:  Flag for release/debug? 
DEFS    := -DSOKOL_GLCORE
# Our headers use -I (full warnings); vendored single-header libs use -isystem so
# their warnings (stb/fontstash/dr/cgltf/sokol) don't drown out ours.
INCS    := -Iinclude -Isrc
INCS    += -isystem deps/sokol -isystem deps/stb -isystem deps/fontstash \
           -isystem deps/dr -isystem deps/cgltf
CFLAGS  := $(STD) $(WARN) $(OPT) $(DEFS) $(INCS)

BUILD   := build
LIBDIR  := lib
LIB     := $(LIBDIR)/libsk.a

SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

.PHONY: all examples run clean check wasm wasm-all serve shaders deps deps-check

all: $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

$(LIBDIR):
	mkdir -p $(LIBDIR)

$(BUILD)/%.o: src/%.c | $(BUILD) deps-check
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS) | $(LIBDIR)
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
run wasm wasm-all serve:
	@$(MAKE) -C examples $@

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

clean:
	rm -rf $(BUILD) $(LIBDIR)
	@$(MAKE) -C examples clean
