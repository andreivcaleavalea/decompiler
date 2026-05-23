#pragma once

#include "ControlGraphFlow.h"

namespace Decompiler::Optimizations
{
bool canPropagate(const IRInstruction& instruction);
size_t countUsesOfName(const Graph& cfg, const std::string& name);
bool replaceFirstUseOfName(Graph& cfg, const std::string& name, const IROperand& replacement);
bool propagateOnce(Graph& cfg);
void propagateCopies(Graph& cfg);
} // namespace Decompiler::Optimizations
