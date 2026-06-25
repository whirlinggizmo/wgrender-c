#include "sk_version.h"

#include "internal/exports.h"

SK_KEEP
int sk_version_major(void) {
    return SK_VERSION_MAJOR;
}

SK_KEEP
int sk_version_minor(void) {
    return SK_VERSION_MINOR;
}

SK_KEEP
int sk_version_patch(void) {
    return SK_VERSION_PATCH;
}

SK_KEEP
const char *sk_version_label(void) {
    return SK_VERSION_LABEL;
}

SK_KEEP
unsigned sk_version_number(void) {
    return SK_VERSION_NUMBER;
}

SK_KEEP
const char *sk_version_string(void) {
    return SK_VERSION_STRING;
}
