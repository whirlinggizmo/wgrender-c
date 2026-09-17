# Web (Emscripten) build settings, shared by the library (Makefile: make web) and
# the examples (examples/Makefile). Everything that must match between compiling
# libsk and linking a program against it lives here.
#
#   BACKEND=webgl2|webgpu   graphics backend (default webgl2)
#   WEB_THREADS=1|0         asset decoding on worker threads (default 1). A threaded
#                           build only starts on a cross-origin isolated page (COOP/
#                           COEP headers; tools/serve.py sends them).
#
# Sets WEB_DIR (webgl2, webgpu, or <backend>-nothreads: the build directory name),
# WASM_CFLAGS_BACKEND (defines and flags for compiling) and WASM_LINK (for linking).

EMCC ?= emcc
EMAR ?= emar
BACKEND ?= webgl2
ifeq ($(filter $(BACKEND),webgl2 webgpu),)
  $(error BACKEND must be webgl2 or webgpu (got '$(BACKEND)'))
endif
WEB_THREADS ?= 1
ifeq ($(filter $(WEB_THREADS),0 1),)
  $(error WEB_THREADS must be 0 or 1 (got '$(WEB_THREADS)'))
endif

ifeq ($(BACKEND),webgpu)
  WASM_DEFS         := -DSOKOL_WGPU --use-port=emdawnwebgpu # the port provides webgpu/webgpu.h
  WASM_BACKEND_LINK := --use-port=emdawnwebgpu
else
  WASM_DEFS         := -DSOKOL_GLES3
  WASM_BACKEND_LINK := -sUSE_WEBGL2=1
endif

ifeq ($(WEB_THREADS),1)
  WEB_DIR           := $(BACKEND)
  WASM_THREADS      := -pthread
  # workers are started up front, so starting a load never waits on one.
  # Growable memory with threads makes JS glue re-check the heap view on access
  # (emscripten warns about it); assets need the growth.
  WASM_THREADS_LINK := -pthread -sPTHREAD_POOL_SIZE=4 -Wno-pthreads-mem-growth
else
  WEB_DIR           := $(BACKEND)-nothreads
  WASM_THREADS      :=
  WASM_THREADS_LINK :=
endif

WASM_CFLAGS_BACKEND := $(WASM_DEFS) $(WASM_THREADS)
# idbfs for persistent storage; FORCE_FILESYSTEM so the FS/IDBFS JS is linked;
# grow memory for assets. (No -sJSPI: sapp_run owns the loop, so we can't suspend
# in callbacks — sk_fs restore is a polled barrier, not an await. See PLAN-sk_fs.)
WASM_LINK := $(WASM_BACKEND_LINK) $(WASM_THREADS_LINK) -sALLOW_MEMORY_GROWTH=1 \
             -sFORCE_FILESYSTEM -lidbfs.js
