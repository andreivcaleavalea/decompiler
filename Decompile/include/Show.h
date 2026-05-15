#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "DecompilerContext.h"
#include "Functions.h"

namespace Decompiler {
    std::vector<std::string> showFunctions(const std::vector<DecompilerFunction>& functions);
    std::vector<std::string> showFunctions(
        const std::vector<DecompilerFunction>& functions,
        const DecompileContext& context,
        const std::unordered_map<uint64_t, std::string>& globalSymbols = {});
}
