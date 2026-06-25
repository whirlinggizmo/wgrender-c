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
INCS    := -Iinclude -Isrc -Ideps/sokol -Ideps/stb -Ideps/fontstash -Ideps/dr -Ideps/cgltf
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
EX_BUILD := examples/build
EX_SRCS  := $(wildcard examples/*.c)
EX_BINS  := $(patsubst examples/%.c,$(EX_BUILD)/%,$(EX_SRCS))

examples: $(EX_BINS)

$(EX_BUILD):
	mkdir -p $(EX_BUILD)

$(EX_BUILD)/%: examples/%.c $(LIB) | $(EX_BUILD)
	$(CC) $(CFLAGS) $< -o $@ -L$(LIBDIR) -lsk $(LDLIBS_PLATFORM)

run: $(EX_BUILD)/hello
	./$(EX_BUILD)/hello

print-ldlibs:
	@echo $(LDLIBS_PLATFORM)

# --- wasm (emscripten; WebGL2 or WebGPU, JSPI) ------------------------------
# Requires emcc on PATH (source your emsdk_env.sh first).
#   make wasm                  # WebGL2 (SOKOL_GLES3), example=hello
#   make wasm BACKEND=wgpu     # WebGPU (SOKOL_WGPU, emdawnwebgpu port)
#   make wasm WASM_EXAMPLE=model
#   make serve                 # static-serve web/ at :8000
EMCC         ?= emcc
WEB          := web
WEB_SHELL    := examples/web/index.html
WASM_EXAMPLE ?= hello
BACKEND      ?= gl
ifeq ($(BACKEND),wgpu)
  WASM_DEFS         := -DSOKOL_WGPU
  WASM_BACKEND_LINK := --use-port=emdawnwebgpu
else
  WASM_DEFS         := -DSOKOL_GLES3
  WASM_BACKEND_LINK := -sUSE_WEBGL2=1
endif
# JSPI (not ASYNCIFY) for the future idbfs sync shim; grow memory for assets.
WASM_LINK := $(WASM_BACKEND_LINK) -sJSPI -sALLOW_MEMORY_GROWTH=1

# Per-example bundles (.js + .wasm) + one shared index.html switcher served from
# web/. The switcher self-populates from web/examples.json (built examples only).
define wasm_deploy
	@cp $(WEB_SHELL) $(WEB)/index.html
	@cd $(WEB) && printf '[%s]\n' \
	    "$$(ls *.js 2>/dev/null | sed 's/\.js$$//;s/^/"/;s/$$/"/' | paste -sd, -)" \
	    > examples.json
	@echo "deployed $(WEB)/ ($(BACKEND)) — 'make serve' then open http://localhost:8000/"
endef

wasm: | $(WEB)
	$(EMCC) $(STD) $(WARN) $(OPT) $(WASM_DEFS) $(INCS) \
	    $(SRCS) examples/$(WASM_EXAMPLE).c \
	    $(WASM_LINK) -o $(WEB)/$(WASM_EXAMPLE).js
	$(wasm_deploy)

wasm-all: | $(WEB)
	@for ex in $(patsubst examples/%.c,%,$(EX_SRCS)); do \
	    echo "  wasm[$(BACKEND)]: $$ex"; \
	    $(EMCC) $(STD) $(WARN) $(OPT) $(WASM_DEFS) $(INCS) \
	        $(SRCS) examples/$$ex.c $(WASM_LINK) -o $(WEB)/$$ex.js || exit 1; \
	done
	$(wasm_deploy)

$(WEB):
	mkdir -p $(WEB)

serve:
	@echo "serving $(WEB)/ at http://localhost:8000  (Ctrl-C to stop)"
	@cd $(WEB) && python3 -m http.server 8000

# Enforce project invariants: no backend (sokol) leakage into the public
# surface, and the naming conventions in AGENTS.md.
check:
	@tools/check_no_backend_leak.sh
	@tools/check_naming.sh

clean:
	rm -rf $(BUILD) $(LIBDIR) $(EX_BUILD) $(WEB)
