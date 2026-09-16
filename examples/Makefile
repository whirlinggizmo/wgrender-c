# libsk examples — desktop binaries + the wasm site.
#
# The library itself is built by the root Makefile; this recurses to keep it
# current (libsk.a is a real prerequisite of every example), so building here
# always links against an up-to-date lib.
#
#   make                       build all desktop examples (build/desktop/)
#   make run                   build + run hello
#   make wasm                  WebGL2 (SOKOL_GLES3), example=hello
#   make wasm BACKEND=wgpu     WebGPU (SOKOL_WGPU, emdawnwebgpu port)
#   make wasm WASM_EXAMPLE=model
#   make wasm-all              build every example for web
#   make serve                 static-serve build/web at :8000
#   make smoke                 build every example headless and run each briefly
#   make webcheck              build all for web, load each in a browser, fail on errors
#   make webcheck BACKEND=wgpu (WebGPU runs in a visible browser window)
#   make clean

UNAME_S := $(shell uname -s)
ROOT    := ..

CC      ?= cc
STD     := -std=gnu11
WARN    := -Wall -Wextra -Wno-unused-parameter
OPT     := -O2
DEFS    := -DSOKOL_GLCORE
# Public headers with full warnings; vendored single-header libs via -isystem.
INCS    := -I$(ROOT)/include -I$(ROOT)/src
INCS    += -isystem $(ROOT)/deps/sokol -isystem $(ROOT)/deps/stb \
           -isystem $(ROOT)/deps/fontstash -isystem $(ROOT)/deps/dr \
           -isystem $(ROOT)/deps/cgltf
CFLAGS  := $(STD) $(WARN) $(OPT) $(DEFS) $(INCS)

LIB     := $(ROOT)/lib/libsk.a

ifeq ($(UNAME_S),Linux)
  LDLIBS_PLATFORM := -lGL -lX11 -lXi -lXcursor -lasound -ldl -lm -lpthread
endif
ifeq ($(UNAME_S),Darwin)
  LDLIBS_PLATFORM := -framework Cocoa -framework QuartzCore -framework OpenGL \
                     -framework AudioToolbox
endif

BUILD_DESKTOP := build/desktop
SRCS          := $(wildcard *.c)
BINS          := $(patsubst %.c,$(BUILD_DESKTOP)/%,$(SRCS))

.PHONY: all run clean wasm wasm-all serve webcheck smoke headless print-ldlibs FORCE

all: $(BINS)

# Keep the library current before linking any example. Root runs its own
# up-to-date check (a cheap no-op when nothing changed) and only re-archives
# libsk.a when objects actually rebuilt, so examples relink only when needed.
$(LIB): FORCE
	@$(MAKE) -C $(ROOT)

$(BUILD_DESKTOP):
	mkdir -p $(BUILD_DESKTOP)

$(BUILD_DESKTOP)/%: %.c $(LIB) | $(BUILD_DESKTOP)
	$(CC) $(CFLAGS) $< -o $@ -L$(ROOT)/lib -lsk $(LDLIBS_PLATFORM)

run: $(BUILD_DESKTOP)/hello
	./$(BUILD_DESKTOP)/hello

print-ldlibs:
	@echo $(LDLIBS_PLATFORM)

# --- headless (no window, GPU or audio device) -------------------------------
# Same example sources linked against lib/libsk_headless.a; runs anywhere, e.g.
# CI or with the displays asleep. tools/smoke.sh runs each for SMOKE_FRAMES frames.
BUILD_HEADLESS  := build/headless
HEADLESS_LIB    := $(ROOT)/lib/libsk_headless.a
HEADLESS_BINS   := $(patsubst %.c,$(BUILD_HEADLESS)/%,$(SRCS))
LDLIBS_HEADLESS := -ldl -lm -lpthread
SMOKE_FRAMES    ?= 180

$(HEADLESS_LIB): FORCE
	@$(MAKE) --no-print-directory -C $(ROOT) HEADLESS=1

