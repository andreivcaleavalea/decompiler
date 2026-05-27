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

    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
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

inline std::string typeToString(const IRType type)
{
    switch (type) {
    case IRType::ASSIGN:
        return "ASSIGN";
    case IRType::LOAD:
        return "LOAD";
    case IRType::LOAD_CONST:
        return "LOAD_CONST";
    case IRType::STORE:
        return "STORE";
    case IRType::LEA:
        return "LEA";
    case IRType::SEXT:
        return "SEXT";
    case IRType::ADD:
        return "ADD";
    case IRType::SUB:
        return "SUB";
    case IRType::MUL:
        return "MUL";
    case IRType::DIV:
        return "DIV";
    case IRType::MOD:
        return "MOD";
    case IRType::NEG:
        return "NEG";
    case IRType::AND:
        return "AND";
    case IRType::OR:
        return "OR";
    case IRType::XOR:
        return "XOR";
    case IRType::NOT:
        return "NOT";
    case IRType::SHL:
        return "SHL";
    case IRType::SHR:
        return "SHR";
    case IRType::SAR:
        return "SAR";
    case IRType::CMP:
        return "CMP";
    case IRType::TEST:
        return "TEST";
    case IRType::SETCC:
        return "SETCC";
    case IRType::JMP:
        return "JMP";
    case IRType::CJMP:
        return "CJMP";
    case IRType::CALL:
        return "CALL";
    case IRType::RET:
        return "RET";
    case IRType::PUSH:
        return "PUSH";
    case IRType::POP:
        return "POP";
    case IRType::PHI:
        return "PHI";
    case IRType::NOP:
        return "NOP";
    case IRType::UNKNOWN:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}
} // namespace Decompiler
