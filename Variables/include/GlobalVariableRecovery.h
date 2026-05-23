#pragma once

#include <map>
#include <unordered_map>
#include <vector>

#include "ControlGraphFlow.h"
#include "DecompilerContext.h"

namespace Decompiler
{
struct GlobalSymbolTable {
    std::vector<GlobalMemoryRegion> regions;
    std::unordered_map<uint64_t, std::string> namedGlobals;
    std::map<uint64_t, ReferencedGlobal> referenced;
};

GlobalSymbolTable buildGlobalSymbolTable(const DecompileContext& context, const std::unordered_map<uint64_t, std::string>& namedGlobals);
void recoverGlobalVariables(Graph& cfg, GlobalSymbolTable& table);
std::vector<ReferencedGlobal> collectReferencedGlobals(const GlobalSymbolTable& table);
} // namespace Decompiler
