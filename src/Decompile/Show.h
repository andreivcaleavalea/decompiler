#pragma once

#include <string>
#include <vector>

#include "DecompilerContext.h"
#include "Functions.h"

namespace Decompiler {
    std::vector<std::string> showFunctions(const std::vector<DecompilerFunction>& functions);
    std::vector<std::string> showFunctions(const std::vector<DecompilerFunction>& functions, const DecompileContext& context);
}
