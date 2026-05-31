#pragma once

#include <vector>
#include <string>

#include "DecompilerContext.h"

namespace Decompiler
{
std::vector<std::string> Decompile(const std::vector<AssemblyInstruction>& input, const DecompileContext& context);
std::string DecompileToString(const std::vector<AssemblyInstruction>& input, const DecompileContext& context, size_t maxInstructions);
} // namespace Decompiler
