# libsk examples — desktop binaries + the wasm site.
#
# The library itself is built by the root Makefile; this recurses to keep it
# current (libsk.a is a real prerequisite of every example), so building here
# always links against an up-to-date lib.
#
#   make                       build all desktop examples (build/desktop/)
#   make run                   build + run hello
#   make wasm                  WebGL2 (SOKOL_GLES3), example=hello
#   make wasm BACKEND=webgpu   WebGPU (SOKOL_WGPU, emdawnwebgpu port)
#   make wasm WASM_EXAMPLE=model
#   make wasm-all              build every example for web
#   make serve [BACKEND=...]   static-serve build/<backend> at :8000
#   make smoke                 build every example headless and run each briefly
#   make webcheck              build all for web, load each in a browser, fail on errors
#   make webcheck BACKEND=webgpu (WebGPU runs in a visible browser window)
#
# Outputs, one directory per target: build/desktop, build/headless, and the web
# sites build/webgl2 and build/webgpu (each with obj/ and webcheck/ screenshots).
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

LIB     := $(ROOT)/build/desktop/libsk.a

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
	$(CC) $(CFLAGS) $< -o $@ $(LIB) $(LDLIBS_PLATFORM)

run: $(BUILD_DESKTOP)/hello
	./$(BUILD_DESKTOP)/hello

print-ldlibs:
	@echo $(LDLIBS_PLATFORM)

# --- headless (no window, GPU or audio device) -------------------------------
# Same example sources linked against build/headless/libsk.a; runs anywhere, e.g.
# CI or with the displays asleep. tools/smoke.sh runs each for SMOKE_FRAMES frames.
BUILD_HEADLESS  := build/headless
HEADLESS_LIB    := $(ROOT)/build/headless/libsk.a
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
# Each backend has its own directory, build/webgl2 or build/webgpu: the site
# (index.html, examples.json, <example>.js/.wasm), obj/ (the library compiled once,
# with header dependency tracking, plus example objects) and webcheck/ screenshots.
# Everything builds in parallel and only what changed rebuilds.
EMCC         ?= emcc
BACKEND      ?= webgl2
ifeq ($(filter $(BACKEND),webgl2 webgpu),)
  $(error BACKEND must be webgl2 or webgpu (got '$(BACKEND)'))
endif
# WEB_THREADS=1 (default): asset decoding on worker threads. The page must be
# cross-origin isolated (COOP/COEP headers; tools/serve.py sends them), or the
# build doesn't start. WEB_THREADS=0 builds without threads, into build/<backend>-nothreads.
WEB_THREADS  ?= 1
ifeq ($(filter $(WEB_THREADS),0 1),)
  $(error WEB_THREADS must be 0 or 1 (got '$(WEB_THREADS)'))
endif
ifeq ($(WEB_THREADS),1)
  BUILD_WEB    := build/$(BACKEND)
  WASM_THREADS := -pthread
  # workers are started up front, so starting a load never waits on one.
  # Growable memory with threads makes JS glue re-check the heap view on access
  # (emscripten warns about it); assets need the growth.
  WASM_THREADS_LINK := -pthread -sPTHREAD_POOL_SIZE=4 -Wno-pthreads-mem-growth
else
  BUILD_WEB    := build/$(BACKEND)-nothreads
  WASM_THREADS :=
  WASM_THREADS_LINK :=
