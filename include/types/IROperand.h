#pragma once

#include <optional>
#include <string>

#include "IRMemoryAddress.h"

namespace Decompiler {
    enum class OpType {
        REG,
        IMM,
        MEM
    };

    struct IROperand {
        OpType type = OpType::REG;
        std::string value;
        std::optional<IRMemoryAddress> memory;
    };
}
