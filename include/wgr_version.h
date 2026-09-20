#ifndef WGR_VERSION_H
#define WGR_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define WGR_VERSION_MAJOR 0
#define WGR_VERSION_MINOR 0
#define WGR_VERSION_PATCH 1
#define WGR_VERSION_LABEL "dev"
/* Set to 0 when WGR_VERSION_LABEL is "" (release builds without a suffix). */
#define WGR_VERSION_HAS_LABEL 1

#define WGR_VERSION_NUMBER \
    ((unsigned)((WGR_VERSION_MAJOR * 1000000) + (WGR_VERSION_MINOR * 1000) + (WGR_VERSION_PATCH)))

#define WGR_STRINGIFY_(x) #x
#define WGR_STRINGIFY(x) WGR_STRINGIFY_(x)

#define WGR_VERSION_CORE \
    WGR_STRINGIFY(WGR_VERSION_MAJOR) "." WGR_STRINGIFY(WGR_VERSION_MINOR) "." WGR_STRINGIFY(WGR_VERSION_PATCH)

#if WGR_VERSION_HAS_LABEL
#define WGR_VERSION_STRING WGR_VERSION_CORE "-" WGR_VERSION_LABEL
#else
#define WGR_VERSION_STRING WGR_VERSION_CORE
#endif

int wgr_version_major(void);
int wgr_version_minor(void);
int wgr_version_patch(void);
const char *wgr_version_label(void);
unsigned wgr_version_number(void);
const char *wgr_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* WGR_VERSION_H */
