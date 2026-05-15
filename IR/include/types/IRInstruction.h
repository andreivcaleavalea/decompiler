#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ConditionCode.h"
#include "IROperand.h"
#include "IRType.h"

namespace Decompiler {
    struct IRInstruction {
        std::string op;
        IRType type = IRType::UNKNOWN;
        std::vector<IROperand> operands;
        ConditionCode condition = ConditionCode::None;
        uint64_t address = 0;

        IRInstruction(std::string opValue, IRType typeValue, std::vector<IROperand> operandValues, uint64_t addressValue, ConditionCode conditionValue = ConditionCode::None)
            : op(std::move(opValue)), type(typeValue), operands(std::move(operandValues)), condition(conditionValue), address(addressValue) {}
    };
}
