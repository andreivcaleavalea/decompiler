#pragma once

#include <vector>

#include "DecompilerTypes.h"
#include "IRInstruction.h"

namespace Decompiler
{
std::vector<IRInstruction> transformASMToIR(const std::vector<AssemblyInstruction>& instructions);
}
