#pragma once

#include <vector>

#include "Functions.h"
#include "GlobalVariableRecovery.h"

namespace Decompiler::Optimizations
{
void removeUnusedGlobals(GlobalSymbolTable& table, const std::vector<const DecompilerFunction*>& functions);
}
