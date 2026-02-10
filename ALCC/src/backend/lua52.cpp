#define LUA_CORE

#include "../core/alcc_backend.h"
extern "C" {
#include "lopcodes.h"
// luaP_opnames is declared in lopcodes.h
}
#include <string.h>

static AlccOpMode map_mode(enum OpMode m) {
    switch(m) {
        case iABC: return ALCC_iABC;
        case iABx: return ALCC_iABx;
        case iAsBx: return ALCC_iAsBx;
        case iAx: return ALCC_iAx;
        default: return ALCC_iABC;
    }
}

static int lua52_get_op_count(void) {
    return NUM_OPCODES;
}

static const AlccOpInfo* lua52_get_op_info(int op) {
    static AlccOpInfo cache[NUM_OPCODES];
    static int initialized = 0;

    if (!initialized) {
        for (int i=0; i<NUM_OPCODES; i++) {
            cache[i].name = luaP_opnames[i];
            cache[i].mode = map_mode(getOpMode(i));
            // Lua 5.2 doesn't have k field in iABC
            cache[i].has_k = 0;
        }
        initialized = 1;
    }

    if (op < 0 || op >= NUM_OPCODES) return NULL;
    return &cache[op];
}

static const char* lua52_get_op_name(int op) {
    const AlccOpInfo* info = lua52_get_op_info(op);
    return info ? info->name : NULL;
}

static void lua52_decode(uint32_t raw, AlccInstruction* out) {
    Instruction i = (Instruction)raw;
    OpCode op = GET_OPCODE(i);
    out->op = op;
    out->a = GETARG_A(i);
    out->b = 0;
    out->c = 0;
    out->k = 0;
    out->bx = 0;

    enum OpMode m = getOpMode(op);
    switch (m) {
        case iABC:
            out->b = GETARG_B(i);
            out->c = GETARG_C(i);
            break;
        case iABx:
            out->bx = GETARG_Bx(i);
            break;
        case iAsBx:
            out->bx = GETARG_sBx(i);
            break;
        case iAx:
            out->bx = GETARG_Ax(i);
            break;
    }
}

static uint32_t lua52_encode(const AlccInstruction* in) {
    OpCode op = (OpCode)in->op;
    Instruction i = 0;
    SET_OPCODE(i, op);
    SETARG_A(i, in->a);

    enum OpMode m = getOpMode(op);
    switch (m) {
        case iABC:
            SETARG_B(i, in->b);
            SETARG_C(i, in->c);
            break;
        case iABx:
            SETARG_Bx(i, in->bx);
            break;
        case iAsBx:
            SETARG_sBx(i, in->bx);
            break;
        case iAx:
            SETARG_Ax(i, in->bx);
            break;
    }
    return (uint32_t)i;
}

AlccBackend alcc_lua52_backend = {
    "Lua 5.2",
    lua52_get_op_count,
    lua52_get_op_info,
    lua52_get_op_name,
    lua52_decode,
    lua52_encode
};