endif
WEB_SHELL    := web/index.html
WASM_EXAMPLE ?= hello
NPROC        := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
LIB_SRCS     := $(wildcard $(ROOT)/src/*.c)
WEB_OBJ_DIR  := $(BUILD_WEB)/obj
WEB_LIB_OBJS := $(patsubst $(ROOT)/src/%.c,$(WEB_OBJ_DIR)/lib/%.o,$(LIB_SRCS))
WEB_EX_OBJS  := $(patsubst %.c,$(WEB_OBJ_DIR)/examples/%.o,$(SRCS))
WEB_BUNDLES  := $(patsubst %.c,$(BUILD_WEB)/%.js,$(SRCS))
ifeq ($(BACKEND),webgpu)
  WASM_DEFS         := -DSOKOL_WGPU --use-port=emdawnwebgpu # the port provides webgpu/webgpu.h
  WASM_BACKEND_LINK := --use-port=emdawnwebgpu
else
  WASM_DEFS         := -DSOKOL_GLES3
  WASM_BACKEND_LINK := -sUSE_WEBGL2=1
endif
# -MD (not -MMD): also track the vendored -isystem headers in deps/
WASM_CFLAGS := $(STD) $(WARN) $(OPT) $(WASM_DEFS) $(WASM_THREADS) $(INCS) -MD -MP
# idbfs for persistent storage; FORCE_FILESYSTEM so the FS/IDBFS JS is linked;
# grow memory for assets. (No -sJSPI: sapp_run owns the loop, so we can't suspend
# in callbacks — sk_fs restore is a polled barrier, not an await. See PLAN-sk_fs.)
WASM_LINK := $(WASM_BACKEND_LINK) $(WASM_THREADS_LINK) -sALLOW_MEMORY_GROWTH=1 \
             -sFORCE_FILESYSTEM -lidbfs.js

# Per-example bundles (.js + .wasm) + one shared index.html switcher. The switcher
# self-populates from examples.json (built examples only).
define wasm_deploy
	@cp $(WEB_SHELL) $(BUILD_WEB)/index.html
	@cd $(BUILD_WEB) && printf '[%s]\n' \
	    "$$(ls *.js 2>/dev/null | sed 's/\.js$$//;s/^/"/;s/$$/"/' | paste -sd, -)" \
	    > examples.json
	@echo "deployed $(BUILD_WEB) — 'make serve BACKEND=$(BACKEND)' then open http://localhost:8000/"
endef

$(BUILD_WEB):
	mkdir -p $(BUILD_WEB)

# Rebuild everything when the compile flags change (WEB_THREADS, OPT, ...).
WEB_FLAGS_STAMP := $(WEB_OBJ_DIR)/flags
$(WEB_FLAGS_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(WASM_CFLAGS)' | cmp -s - $@ || echo '$(WASM_CFLAGS)' > $@

$(WEB_OBJ_DIR)/lib/%.o: $(ROOT)/src/%.c $(WEB_FLAGS_STAMP)
	@mkdir -p $(dir $@)
	@echo "  emcc[$(BACKEND)]: $<"
	@$(EMCC) $(WASM_CFLAGS) -c $< -o $@

$(WEB_OBJ_DIR)/examples/%.o: %.c $(WEB_FLAGS_STAMP)
	@mkdir -p $(dir $@)
	@$(EMCC) $(WASM_CFLAGS) -c $< -o $@

$(BUILD_WEB)/%.js: $(WEB_OBJ_DIR)/examples/%.o $(WEB_LIB_OBJS)
	@echo "  wasm[$(BACKEND)]: $*"
	@$(EMCC) $(WEB_OBJ_DIR)/examples/$*.o $(WEB_LIB_OBJS) $(WASM_LINK) -o $@

-include $(WEB_LIB_OBJS:.o=.d) $(WEB_EX_OBJS:.o=.d)

wasm:
	@$(MAKE) --no-print-directory -j$(NPROC) $(BUILD_WEB)/$(WASM_EXAMPLE).js
	$(wasm_deploy)

wasm-all:
	@$(MAKE) --no-print-directory -j$(NPROC) $(WEB_BUNDLES)
	$(wasm_deploy)

serve:
	@python3 $(ROOT)/tools/serve.py 8000 $(BUILD_WEB)

# Web smoke check (tools/webcheck.mjs): needs Node >= 22 and a Chromium-based
# browser (Brave/Chrome/Chromium). Extra options: WEBCHECK_FLAGS="--headed pick".
# resolve via the shell: make's direct exec can hit a node/ directory on PATH (emsdk)
NODE ?= $(shell command -v node 2>/dev/null || echo node)
webcheck: wasm-all
	@$(NODE) $(ROOT)/tools/webcheck.mjs --backend=$(BACKEND) --threads=$(WEB_THREADS) $(WEBCHECK_FLAGS)

clean:
	rm -rf build
