#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ConditionCode.h"
#include "IROperand.h"
#include "IRType.h"

#include <iomanip>
#include <sstream>

namespace Decompiler
{
struct IRInstruction {
    std::string op;
    IRType type = IRType::UNKNOWN;
    std::vector<IROperand> operands;
    ConditionCode condition = ConditionCode::None;
    uint64_t address        = 0;

    IRInstruction(
          std::string opValue,
          IRType typeValue,
          std::vector<IROperand> operandValues,
          uint64_t addressValue,
          ConditionCode conditionValue = ConditionCode::None)
        : op(std::move(opValue)), type(typeValue), operands(std::move(operandValues)), condition(conditionValue), address(addressValue)
    {
    }

    std::string toString() const
    {
        std::ostringstream out;

        out << "0x" << std::hex << std::setw(8) << std::setfill('0') << address << std::dec;
        out << ": " << typeToString(type);

        if (condition != ConditionCode::None) {
            out << " [cond=" << conditionToOperator(condition) << "]";
        }

        for (size_t i = 0; i < operands.size(); ++i) {
            out << (i == 0 ? " " : ", ");
            const auto& op = operands[i];
            out << (op.value.empty() ? "<empty>" : op.value);
        }

        return out.str();
    }
};
} // namespace Decompiler
