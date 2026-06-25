#include "rl_version.h"

#include "internal/exports.h"

RL_KEEP
int rl_version_major(void) {
    return RL_VERSION_MAJOR;
}

RL_KEEP
int rl_version_minor(void) {
    return RL_VERSION_MINOR;
}

RL_KEEP
int rl_version_patch(void) {
    return RL_VERSION_PATCH;
}

RL_KEEP
const char *rl_version_label(void) {
    return RL_VERSION_LABEL;
}

RL_KEEP
unsigned rl_version_number(void) {
    return RL_VERSION_NUMBER;
}

RL_KEEP
const char *rl_version_string(void) {
    return RL_VERSION_STRING;
}