$(BUILD_HEADLESS):
	mkdir -p $(BUILD_HEADLESS)

$(BUILD_HEADLESS)/%: %.c $(HEADLESS_LIB) | $(BUILD_HEADLESS)
	$(CC) $(STD) $(WARN) $(OPT) $(INCS) $< -o $@ $(HEADLESS_LIB) $(LDLIBS_HEADLESS)

headless: $(HEADLESS_BINS)

smoke: headless
	@$(ROOT)/tools/smoke.sh $(SMOKE_FRAMES) $(HEADLESS_BINS)

# --- wasm (emscripten; WebGL2 or WebGPU) ------------------------------------
# Requires emcc on PATH (source your emsdk_env.sh first). The whole lib is
# compiled into each bundle (emcc, no static .a reuse), so no lib prereq here.
EMCC         ?= emcc
BUILD_WEB    := build/web
WEB_SHELL    := web/index.html
WASM_EXAMPLE ?= hello
BACKEND      ?= gl
LIB_SRCS     := $(wildcard $(ROOT)/src/*.c)
ifeq ($(BACKEND),wgpu)
  WASM_DEFS         := -DSOKOL_WGPU
  WASM_BACKEND_LINK := --use-port=emdawnwebgpu
else
  WASM_DEFS         := -DSOKOL_GLES3
  WASM_BACKEND_LINK := -sUSE_WEBGL2=1
endif
# idbfs for persistent storage; FORCE_FILESYSTEM so the FS/IDBFS JS is linked;
# grow memory for assets. (No -sJSPI: sapp_run owns the loop, so we can't suspend
# in callbacks — sk_fs restore is a polled barrier, not an await. See PLAN-sk_fs.)
WASM_LINK := $(WASM_BACKEND_LINK) -sALLOW_MEMORY_GROWTH=1 \
             -sFORCE_FILESYSTEM -lidbfs.js

# Per-example bundles (.js + .wasm) + one shared index.html switcher. The switcher
# self-populates from examples.json (built examples only).
define wasm_deploy
	@cp $(WEB_SHELL) $(BUILD_WEB)/index.html
	@cd $(BUILD_WEB) && printf '[%s]\n' \
	    "$$(ls *.js 2>/dev/null | sed 's/\.js$$//;s/^/"/;s/$$/"/' | paste -sd, -)" \
	    > examples.json
	@echo "deployed $(BUILD_WEB) ($(BACKEND)) — 'make serve' then open http://localhost:8000/"
endef

$(BUILD_WEB):
	mkdir -p $(BUILD_WEB)

wasm: | $(BUILD_WEB)
	$(EMCC) $(STD) $(WARN) $(OPT) $(WASM_DEFS) $(INCS) \
	    $(LIB_SRCS) $(WASM_EXAMPLE).c \
	    $(WASM_LINK) -o $(BUILD_WEB)/$(WASM_EXAMPLE).js
	$(wasm_deploy)

wasm-all: | $(BUILD_WEB)
	@for ex in $(patsubst %.c,%,$(SRCS)); do \
	    echo "  wasm[$(BACKEND)]: $$ex"; \
	    $(EMCC) $(STD) $(WARN) $(OPT) $(WASM_DEFS) $(INCS) \
	        $(LIB_SRCS) $$ex.c $(WASM_LINK) -o $(BUILD_WEB)/$$ex.js || exit 1; \
	done
	$(wasm_deploy)

serve:
	@python3 $(ROOT)/tools/serve.py 8000

# Web smoke check (tools/webcheck.mjs): needs Node >= 22 and a Chromium-based
# browser (Brave/Chrome/Chromium). Extra options: WEBCHECK_FLAGS="--headed pick".
# resolve via the shell: make's direct exec can hit a node/ directory on PATH (emsdk)
NODE ?= $(shell command -v node 2>/dev/null || echo node)
webcheck: wasm-all
	@$(NODE) $(ROOT)/tools/webcheck.mjs --backend=$(BACKEND) $(WEBCHECK_FLAGS)

clean:
	rm -rf build
