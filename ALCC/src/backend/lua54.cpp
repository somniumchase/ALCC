#define LUA_CORE

#include "../core/alcc_backend.h"
extern "C" {
#include "lopcodes.h"
#include "lopnames.h"
}
#include <string.h>

static AlccOpMode map_mode(enum OpMode m) {
    switch(m) {
        case iABC: return ALCC_iABC;
        case iABx: return ALCC_iABx;
        case iAsBx: return ALCC_iAsBx;
        case iAx: return ALCC_iAx;
        case isJ: return ALCC_isJ;
        default: return ALCC_iABC;
    }
}

static int lua54_get_op_count(void) {
    return NUM_OPCODES;
}

static const AlccOpInfo* lua54_get_op_info(int op) {
    static AlccOpInfo cache[NUM_OPCODES];
    static int initialized = 0;

    if (!initialized) {
        for (int i=0; i<NUM_OPCODES; i++) {
            cache[i].name = opnames[i];
            cache[i].mode = map_mode(getOpMode(i));
            // In Lua 5.4, iABC can have k.
            // But strict iABC structure always has k field physically.
            // Whether it's used depends on opcode semantics, but for encoding/decoding we treat it as present.
            if (cache[i].mode == ALCC_iABC) {
                cache[i].has_k = 1;
            } else {
                cache[i].has_k = 0;
            }
        }
        initialized = 1;
    }

    if (op < 0 || op >= NUM_OPCODES) return NULL;
    return &cache[op];
}

static const char* lua54_get_op_name(int op) {
    const AlccOpInfo* info = lua54_get_op_info(op);
    return info ? info->name : NULL;
}

static void lua54_decode(uint32_t raw, AlccInstruction* out) {
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
            out->k = GETARG_k(i);
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
        case isJ:
            out->bx = GETARG_sJ(i);
            break;
    }
}

static uint32_t lua54_encode(const AlccInstruction* in) {
    OpCode op = (OpCode)in->op;
    Instruction i = 0;
    SET_OPCODE(i, op);
    SETARG_A(i, in->a);

    enum OpMode m = getOpMode(op);
    switch (m) {
        case iABC:
            SETARG_B(i, in->b);
            SETARG_C(i, in->c);
            SETARG_k(i, in->k);
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
        case isJ:
            SETARG_sJ(i, in->bx);
            break;
    }
    return (uint32_t)i;
}

AlccBackend alcc_lua54_backend = {
    "Lua 5.4",
    lua54_get_op_count,
    lua54_get_op_info,
    lua54_get_op_name,
    lua54_decode,
    lua54_encode
};
