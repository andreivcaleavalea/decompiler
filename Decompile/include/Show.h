#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Program.h"

namespace Decompiler
{
struct RenderedFunction {
    uint64_t startAddress;
    std::vector<std::string> lines;
};

struct RenderedProgram {
    std::vector<std::string> globalDeclarations;
    std::vector<RenderedFunction> functions;
};

std::vector<std::string> showFunctions(DecompilerProgram& program);
RenderedProgram showFunctionsPerFunction(DecompilerProgram& program);
} // namespace Decompiler
