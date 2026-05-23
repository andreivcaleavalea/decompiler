#pragma once

#include <string>
#include <unordered_map>

#include "Program.h"

namespace Decompiler
{
void buildFunctionIR(DecompilerFunction& function, DecompilerProgram& program, const std::unordered_map<uint64_t, std::string>& functionNames);

void buildFunctionAST(DecompilerFunction& function);
} // namespace Decompiler
