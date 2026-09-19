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
#   make wasm-all              build every example for web (prints a size summary)
#   make websize               wasm/JS sizes of every built web example
#   make serve [BACKEND=...]   static-serve build/<backend> at :8000
#   make smoke                 build every example headless and run each briefly
#   make webcheck              build all for web, load each in a browser, fail on errors
#   make webcheck BACKEND=webgpu (on a virtual X display when Xvfb is installed)
#
# Outputs, one directory per target: build/desktop, build/headless, and the web
# sites build/webgl2, build/webgpu and build/<backend>-nothreads (each with obj/
# and webcheck/ screenshots). The libraries are in the root build/ directory.
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
INCS    += -isystem $(ROOT)/deps/sokol -isystem $(ROOT)/deps/sokol_utils -isystem $(ROOT)/deps/stb \
           -isystem $(ROOT)/deps/fontstash -isystem $(ROOT)/deps/dr \
           -isystem $(ROOT)/deps/cgltf -isystem $(ROOT)/deps/clay
CFLAGS  := $(STD) $(WARN) $(OPT) $(DEFS) $(INCS)

LIB     := $(ROOT)/build/desktop/libsk.a
# WINDOWS=1: the same examples cross-compiled with MinGW as .exe files (build/windows;
# the root Makefile's `make windows`). The smoke run goes through Wine.
WINDOWS ?= 0
MINGW   ?= x86_64-w64-mingw32-
LIB_FLAGS :=
EXE       :=

ifeq ($(WINDOWS),1)
  CC              := $(MINGW)gcc
  EXE             := .exe
  LIB             := $(ROOT)/build/windows/libsk.a
  LIB_FLAGS       := WINDOWS=1
  LDLIBS_PLATFORM := -static -lopengl32 -lgdi32 -luser32 -lkernel32 -lole32 -lshell32 -lwinmm
else ifeq ($(UNAME_S),Linux)
  LDLIBS_PLATFORM := -lGL -lX11 -lXi -lXcursor -lXrandr -lasound -ldl -lm -lpthread
else ifeq ($(UNAME_S),Darwin)
  LDLIBS_PLATFORM := -framework Cocoa -framework QuartzCore -framework OpenGL \
                     -framework AudioToolbox
endif

BUILD_DESKTOP := build/$(if $(filter 1,$(WINDOWS)),windows,desktop)
SRCS          := $(wildcard *.c)
BINS          := $(patsubst %.c,$(BUILD_DESKTOP)/%$(EXE),$(SRCS))

.PHONY: all run clean wasm wasm-all websize serve serve-tls spritebench-web loadbench-web webcheck webstart smoke headless print-ldlibs FORCE

all: $(BINS)

# Keep the library current before linking any example. Root runs its own
# up-to-date check (a cheap no-op when nothing changed) and only re-archives
# libsk.a when objects actually rebuilt, so examples relink only when needed.
$(LIB): FORCE
	@$(MAKE) -C $(ROOT) $(LIB_FLAGS)

$(BUILD_DESKTOP):
	mkdir -p $(BUILD_DESKTOP)

$(BUILD_DESKTOP)/%$(EXE): %.c $(LIB) | $(BUILD_DESKTOP)
	$(CC) $(CFLAGS) $< -o $@ $(LIB) $(LDLIBS_PLATFORM)

run: $(BUILD_DESKTOP)/hello
	./$(BUILD_DESKTOP)/hello

print-ldlibs:
	@echo $(LDLIBS_PLATFORM)

# --- headless (no window, GPU or audio device) -------------------------------
# Same example sources linked against build/headless/libsk.a; runs anywhere, e.g.
# CI or with the displays asleep. tools/smoke.sh runs each for SMOKE_FRAMES frames.
BUILD_HEADLESS  := build/$(if $(filter 1,$(WINDOWS)),windows-headless,headless)
HEADLESS_LIB    := $(ROOT)/$(BUILD_HEADLESS)/libsk.a
HEADLESS_BINS   := $(patsubst %.c,$(BUILD_HEADLESS)/%$(EXE),$(SRCS))
LDLIBS_HEADLESS := $(if $(filter 1,$(WINDOWS)),-static,-ldl -lm -lpthread)
SMOKE_FRAMES    ?= 180
SMOKE_RUNNER    := $(if $(filter 1,$(WINDOWS)),$(abspath $(ROOT))/tools/wine.sh)

