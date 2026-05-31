#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "Register.h"

namespace Decompiler
{

enum class OperandTag {
    Immediate,
    Register,
    SsaTemp,
    StackVar,
    HeapDeref,
    GlobalVar,
    Label,
};

struct HeapDerefData {
    std::string base;
    std::string index;
    int32_t scale        = 1;
    int64_t displacement = 0;
    uint32_t sizeBytes   = 0;
    bool ripRelative     = false;
    std::optional<uint64_t> absolute;
};

struct IROperand {
    OperandTag tag = OperandTag::Register;
    std::string name;
    Register reg;
    int64_t stackOffset = 0;
    uint32_t sizeBytes  = 0;
    HeapDerefData heapDeref;

    std::string arrayIndex;
    int32_t arrayIndexScale = 1;

    bool isRegister() const
    {
        return tag == OperandTag::Register;
    }
    bool isSsaTemp() const
    {
        return tag == OperandTag::SsaTemp;
    }
    bool isAnyReg() const
    {
        return tag == OperandTag::Register || tag == OperandTag::SsaTemp;
    }
    bool isImmediate() const
    {
        return tag == OperandTag::Immediate;
    }
    bool isStackVar() const
    {
        return tag == OperandTag::StackVar;
    }
    bool isHeapDeref() const
    {
        return tag == OperandTag::HeapDeref;
    }
    bool isGlobalVar() const
    {
        return tag == OperandTag::GlobalVar;
    }
    bool isLabel() const
    {
        return tag == OperandTag::Label;
    }
    bool isMemory() const
    {
        return tag == OperandTag::HeapDeref || tag == OperandTag::StackVar;
    }
};

inline IROperand RegisterOperand(const std::string& regName)
{
    IROperand op;
    op.tag  = OperandTag::Register;
    op.reg  = registerFromName(regName);
    op.name = op.reg.name.empty() ? regName : op.reg.name;
    return op;
}

inline IROperand SsaTempOperand(const std::string& ssaName)
{
    IROperand op;
    op.tag  = OperandTag::SsaTemp;
    op.name = ssaName;
    return op;
}

inline IROperand ImmediateOperand(std::string value)
{
    IROperand op;
    op.tag  = OperandTag::Immediate;
    op.name = std::move(value);
    return op;
}

inline IROperand ImmediateOperand(const uint64_t value)
{
    return ImmediateOperand(std::to_string(value));
}

} // namespace Decompiler
