#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "DecompilerContext.h"

namespace Decompiler
{
struct DecompiledFunctionOutput {
    uint64_t startAddress;
    std::vector<std::string> lines;
};

struct DecompiledProgramOutput {
    std::vector<std::string> globalDeclarations;
    std::vector<DecompiledFunctionOutput> functions;
};

std::vector<std::string> Decompile(const std::vector<AssemblyInstruction>& input, const DecompileContext& context);
std::string DecompileToString(const std::vector<AssemblyInstruction>& input, const DecompileContext& context, size_t maxInstructions);
DecompiledProgramOutput DecompileFunctions(const std::vector<AssemblyInstruction>& input, const DecompileContext& context);
} // namespace Decompiler
