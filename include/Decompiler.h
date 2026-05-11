#pragma once

#include <vector>
#include <string>

#include "DecompilerContext.h"

namespace Decompiler {
    std::vector<std::string> Decompile(const std::vector<AssemblyInstruction>& input, const DecompileContext& context);
}
