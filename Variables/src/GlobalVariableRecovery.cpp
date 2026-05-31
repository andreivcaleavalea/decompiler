#include "GlobalVariableRecovery.h"

#include <algorithm>
#include <sstream>
#include <string>

namespace Decompiler
{
namespace
{
    std::string sanitizeSymbolPart(std::string value)
    {
        for (char& ch : value) {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (std::isalnum(c) == 0) {
                ch = '_';
            } else {
                ch = static_cast<char>(std::tolower(c));
            }
        }
        while (!value.empty() && value.front() == '_') {
            value.erase(value.begin());
        }
        return value.empty() ? "mem" : value;
    }

    std::string hexAddress(const uint64_t address)
    {
        std::ostringstream oss;
        oss << std::hex << address;
        return oss.str();
    }

    const GlobalMemoryRegion* findRegion(const std::vector<GlobalMemoryRegion>& regions, const uint64_t address)
    {
        for (const auto& region : regions) {
            if (region.size == 0) {
                continue;
            }
            if (address >= region.start && address - region.start < region.size) {
                return &region;
            }
        }
        return nullptr;
    }

    std::string resolveGlobalSymbol(GlobalSymbolTable& table, const uint64_t address, const size_t sizeBytes)
    {
        const auto* region = findRegion(table.regions, address);
        if (region == nullptr) {
            return {};
        }

        const auto namedIt = table.namedGlobals.find(address);
        const std::string symbol =
              (namedIt != table.namedGlobals.end()) ? namedIt->second : "global_" + sanitizeSymbolPart(region->sectionName) + "_" + hexAddress(address);

        auto& reference     = table.referenced[address];
        reference.address   = address;
        reference.symbol    = symbol;
        reference.sizeBytes = std::max(reference.sizeBytes, sizeBytes);
        return symbol;
    }

    bool isGlobalMemoryOperand(const IROperand& operand)
    {
        return operand.isHeapDeref() && operand.heapDeref.absolute.has_value();
    }

    void convertOperand(IROperand& operand, GlobalSymbolTable& table)
    {
        if (!isGlobalMemoryOperand(operand)) {
            return;
        }

        const uint64_t address   = *operand.heapDeref.absolute;
        const size_t sizeBytes   = operand.heapDeref.sizeBytes;
        const std::string symbol = resolveGlobalSymbol(table, address, sizeBytes);
        if (symbol.empty()) {
            return;
        }

        operand.tag       = OperandTag::GlobalVar;
        operand.name      = symbol;
        operand.sizeBytes = sizeBytes;
        operand.heapDeref = {};
    }

    bool instructionHasXmmOperand(const IRInstruction& instruction)
    {
        for (const auto& operand : instruction.operands) {
            if (operand.isAnyReg() && operand.name.starts_with("xmm")) {
                return true;
            }
        }
        return false;
    }
} // namespace

void recoverGlobalVariables(Graph& cfg, GlobalSymbolTable& table)
{
    for (auto& block : cfg.blocks) {
        for (auto& instruction : block.instructions) {
            const bool floatContext = instructionHasXmmOperand(instruction);
            for (auto& operand : instruction.operands) {
                if (!isGlobalMemoryOperand(operand)) {
                    continue;
                }
                const uint64_t address = *operand.heapDeref.absolute;
                convertOperand(operand, table);
                if (floatContext) {
                    const auto it = table.referenced.find(address);
                    if (it != table.referenced.end()) {
                        it->second.isFloat = true;
                    }
                }
            }
        }
    }
}

std::vector<ReferencedGlobal> collectReferencedGlobals(const GlobalSymbolTable& table)
{
    std::vector<ReferencedGlobal> globals;
    globals.reserve(table.referenced.size());
    for (const auto& [_, global] : table.referenced) {
        globals.push_back(global);
    }
    return globals;
}

GlobalSymbolTable buildGlobalSymbolTable(const DecompileContext& context, const std::unordered_map<uint64_t, std::string>& namedGlobals)
{
    GlobalSymbolTable table;
    table.regions.reserve(context.sections.size());
    for (const auto& section : context.sections) {
        if (section.isExecutable || section.size == 0) {
            continue;
        }
        table.regions.push_back({ section.virtualAddress, section.size, section.offset, section.name });
    }
    table.namedGlobals = namedGlobals;
    return table;
}
} // namespace Decompiler
