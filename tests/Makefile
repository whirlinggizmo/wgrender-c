# libwgrender tests.
#
#   make test                  build the headless library if needed, build and run unit tests
#   make test TESTS=pick_      only tests whose name starts with "pick_"
#   make test SANITIZE=thread  build library + tests with a sanitizer (thread, address, undefined)
#   make test WINDOWS=1        cross-compile with MinGW and run under Wine (tools/wine.sh)
#   make clean
#
# Unit tests (tests/unit/) call library internals directly and link against
# build/<host>-headless/libwgrender.a, with no stubs. They don't open a window or touch the GPU.

ROOT := ..

CC      ?= cc
STD     := -std=gnu11
WARN    := -Wall -Wextra -Wno-unused-parameter
OPT     := -O1 -g
DEFS    := -DWGR_HEADLESS -DSOKOL_DUMMY_BACKEND
INCS    := -I$(ROOT)/include -I$(ROOT)/src -Iunit -isystem $(ROOT)/deps/sokol -isystem $(ROOT)/deps/fontstash
CFLAGS  := $(STD) $(WARN) $(OPT) $(DEFS) $(INCS)

# Unit tests link the headless library: no window, GPU, audio device, or
# GL/X11/ALSA libraries needed (runs in CI and with no display).
include $(ROOT)/mk/host.mk # HOST_OS: linux / macos
LIB     := $(ROOT)/build/$(HOST_OS)-headless/libwgrender.a
LDLIBS_PLATFORM := -ldl -lm -lpthread
LIB_FLAGS :=
EXE       :=

BUILD      := build
WINDOWS    ?= 0
MINGW      ?= x86_64-w64-mingw32-
ifeq ($(WINDOWS),1)
  CC              := $(MINGW)gcc
  LIB             := $(ROOT)/build/windows-headless/libwgrender.a
  LDLIBS_PLATFORM := -static -lpthread # the tests call POSIX threads and clocks (winpthreads); no DLLs to ship
  LIB_FLAGS       := WINDOWS=1
  EXE             := .exe
  BUILD           := build/windows
endif
SANITIZE   ?=
UNIT_BIN   := $(BUILD)/unit_tests$(if $(SANITIZE),_$(SANITIZE))$(EXE)
UNIT_SRCS  := $(wildcard unit/*.c)
UNIT_HDRS  := $(wildcard unit/*.h)

.PHONY: test unit clean FORCE

test: unit

# ThreadSanitizer can't map its shadow memory with the high ASLR entropy of newer
# kernels ("unexpected memory mapping"), so run it with ASLR off.
RUN := $(if $(filter thread,$(SANITIZE)),setarch $(shell uname -m) -R)
ifeq ($(WINDOWS),1)
  RUN := $(abspath $(ROOT))/tools/wine.sh
endif

unit: $(UNIT_BIN)
	@$(RUN) ./$(UNIT_BIN) $(TESTS)

# Keep the library current (the root build is a cheap no-op when nothing changed).
$(LIB): FORCE
	@$(MAKE) --no-print-directory -C $(ROOT) HEADLESS=1 $(LIB_FLAGS)

$(BUILD):
	mkdir -p $(BUILD)

ifeq ($(SANITIZE),)
$(UNIT_BIN): $(UNIT_SRCS) $(UNIT_HDRS) $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(UNIT_SRCS) -o $@ $(LIB) $(LDLIBS_PLATFORM)
else
# Sanitizer build (`make test SANITIZE=thread`, `address`, `undefined`): the
# library is compiled into the test binary with instrumentation, since the
# prebuilt archive isn't.
LIB_SRCS := $(wildcard $(ROOT)/src/*.c)
SAN_INCS := -isystem $(ROOT)/deps/stb -isystem $(ROOT)/deps/fontstash -isystem $(ROOT)/deps/dr \
            -isystem $(ROOT)/deps/cgltf
SAN_FLAGS := -fsanitize=$(SANITIZE) -fno-omit-frame-pointer -g
$(UNIT_BIN): $(UNIT_SRCS) $(UNIT_HDRS) $(LIB_SRCS) FORCE | $(BUILD)
	$(CC) $(CFLAGS) $(SAN_INCS) $(SAN_FLAGS) $(UNIT_SRCS) $(LIB_SRCS) -o $@ $(LDLIBS_PLATFORM)
endif

clean:
	rm -rf $(BUILD)
