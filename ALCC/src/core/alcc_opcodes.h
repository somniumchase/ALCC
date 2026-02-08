#ifndef ALCC_OPCODES_H
#define ALCC_OPCODES_H

#include "lua.h"

// Abstract OpCodes Modes (matches Lua 5.5 structure)
typedef enum {
    ALCC_iABC,
    ALCC_ivABC,
    ALCC_iABx,
    ALCC_iAsBx,
    ALCC_iAx,
    ALCC_isJ
} AlccOpMode;

typedef struct {
    const char* name;
    AlccOpMode mode;
    int has_k; // Flag for 'k' argument
} AlccOpInfo;

// Get number of opcodes
int alcc_get_num_opcodes(void);

// Get opcode info by value
const AlccOpInfo* alcc_get_op_info(int op);

// Get opcode name by value
const char* alcc_get_op_name(int op);

// Get op mode by value
AlccOpMode alcc_get_op_mode(int op);

#endif
