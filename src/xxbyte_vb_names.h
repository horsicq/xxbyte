#ifndef XXBYTE_VB_NAMES_H
#define XXBYTE_VB_NAMES_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_VB

/* An intrinsic control class of the VB6 runtime: its name and the names of
 * its event-sink slots (indexes into xxbyte_vb_event_names, 0xFF for a slot
 * the class leaves empty). */
typedef struct xxbyte_vb_control_class {
    const char *name;
    uint32_t event_count;
    const uint8_t *events;
} xxbyte_vb_control_class;

#define XXBYTE_VB_EVENT_NAME_COUNT 60u
#define XXBYTE_VB_CONTROL_CLASS_COUNT 43u
#define XXBYTE_VB_MSVBVM60_ORDINAL_FIRST 100u
#define XXBYTE_VB_MSVBVM60_ORDINAL_COUNT 900u

extern const char *const xxbyte_vb_event_names[XXBYTE_VB_EVENT_NAME_COUNT];
extern const xxbyte_vb_control_class xxbyte_vb_control_classes[XXBYTE_VB_CONTROL_CLASS_COUNT];
/* Export names of MSVBVM60.DLL ordinals FIRST to FIRST + COUNT - 1 (NULL
 * where the ordinal has no name). */
extern const char *const xxbyte_vb_msvbvm60_ordinals[XXBYTE_VB_MSVBVM60_ORDINAL_COUNT];

#endif

#endif
