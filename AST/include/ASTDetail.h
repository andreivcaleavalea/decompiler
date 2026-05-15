#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "DecompilerContext.h"
#include "IRTypes.h"

namespace Decompiler::ASTDetail {
    void setGlobalMemoryRegions(std::vector<GlobalMemoryRegion> regions, std::unordered_map<uint64_t, std::string> namedGlobals = {});
    std::vector<ReferencedGlobal> referencedGlobals();
    std::string trimCopy(const std::string& value);
    bool parseSignedInteger(const std::string& raw, long long& out);
    bool toStackSymbol(const std::string& operand, std::string& symbol, StackFrameLayout layout = StackFrameLayout::Generic);
    std::string globalSymbolForAddress(uint64_t address, size_t sizeBytes = 0);
    std::string operandForDisplay(const IROperand& operand, StackFrameLayout layout = StackFrameLayout::Generic);
    std::string normalizeOperandForDisplay(const std::string& operand, StackFrameLayout layout = StackFrameLayout::Generic);
    std::string normalizeImmediateForDisplay(const std::string& operand);
    std::string invertComparisonOperator(const std::string& op);
    bool startsWithAny(const std::string& value, std::initializer_list<std::string_view> prefixes);
    bool isTemporaryName(const std::string& value);
    std::string normalizedValueForDisplay(const std::string& operand, StackFrameLayout layout = StackFrameLayout::Generic);
    std::string substituteTempFromDefinitions(const std::vector<IRInstruction>& instrs, size_t cmpIndex, const std::string& operand, StackFrameLayout layout = StackFrameLayout::Generic);
    std::string invertConditionExpr(const std::string& expr);
}
