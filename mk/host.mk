# The host's native target name, so build directories name the OS they are for
# (build/linux, build/macos) the way build/windows and build/webgl2 already do.
# "desktop" in prose and in DESKTOP=1 still means native-not-web, whichever OS.
UNAME_S ?= $(shell uname -s)
HOST_OS := $(if $(filter Linux,$(UNAME_S)),linux,$(if $(filter Darwin,$(UNAME_S)),macos,$(shell echo $(UNAME_S) | tr A-Z a-z)))