$(HEADLESS_LIB): FORCE
	@$(MAKE) --no-print-directory -C $(ROOT) HEADLESS=1 $(LIB_FLAGS)

$(BUILD_HEADLESS):
	mkdir -p $(BUILD_HEADLESS)

$(BUILD_HEADLESS)/%$(EXE): %.c $(HEADLESS_LIB) | $(BUILD_HEADLESS)
	$(CC) $(STD) $(WARN) $(OPT) $(INCS) $< -o $@ $(HEADLESS_LIB) $(LDLIBS_HEADLESS)

headless: $(HEADLESS_BINS)

smoke: headless
	@SMOKE_RUNNER="$(SMOKE_RUNNER)" $(ROOT)/tools/smoke.sh $(SMOKE_FRAMES) $(HEADLESS_BINS)

# --- wasm (emscripten; WebGL2 or WebGPU) ------------------------------------
# Each web target has its own site directory, build/<backend> (or
# build/<backend>-nothreads): index.html, examples.json, <example>.js/.wasm, obj/
# (example objects) and webcheck/ screenshots. The examples link the web library
# the root Makefile builds (build/<backend>/libsk.a, `make web`); BACKEND and
# WEB_THREADS are defined in mk/web.mk.
include $(ROOT)/mk/web.mk
BUILD_WEB    := build/$(WEB_DIR)
WEB_LIB      := $(ROOT)/build/$(WEB_DIR)/libsk.a
WEB_SHELL    := web/index.html
WASM_EXAMPLE ?= hello
NPROC        := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
WEB_OBJ_DIR  := $(BUILD_WEB)/obj
WEB_EX_OBJS  := $(patsubst %.c,$(WEB_OBJ_DIR)/examples/%.o,$(SRCS))
WEB_BUNDLES  := $(patsubst %.c,$(BUILD_WEB)/%.js,$(SRCS))
# -MD (not -MMD): also track the vendored -isystem headers in deps/
WASM_CFLAGS := $(STD) $(WARN) $(WEB_OPT) $(WASM_CFLAGS_BACKEND) $(INCS) -MD -MP

# Per-example bundles (.js + .wasm) + one shared index.html switcher. The switcher
# self-populates from examples.json (built examples only).
define wasm_deploy
	@python3 $(ROOT)/tools/webdeploy.py $(BUILD_WEB) $(WEB_SHELL)
	@echo "deployed $(BUILD_WEB) — 'make serve BACKEND=$(BACKEND)' then open http://localhost:8000/"
endef

$(BUILD_WEB):
	mkdir -p $(BUILD_WEB)

# Keep the web library current (like $(LIB) above), sharing this make's job slots.
$(WEB_LIB): FORCE
	@$(MAKE) --no-print-directory -C $(ROOT) all WEB=1

