#ifndef ALCC_BACKEND_H
#define ALCC_BACKEND_H

#include <stdint.h>

// Abstract OpCodes Modes
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
    int has_k;
} AlccOpInfo;

// Generic decoded instruction
typedef struct {
    int op;
    int a;
    int b;
    int c;
    int k;
    int bx; // or sbx/ax/sj (store raw value)
} AlccInstruction;

typedef struct AlccBackend {
    const char* name;

    int (*get_op_count)(void);
    const AlccOpInfo* (*get_op_info)(int op);
    const char* (*get_op_name)(int op);

    // Decode a raw 32-bit instruction into generic struct
    void (*decode_instruction)(uint32_t raw, AlccInstruction* out);

    // Encode generic struct into raw 32-bit instruction
    uint32_t (*encode_instruction)(const AlccInstruction* in);

} AlccBackend;

#endif
