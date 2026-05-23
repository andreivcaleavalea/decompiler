#pragma once

#include "DecompilerContext.h"
#include "Functions.h"
#include "GlobalVariableRecovery.h"

namespace Decompiler
{
struct DecompilerProgram {
    std::vector<DecompilerFunction> functions;
    GlobalSymbolTable globals;
    DecompileContext context;
};
} // namespace Decompiler