# Rebuild the example objects when the compile flags change (WEB_THREADS, OPT, ...).
WEB_FLAGS_STAMP := $(WEB_OBJ_DIR)/flags
$(WEB_FLAGS_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(WASM_CFLAGS)' | cmp -s - $@ || echo '$(WASM_CFLAGS)' > $@

# ... and relink the programs when the link flags change (mk/web.mk's WASM_LINK).
WEB_LINK_STAMP := $(WEB_OBJ_DIR)/link-flags
$(WEB_LINK_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(WASM_LINK)' | cmp -s - $@ || echo '$(WASM_LINK)' > $@

$(WEB_OBJ_DIR)/examples/%.o: %.c $(WEB_FLAGS_STAMP)
	@mkdir -p $(dir $@)
	@$(EMCC) $(WASM_CFLAGS) -c $< -o $@

$(BUILD_WEB)/%.js: $(WEB_OBJ_DIR)/examples/%.o $(WEB_LIB) $(WEB_LINK_STAMP)
	@echo "  wasm[$(WEB_DIR)]: $*"
	@$(EMCC) $(WEB_OBJ_DIR)/examples/$*.o $(WEB_LIB) $(WASM_LINK) -o $@

-include $(WEB_EX_OBJS:.o=.d)

# The benchmarks (tools/bench) as web pages of their own, build/<backend>/bench/, so
# they stay out of the examples list and webcheck. Serve the site (make serve /
# serve-tls) and open /bench/?ex=spritebench or ?ex=loadbench; the results are
# printed to the browser console. LOADBENCH_FLAGS picks what loadbench loads, e.g.
# "-DLOADBENCH_HELMET_ONLY -DLOADBENCH_KTX=1" (FlightHelmet alone, compressed).
BENCH_WEB := $(BUILD_WEB)/bench
BENCH_FLAGS_spritebench :=
BENCH_FLAGS_loadbench = $(LOADBENCH_FLAGS)
$(WEB_OBJ_DIR)/bench/%.o: $(ROOT)/tools/bench/%.c $(WEB_FLAGS_STAMP) FORCE
	@mkdir -p $(dir $@)
	@$(EMCC) $(WASM_CFLAGS) $(BENCH_FLAGS_$*) -c $< -o $@

$(BENCH_WEB)/%.js: $(WEB_OBJ_DIR)/bench/%.o $(WEB_LIB) $(WEB_LINK_STAMP)
	@mkdir -p $(dir $@)
	@echo "  wasm[$(WEB_DIR)]: $*"
	@$(EMCC) $< $(WEB_LIB) $(WASM_LINK) -o $@

spritebench-web loadbench-web: %-web: $(BENCH_WEB)/%.js
	@cp $(WEB_SHELL) $(BENCH_WEB)/index.html
	@ls $(BENCH_WEB)/*.js | sed 's#.*/\(.*\)\.js$$#"\1"#' | paste -sd, | sed 's/.*/[&]/' > $(BENCH_WEB)/examples.json
	@echo "built $(BENCH_WEB): serve the site, then open /bench/?ex=$* (results in the console)"

-include $(wildcard $(WEB_OBJ_DIR)/bench/*.d)

wasm:
	@$(MAKE) --no-print-directory -j$(NPROC) $(BUILD_WEB)/$(WASM_EXAMPLE).js
	$(wasm_deploy)

wasm-all:
	@$(MAKE) --no-print-directory -j$(NPROC) $(WEB_BUNDLES)
	$(wasm_deploy)
	@$(ROOT)/tools/websize.sh $(BUILD_WEB) | tail -n +2 | awk 'NR == 1 || /^hello |^simple |^model / { print } END { print "(all examples: $(BUILD_WEB)/sizes.txt, make websize)" }'

# Web build sizes per example (tools/websize.sh), for the current BACKEND/WEB_THREADS.
websize:
	@$(ROOT)/tools/websize.sh $(BUILD_WEB)

serve:
	@python3 $(ROOT)/tools/serve.py 8000 $(BUILD_WEB)

# HTTPS on 8443 for other devices on the LAN (a phone): threaded builds need a secure
# page, and only localhost is secure over plain HTTP. TLS_CERT / TLS_KEY (command line
# or environment) name a certificate the device trusts and its key.
serve-tls:
	@if [ -z "$(TLS_CERT)" ] || [ -z "$(TLS_KEY)" ]; then \
	    echo "serve-tls: set TLS_CERT and TLS_KEY (PEM files; the certificate must name this machine's LAN address)"; \
	    exit 1; \
	fi
	@python3 $(ROOT)/tools/serve.py 8443 $(BUILD_WEB) --tls "$(TLS_CERT)" "$(TLS_KEY)"

# Web smoke check (tools/webcheck.mjs): needs Node >= 22 and a Chromium-based
# browser (Brave/Chrome/Chromium). Extra options: WEBCHECK_FLAGS="--headed pick".
# resolve via the shell: make's direct exec can hit a node/ directory on PATH (emsdk)
NODE ?= $(shell command -v node 2>/dev/null || echo node)
webcheck: wasm-all
	@$(NODE) $(ROOT)/tools/webcheck.mjs --backend=$(BACKEND) --threads=$(WEB_THREADS) $(WEBCHECK_FLAGS)

# Startup times per example, cold / warm / hot (tools/webstart.mjs). Extra options:
# WEBSTART_FLAGS="--net=4g simple model".
webstart: wasm-all
	@$(NODE) $(ROOT)/tools/webstart.mjs --backend=$(BACKEND) --threads=$(WEB_THREADS) $(WEBSTART_FLAGS)

clean:
	rm -rf build
