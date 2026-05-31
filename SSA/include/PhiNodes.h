#pragma once

#include "ControlGraphFlow.h"

#include <set>
#include <vector>

namespace Decompiler
{
void insertPhiInstructions(Graph& cfg, std::vector<std::set<size_t>>& dominanceFrontier);
void replacePhiInstructionsWithCopies(Graph& cfg);
} // namespace Decompiler
