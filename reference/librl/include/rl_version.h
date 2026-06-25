#ifndef RL_VERSION_H
#define RL_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define RL_VERSION_MAJOR 0
#define RL_VERSION_MINOR 0
#define RL_VERSION_PATCH 1
#define RL_VERSION_LABEL "dev"
/* Set to 0 when RL_VERSION_LABEL is "" (release builds without a suffix). */
#define RL_VERSION_HAS_LABEL 1

#define RL_VERSION_NUMBER \
    ((unsigned)((RL_VERSION_MAJOR * 1000000) + (RL_VERSION_MINOR * 1000) + (RL_VERSION_PATCH)))

#define RL_STRINGIFY_(x) #x
#define RL_STRINGIFY(x) RL_STRINGIFY_(x)

#define RL_VERSION_CORE \
    RL_STRINGIFY(RL_VERSION_MAJOR) "." RL_STRINGIFY(RL_VERSION_MINOR) "." RL_STRINGIFY(RL_VERSION_PATCH)

#if RL_VERSION_HAS_LABEL
#define RL_VERSION_STRING RL_VERSION_CORE "-" RL_VERSION_LABEL
#else
#define RL_VERSION_STRING RL_VERSION_CORE
#endif

int rl_version_major(void);
int rl_version_minor(void);
int rl_version_patch(void);
const char *rl_version_label(void);
unsigned rl_version_number(void);
const char *rl_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* RL_VERSION_H */
