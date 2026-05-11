#pragma once

#include <string>

#include "types/IROperand.h"
#include "types/IRProperties.h"

namespace Decompiler {
    IROperand parse_operand(const std::string &raw);
}
