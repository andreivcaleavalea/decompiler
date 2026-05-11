#pragma once

namespace Decompiler {
    enum class IRType {
        ASSIGN,
        LOAD,
        LOAD_CONST,
        STORE,
        LEA,
        SEXT,

        ADD,
        SUB,
        MUL,
        DIV,
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
}
