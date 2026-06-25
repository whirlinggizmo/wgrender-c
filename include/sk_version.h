#ifndef SK_VERSION_H
#define SK_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define SK_VERSION_MAJOR 0
#define SK_VERSION_MINOR 0
#define SK_VERSION_PATCH 1
#define SK_VERSION_LABEL "dev"
/* Set to 0 when SK_VERSION_LABEL is "" (release builds without a suffix). */
#define SK_VERSION_HAS_LABEL 1

#define SK_VERSION_NUMBER \
    ((unsigned)((SK_VERSION_MAJOR * 1000000) + (SK_VERSION_MINOR * 1000) + (SK_VERSION_PATCH)))

#define SK_STRINGIFY_(x) #x
#define SK_STRINGIFY(x) SK_STRINGIFY_(x)

#define SK_VERSION_CORE \
    SK_STRINGIFY(SK_VERSION_MAJOR) "." SK_STRINGIFY(SK_VERSION_MINOR) "." SK_STRINGIFY(SK_VERSION_PATCH)

#if SK_VERSION_HAS_LABEL
#define SK_VERSION_STRING SK_VERSION_CORE "-" SK_VERSION_LABEL
#else
#define SK_VERSION_STRING SK_VERSION_CORE
#endif

int sk_version_major(void);
int sk_version_minor(void);
int sk_version_patch(void);
const char *sk_version_label(void);
unsigned sk_version_number(void);
const char *sk_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* SK_VERSION_H */
