#include "RemoveUnusedGlobals.h"

#include <string>
#include <unordered_set>

namespace Decompiler::Optimizations
{
void removeUnusedGlobals(GlobalSymbolTable& table, const std::vector<const DecompilerFunction*>& functions)
{
    std::unordered_set<std::string> usedSymbols;
    for (const auto* function : functions) {
        for (const auto& block : function->graph.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& operand : instruction.operands) {
                    if (operand.tag == OperandTag::GlobalVar && !operand.name.empty()) {
                        usedSymbols.insert(operand.name);
                    }
                }
            }
        }
    }

    auto it = table.referenced.begin();
    while (it != table.referenced.end()) {
        if (!usedSymbols.contains(it->second.symbol)) {
            it = table.referenced.erase(it);
        } else {
            ++it;
        }
    }
}
} // namespace Decompiler::Optimizations
