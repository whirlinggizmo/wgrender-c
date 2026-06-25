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

.PHONY: all examples run clean print-ldlibs check

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

# Enforce project invariants: no backend (sokol) leakage into the public
# surface, and the naming conventions in AGENTS.md.
check:
	@tools/check_no_backend_leak.sh
	@tools/check_naming.sh

clean:
	rm -rf $(BUILD) $(LIBDIR) $(EX_BUILD)
