# libsk tests.
#
#   make test                  build lib/libsk.a if needed, build and run unit tests
#   make test TESTS=pick_      only tests whose name starts with "pick_"
#   make clean
#
# Unit tests (tests/unit/) call library internals directly and link against
# lib/libsk.a, with no stubs. They don't open a window or touch the GPU.

ROOT := ..

CC      ?= cc
STD     := -std=gnu11
WARN    := -Wall -Wextra -Wno-unused-parameter
OPT     := -O1 -g
DEFS    := -DSOKOL_GLCORE
INCS    := -I$(ROOT)/include -I$(ROOT)/src -Iunit -isystem $(ROOT)/deps/sokol
CFLAGS  := $(STD) $(WARN) $(OPT) $(DEFS) $(INCS)

LIB     := $(ROOT)/lib/libsk.a
# platform link flags: single source of truth in examples/Makefile
LDLIBS_PLATFORM := $(shell $(MAKE) --no-print-directory -s -C $(ROOT)/examples print-ldlibs)

BUILD      := build
UNIT_BIN   := $(BUILD)/unit_tests
UNIT_SRCS  := $(wildcard unit/*.c)
UNIT_HDRS  := $(wildcard unit/*.h)

.PHONY: test unit clean FORCE

test: unit

unit: $(UNIT_BIN)
	@./$(UNIT_BIN) $(TESTS)

# Keep the library current (the root build is a cheap no-op when nothing changed).
$(LIB): FORCE
	@$(MAKE) --no-print-directory -C $(ROOT)

$(BUILD):
	mkdir -p $(BUILD)

$(UNIT_BIN): $(UNIT_SRCS) $(UNIT_HDRS) $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(UNIT_SRCS) -o $@ $(LIB) $(LDLIBS_PLATFORM)

clean:
	rm -rf $(BUILD)
