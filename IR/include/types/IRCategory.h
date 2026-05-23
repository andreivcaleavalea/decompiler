#pragma once

namespace Decompiler
{
enum class IRCategory {
    Data,       // MOV, STORE, LEA, SEXT, SETCC
    Arithmetic, // ADD, SUB, MUL, DIV, NEG
    Bitwise,    // AND, OR, XOR, NOT, SHL, SHR, SAR
    Flag,       // CMP, TEST
    Jump,       // JMP, CJMP
    Call,       // CALL
    Return,     // RET
    Stack,      // PUSH, POP
    Unknown     // UNKNOWN
};
}
