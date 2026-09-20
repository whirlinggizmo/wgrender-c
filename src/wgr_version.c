#include "wgr_version.h"

#include "internal/exports_internal.h"

WGRI_KEEP
int wgr_version_major(void) {
    return WGR_VERSION_MAJOR;
}

WGRI_KEEP
int wgr_version_minor(void) {
    return WGR_VERSION_MINOR;
}

WGRI_KEEP
int wgr_version_patch(void) {
    return WGR_VERSION_PATCH;
}

WGRI_KEEP
const char *wgr_version_label(void) {
    return WGR_VERSION_LABEL;
}

WGRI_KEEP
unsigned wgr_version_number(void) {
    return WGR_VERSION_NUMBER;
}

WGRI_KEEP
const char *wgr_version_string(void) {
    return WGR_VERSION_STRING;
}
