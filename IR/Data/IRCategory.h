#pragma once

namespace Decompiler
{
enum class IRCategory {
    Data,       // ASSIGN, LOAD, LOAD_CONST, STORE, LEA, SEXT, SETCC
    Arithmetic, // ADD, SUB, MUL, DIV, MOD, SMUL, SDIV, SMOD, NEG
    Bitwise,    // AND, OR, XOR, NOT, SHL, SHR, SAR
    Flag,       // CMP, TEST
    Jump,       // JMP, CJMP
    Call,       // CALL
    Return,     // RET
    Stack,      // PUSH, POP
    Unknown     // PHI, NOP, UNKNOWN
};
}
