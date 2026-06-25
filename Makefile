# libsk — sokol backend (desktop GL first)
#
# Targets:
#   make            build static lib (lib/libsk.a)
#   make examples   build example programs (examples/build/)
#   make run        build + run the hello example
#   make clean

UNAME_S := $(shell uname -s)

CC      ?= cc
AR      ?= ar
STD     := -std=gnu11
WARN    := -Wall -Wextra -Wno-unused-parameter
OPT     := -O2 -g
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

# Platform link flags (consumers link these alongside -lsk)
ifeq ($(UNAME_S),Linux)
  LDLIBS_PLATFORM := -lGL -lX11 -lXi -lXcursor -lasound -ldl -lm -lpthread
endif
ifeq ($(UNAME_S),Darwin)
  LDLIBS_PLATFORM := -framework Cocoa -framework QuartzCore -framework OpenGL \
                     -framework AudioToolbox
endif

.PHONY: all examples run clean print-ldlibs check wasm wasm-all serve

all: $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

$(LIBDIR):
	mkdir -p $(LIBDIR)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS) | $(LIBDIR)
	$(AR) rcs $@ $(OBJS)
	@echo "built $@"

# --- examples ---------------------------------------------------------------
EX_DIR           := examples
EX_BUILD_DESKTOP := $(EX_DIR)/build/desktop
EX_SRCS          := $(wildcard $(EX_DIR)/*.c)
EX_BINS          := $(patsubst $(EX_DIR)/%.c,$(EX_BUILD_DESKTOP)/%,$(EX_SRCS))

examples: $(EX_BINS)

$(EX_BUILD_DESKTOP):
	mkdir -p $(EX_BUILD_DESKTOP)

$(EX_BUILD_DESKTOP)/%: $(EX_DIR)/%.c $(LIB) | $(EX_BUILD_DESKTOP)
	$(CC) $(CFLAGS) $< -o $@ -L$(LIBDIR) -lsk $(LDLIBS_PLATFORM)

run: $(EX_BUILD_DESKTOP)/hello
	./$(EX_BUILD_DESKTOP)/hello

print-ldlibs:
	@echo $(LDLIBS_PLATFORM)

# --- wasm (emscripten; WebGL2 or WebGPU) ------------------------------------
# Requires emcc on PATH (source your emsdk_env.sh first).
#   make wasm                  # WebGL2 (SOKOL_GLES3), example=hello
#   make wasm BACKEND=wgpu     # WebGPU (SOKOL_WGPU, emdawnwebgpu port)
#   make wasm WASM_EXAMPLE=model
#   make serve                 # static-serve examples/build/web at :8000
EMCC         ?= emcc
EX_BUILD_WEB := $(EX_DIR)/build/web
WEB_SHELL    := $(EX_DIR)/web/index.html
WASM_EXAMPLE ?= hello
BACKEND      ?= gl
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
# ccall + HEAPU8 + malloc/free: the sk_asset web fetch bridge hands downloaded
# bytes from JS back into C (sk_asset_on_fetched). idbfs for persistent storage.
WASM_LINK := $(WASM_BACKEND_LINK) -sALLOW_MEMORY_GROWTH=1 \
             -sFORCE_FILESYSTEM -lidbfs.js \
             -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU8 \
             -sEXPORTED_FUNCTIONS=_main,_malloc,_free

# Per-example bundles (.js + .wasm) + one shared index.html switcher. The switcher
# self-populates from examples.json (built examples only).
define wasm_deploy
	@cp $(WEB_SHELL) $(EX_BUILD_WEB)/index.html
	@cd $(EX_BUILD_WEB) && printf '[%s]\n' \
	    "$$(ls *.js 2>/dev/null | sed 's/\.js$$//;s/^/"/;s/$$/"/' | paste -sd, -)" \
	    > examples.json
	@echo "deployed $(EX_BUILD_WEB) ($(BACKEND)) — 'make serve' then open http://localhost:8000/"
endef

wasm: | $(EX_BUILD_WEB)
	$(EMCC) $(STD) $(WARN) $(OPT) $(WASM_DEFS) $(INCS) \
	    $(SRCS) $(EX_DIR)/$(WASM_EXAMPLE).c \
	    $(WASM_LINK) -o $(EX_BUILD_WEB)/$(WASM_EXAMPLE).js
	$(wasm_deploy)

wasm-all: | $(EX_BUILD_WEB)
	@for ex in $(patsubst $(EX_DIR)/%.c,%,$(EX_SRCS)); do \
	    echo "  wasm[$(BACKEND)]: $$ex"; \
	    $(EMCC) $(STD) $(WARN) $(OPT) $(WASM_DEFS) $(INCS) \
	        $(SRCS) $(EX_DIR)/$$ex.c $(WASM_LINK) -o $(EX_BUILD_WEB)/$$ex.js || exit 1; \
	done
	$(wasm_deploy)

$(EX_BUILD_WEB):
	mkdir -p $(EX_BUILD_WEB)

serve:
	@python3 tools/serve.py 8000

# Enforce project invariants: no backend (sokol) leakage into the public
# surface, and the naming conventions in AGENTS.md.
check:
	@tools/check_no_backend_leak.sh
	@tools/check_naming.sh

clean:
	rm -rf $(BUILD) $(LIBDIR) $(EX_DIR)/build
