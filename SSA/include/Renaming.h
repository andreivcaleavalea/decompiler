#pragma once

#include "ControlGraphFlow.h"

#include <vector>

namespace Decompiler
{
void renameVariables(Graph& cfg, std::vector<size_t>& immediateDominators, std::vector<std::vector<size_t>>& dominatorTreeChildren);
} // namespace Decompiler
