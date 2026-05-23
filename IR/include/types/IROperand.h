#pragma once

#include <optional>
#include <string>
#include <utility>

#include "IRMemoryAddress.h"

namespace Decompiler
{
enum class OpType { REG, IMM, MEM };

enum class OperandKind {
    RawRegister,
    SsaTemp,
    StackVar,
    GlobalVar,
};

struct IROperand {
    OpType type = OpType::REG;
    std::string value;
    std::optional<IRMemoryAddress> memory;
    OperandKind kind = OperandKind::RawRegister;
};

inline IROperand RegisterOperand(std::string value)
{
    return { OpType::REG, std::move(value), std::nullopt, OperandKind::RawRegister };
}

inline IROperand SsaTempOperand(std::string value)
{
    return { OpType::REG, std::move(value), std::nullopt, OperandKind::SsaTemp };
}

inline IROperand ImmediateOperand(std::string value)
{
    return { OpType::IMM, std::move(value), std::nullopt, OperandKind::RawRegister };
}

inline IROperand ImmediateOperand(const uint64_t value)
{
    return ImmediateOperand(std::to_string(value));
}
} // namespace Decompiler
