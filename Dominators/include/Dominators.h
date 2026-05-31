#pragma once

#include <set>
#include <vector>

#include "ControlGraphFlow.h"

namespace Decompiler::Dominators
{

std::vector<std::set<size_t>> computeDominators(const Graph& cfg);
std::vector<std::set<size_t>> computePostDominators(const Graph& cfg);
std::vector<size_t> computeImmediateDominators(const std::vector<std::set<size_t>>& doms);
std::vector<size_t> computeImmediatePostDominators(const std::vector<std::set<size_t>>& postdoms);
std::vector<std::vector<size_t>> buildDominatorTreeChildren(const std::vector<size_t>& idom);
std::vector<std::set<size_t>> computeDominanceFrontier(const Graph& cfg, const std::vector<size_t>& idom);

} // namespace Decompiler::Dominators
