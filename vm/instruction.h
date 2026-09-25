/*
 * ============================================================================
 * Cieto Virtual Machine Instruction Set Architecture (ISA)
 * ============================================================================
 *
 * The Cieto VM utilizes a Register-Based, Fixed-Width (32-bit) Instruction Set.
 * 
 * ----------------------------------------------------------------------------
 * 1. Fixed-Width (32-bit): Every instruction is exactly 4 bytes. 
 *
 * 2. Register-Based: Instructions operate directly on a "window" of virtual 
 * registers located in the call stack.
 *
 * 3. Little-Endian: The OpCode is placed in the lowest 8 bits.
 *
 * INSTRUCTION :
 * ----------------------------------------------------------------------------
 * The 32-bit instruction is divided into fields. There are three main patterns
 * used to interpret these bits, depending on the specific OpCode.
 *
 * Field Positions:
 * 31      24 23      16 15       8 7        0
 * +---------+---------+---------+---------+
 * |    C    |    B    |    A    |  OpCode |
 * +---------+---------+---------+---------+
 * Bits:  8 bits    8 bits    8 bits    8 bits
 *
 * 1. iABC (Standard):
 * Used for arithmetic, logic, and register moves.
 * [ OpCode ] 8 bits:  The operation command.
 * [    A   ] 8 bits:  Target Register index (0-255).
 * [    B   ] 8 bits:  Source 1 (Register index or Constant index).
 * [    C   ] 8 bits:  Source 2 (Register index or Constant index).
 * > Example: ADD R(A), R(B), R(C)
 *
 * 2. iABx (Large Operand):
 * Used when a larger range is needed.
 * Fields B and C are merged into a single 16-bit field called "Bx".
 * [ OpCode ] 8 bits
 * [    A   ] 8 bits
 * [   Bx   ] 16 bits: Unsigned integer (0 - 65535).
 * > Example: LOADK R(A), Const(Bx)
 *
 * 3. Type iAsBx (Signed Jump):
 * Used for control flow (jumps, loops) where the offset can be negative.
 * The 16-bit Bx field is interpreted as a signed integer "sBx".
 * sBx = Bx - BIAS (where BIAS is typically MAX_BX >> 1).
 * > Example: JMP sBx
 *
 * FIELD DEFINITIONS:
 * -----------------------------------------------------------------------------
 * - OpCode (0-7):   The instruction ID.
 * - A      (8-15):  Usually the destination register (R[A]).
 * - B      (16-23): First operand (R[B] or constant).
 * - C      (24-31): Second operand (R[C] or constant).
 * - Bx     (16-31): Combined B and C for larger unsigned values.
 * - sBx    (16-31): Combined B and C for signed jump offsets.
 *
 * MACRO:
 * -----------------------------------------------------------------------------
 * To pack/unpack these values efficiently, Cieto use bitwise operations:
 * - Packing: (val << POS) & MASK
 * - Unpacking: (instruction >> POS) & MASK
 *
**/

#ifndef CIETO_INSTRUCTION_H
#define CIETO_INSTRUCTION_H

#include <stdint.h>
#include "common.h"

typedef uint32_t Instruction;

typedef enum{
    OP_MOVE,        // R[A] <= R[B]
    OP_LOADK,       // R[A] <= K[Bx]
    OP_LOADBOOL,    // R[A] <= (B != 0)
    OP_LOADNULL,    // R[A], R[A+1], ..., R[A+B-1] <= null

    OP_GET_GLOBAL, // R[A] <= GlobalEnv[Bx]
    OP_SET_GLOBAL, // GlobalEnv[Bx] <= R[A]

    OP_GET_UPVAL,  // R[A] <= Upv[B]
    OP_SET_UPVAL,  // Upv[B] <= R[A]
    OP_CLOSE_UPVAL,

    OP_GET_INDEX, // R[A] <= R[B][R[C]]
    OP_SET_INDEX, // R[A][R[B]] <= R[C]

    OP_GET_PROPERTY, // R[A] <= R[B].K[C]
    OP_SET_PROPERTY, // R[A].K[B] <= R[C]

    OP_FIELD,

    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,

    OP_NOT, OP_NEG,

    OP_EQ, OP_LT, OP_LE,

    OP_JMP,
    OP_JMP_IF_FALSE,  // R[A] is condition
    OP_JMP_IF_TRUE,   // R[A] is condition
    OP_CALL,
    OP_PREP_INVOKE,   // prepare R[A].K[C] before evaluating call arguments
    OP_INVOKE,        // R[A] <= prepared call(R[A+1], ..., R[A+B])
    OP_TAILCALL,
    OP_DEFER,
    OP_SYSTEM,
    OP_RETURN,

    OP_CLOSURE,

    OP_CLASS,
    OP_METHOD,

    OP_BUILD_LIST,
    OP_BUILD_MAP,
    OP_INIT_LIST,
    OP_FILL_LIST,
    OP_SLICE,
    OP_TO_STRING,
    OP_CONCAT,      // R[A] <= concat(R[B], ..., R[B+C-1])

    OP_IMPORT,

    OP_FOREACH,

    OP_PRINT,
} OpCode;

#define SIZE_OP     8
#define SIZE_A      8
#define SIZE_B      8
#define SIZE_C      8
#define SIZE_BX     (SIZE_C + SIZE_B)

#define POS_OP      0
#define POS_A       (POS_OP + SIZE_OP)
#define POS_B       (POS_A + SIZE_A)
#define POS_C       (POS_B + SIZE_B)
#define POS_BX      POS_B

#define MASK_OP     ((1 << SIZE_OP) - 1)
#define MASK_A      ((1 << SIZE_A) - 1)
#define MASK_B      ((1 << SIZE_B) - 1)
#define MASK_C      ((1 << SIZE_C) - 1)
#define MASK_BX     ((1 << SIZE_BX) - 1)

// Getters
#define GET_OPCODE(i)   ((OpCode)((i >> POS_OP) & MASK_OP))
#define GET_ARG_A(i)    ((int)((i >> POS_A) & MASK_A))
#define GET_ARG_B(i)    ((int)((i >> POS_B) & MASK_B))
#define GET_ARG_C(i)    ((int)((i >> POS_C) & MASK_C))
#define GET_ARG_Bx(i)   ((int)((i >> POS_BX) & MASK_BX))

// Signed Bx
#define MAX_BX          MASK_BX
#define MAX_ARG_BX      65535
#define OFFSET_sBx      (MAX_BX >> 1)
#define GET_ARG_sBx(i)  (GET_ARG_Bx(i) - OFFSET_sBx)

// Constructors
#define CREATE_ABC(op, a, b, c) \
    ((Instruction)(((op) & MASK_OP) << POS_OP) | \
    ((Instruction)((a) & MASK_A) << POS_A) | \
    ((Instruction)((b) & MASK_B) << POS_B) | \
    ((Instruction)((c) & MASK_C) << POS_C))

#define CREATE_ABx(op, a, bx) \
    ((Instruction)(((op) & MASK_OP) << POS_OP) | \
    ((Instruction)((a) & MASK_A) << POS_A) | \
    ((Instruction)((bx) & MASK_BX) << POS_BX))

#define CREATE_AsBx(op, a, sbx) \
    (CREATE_ABx((op), (a), ((sbx) + OFFSET_sBx)))

#endif // CIETO_INSTRUCTION_H
