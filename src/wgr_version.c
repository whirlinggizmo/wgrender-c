#include "wgr_version.h"

#include "internal/exports_internal.h"

WGR_KEEP
int wgr_version_major(void) {
    return WGR_VERSION_MAJOR;
}

WGR_KEEP
int wgr_version_minor(void) {
    return WGR_VERSION_MINOR;
}

WGR_KEEP
int wgr_version_patch(void) {
    return WGR_VERSION_PATCH;
}

WGR_KEEP
const char *wgr_version_label(void) {
    return WGR_VERSION_LABEL;
}

WGR_KEEP
unsigned wgr_version_number(void) {
    return WGR_VERSION_NUMBER;
}

WGR_KEEP
const char *wgr_version_string(void) {
    return WGR_VERSION_STRING;
}
