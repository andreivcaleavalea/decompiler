#pragma once

namespace Decompiler
{
enum class IRType {
    ASSIGN,
    LOAD,
    LOAD_CONST,
    STORE,
    LEA,
    SEXT,
    CONVERT,

    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    SMUL,
    SDIV,
    SMOD,
    NEG,

    AND,
    OR,
    XOR,
    NOT,
    SHL,
    SHR,
    SAR,

    CMP,
    TEST,
    SETCC,

    JMP,
    CJMP,
    CALL,
    RET,

    PUSH,
    POP,

    PHI,
    NOP,
    UNKNOWN
};
} // namespace Decompiler
