# Web (Emscripten) build settings, shared by the library (Makefile: make web) and
# the examples (examples/Makefile). Everything that must match between compiling
# libwgrender and linking a program against it lives here.
#
#   BACKEND=webgl2|webgpu   graphics backend (default webgl2)
#   WEB_THREADS=1|0         asset decoding on worker threads (default 1). A threaded
#                           build only starts on a cross-origin isolated page (COOP/
#                           COEP headers; tools/serve.py sends them).
#   WEB_DEBUG=0|1           1: no optimization, debug info and runtime assertions
#                           (default 0: -O2 compile, -O3 link)
#
# Sets WEB_DIR (the build directory name: webgl2, webgpu, plus -nothreads and/or
# -debug), WEB_OPT (compile optimization), WASM_CFLAGS_BACKEND (defines and flags for
# compiling) and WASM_LINK (for linking).

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
WEB_DEBUG ?= 0
ifeq ($(filter $(WEB_DEBUG),0 1),)
  $(error WEB_DEBUG must be 0 or 1 (got '$(WEB_DEBUG)'))
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
  # Workers are started with the page, but main() doesn't wait for them to load
  # (PTHREAD_POOL_DELAY_LOAD): each fetches the JS again, a round trip or more apiece
  # on a slow network (~500 ms on 4G before, measured by tools/webstart.mjs). A thread
  # created before its worker is up starts when it is; nothing on the main thread
  # waits for one to start (asset loads queue for them).
  # Growable memory with threads makes JS glue re-check the heap view on access
  # (emscripten warns about it); assets need the growth.
  WASM_THREADS_LINK := -pthread -sPTHREAD_POOL_SIZE=4 -sPTHREAD_POOL_DELAY_LOAD=1 -Wno-pthreads-mem-growth
else
  WEB_DIR           := $(BACKEND)-nothreads
  WASM_THREADS      :=
  WASM_THREADS_LINK :=
endif

# The link optimization is what shrinks the output: wasm-opt, minified JS glue, no
# assertions (simple: 874 -> 737 KB wasm, 425 -> 190 KB JS).
ifeq ($(WEB_DEBUG),1)
  WEB_DIR           := $(WEB_DIR)-debug
  WEB_OPT           := -O0 -g
  WASM_OPT_LINK     := -O0 -g -sASSERTIONS=1
  WASM_RELEASE_DEFS :=
else
  WEB_OPT           := -O2
  # Closure minifies the JS glue (45 -> 27 KB gzipped); only the browser and its
  # workers run it
  WASM_OPT_LINK     := -O3 --closure 1 -sENVIRONMENT=web,worker
  # A release build: no C asserts, and no sokol validation layer (SOKOL_DEBUG follows
  # NDEBUG), its checks and its messages. The desktop and headless builds keep both,
  # and make smoke runs every example with them; WEB_DEBUG=1 builds keep them too.
  WASM_RELEASE_DEFS := -DNDEBUG
endif

WASM_CFLAGS_BACKEND := $(WASM_DEFS) $(WASM_RELEASE_DEFS) $(WASM_THREADS)
# FORCE_FILESYSTEM so the FS JS is linked (wgr_fs: MEMFS, kept in IndexedDB);
# grow memory for assets. (No -sJSPI: sapp_run owns the loop, so we can't suspend
# in callbacks — wgr_fs's cache is polled, not awaited. See PLAN-wgr_fs.)
WASM_LINK := $(WASM_OPT_LINK) $(WASM_BACKEND_LINK) $(WASM_THREADS_LINK) -sALLOW_MEMORY_GROWTH=1 \
             -sFORCE_FILESYSTEM
