#ifndef XXBYTE_YARV_TABLES_H
#define XXBYTE_YARV_TABLES_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_YARV

#define YARV_VERSION_COUNT 8u
#define YARV_SIGNATURE_COUNT 36u

typedef struct yarv_mnemonic {
    const char *name;
    uint16_t base;  /* id of the instruction without trace_ or zjit_ */
    uint16_t group; /* XXBYTE_GROUP_* */
} yarv_mnemonic;

typedef struct yarv_op {
    uint16_t id;       /* xxbyte_yarv_id */
    uint8_t signature; /* index into xxbyte_yarv_signatures */
} yarv_op;

typedef struct yarv_version {
    uint32_t mode;      /* XXBYTE_YARV_MODE(major, minor) */
    uint32_t count;     /* instructions */
    const yarv_op *ops; /* indexed by instruction number */
} yarv_version;

extern const yarv_mnemonic xxbyte_yarv_mnemonics[XXBYTE_YARV_ID_COUNT];
extern const char *const xxbyte_yarv_signatures[YARV_SIGNATURE_COUNT];
extern const yarv_version xxbyte_yarv_versions[YARV_VERSION_COUNT];

#endif

#endif
