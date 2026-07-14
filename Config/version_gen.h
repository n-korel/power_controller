#ifndef VERSION_GEN_H
#define VERSION_GEN_H

/* Fallback defaults for host tests / IDE without a generated build/version_gen.h.
 * Firmware builds override these via Makefile-generated build/version_gen.h
 * (placed earlier on the include path with -I$(BUILD_DIR)). */

#ifndef FW_GIT_HASH_STR
#define FW_GIT_HASH_STR "00000000"
#endif

#ifndef FW_GIT_DIRTY
#define FW_GIT_DIRTY 0U
#endif

#ifndef FW_BUILD_EPOCH
#define FW_BUILD_EPOCH 0UL
#endif

#endif /* VERSION_GEN_H */
