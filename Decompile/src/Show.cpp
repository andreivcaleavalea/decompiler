#include "Show.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <string>
#include <optional>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ASTDetail.h"
#include "GlobalVariableRecovery.h"
#include "Optimizations.h"
#include "llvm/Demangle/Demangle.h"

namespace Decompiler
{
namespace
{
    struct GlobalDeclarationInfo {
        ReferencedGlobal global;
        bool isArray           = false;
        size_t arrayLength     = 0;
        bool isFunctionPointer = false;
    };

    const GlobalMemoryRegion* findRegionForAddress(const std::vector<GlobalMemoryRegion>& regions, const uint64_t address)
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

    bool isZeroInitializedRegion(const GlobalMemoryRegion& region)
    {
        std::string name = region.sectionName;
        std::ranges::transform(name, name.begin(), [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return name.find("bss") != std::string::npos || name.find("common") != std::string::npos || name.find("zerofill") != std::string::npos;
    }

    std::optional<uint64_t> readUnsignedGlobalValue(
          const DecompileContext& context, const std::vector<GlobalMemoryRegion>& regions, const ReferencedGlobal& global)
    {
        if (!context.rawData || global.sizeBytes == 0 || global.sizeBytes > sizeof(uint64_t)) {
            return std::nullopt;
        }

        const auto* region = findRegionForAddress(regions, global.address);
        if (!region) {
            return std::nullopt;
        }

        const uint64_t offsetInRegion = global.address - region->start;
        const uint64_t fileOffset     = region->fileOffset + offsetInRegion;
        if (fileOffset > context.rawData->size() || global.sizeBytes > context.rawData->size() - fileOffset) {
            return std::nullopt;
        }

        uint64_t value = 0;
        for (size_t i = 0; i < global.sizeBytes; ++i) {
            value |= static_cast<uint64_t>((*context.rawData)[fileOffset + i]) << (i * 8);
        }
        return value;
    }

    std::vector<uint64_t> readUnsignedGlobalArray(
          const DecompileContext& context,
          const std::vector<GlobalMemoryRegion>& regions,
          const ReferencedGlobal& global,
          const size_t elementSize,
          const size_t length)
    {
        std::vector<uint64_t> values;
        values.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            ReferencedGlobal element = global;
            element.address          = global.address + i * elementSize;
            element.sizeBytes        = elementSize;
            const auto value         = readUnsignedGlobalValue(context, regions, element);
            if (!value.has_value()) {
                return {};
            }
            values.push_back(*value);
        }
        return values;
    }

    std::string globalTypeForSize(const size_t sizeBytes)
    {
        switch (sizeBytes) {
        case 1:
            return "uint8_t";
        case 2:
            return "uint16_t";
        case 4:
            return "int32_t";
        case 8:
            return "uint64_t";
        default:
            return "uint8_t";
        }
    }

    std::optional<size_t> parsePositiveIntegerAt(const std::string& line, size_t pos)
    {
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
            ++pos;
        }
        if (pos >= line.size() || std::isdigit(static_cast<unsigned char>(line[pos])) == 0) {
            return std::nullopt;
        }

        size_t end = pos;
        while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end])) != 0) {
            ++end;
        }

        size_t value         = 0;
        const auto [ptr, ec] = std::from_chars(line.data() + pos, line.data() + end, value);
        if (ec != std::errc() || ptr != line.data() + end) {
            return std::nullopt;
        }
        return value;
    }

    bool isIdentifierCharAt(const std::string& line, const size_t pos)
    {
        if (pos >= line.size()) {
            return false;
        }
        const unsigned char ch = static_cast<unsigned char>(line[pos]);
        return std::isalnum(ch) != 0 || line[pos] == '_';
    }

    bool containsSymbolToken(const std::string& line, const std::string& symbol, size_t& pos)
    {
        size_t searchFrom = 0;
        while ((pos = line.find(symbol, searchFrom)) != std::string::npos) {
            const bool leftOk  = pos == 0 || !isIdentifierCharAt(line, pos - 1);
            const bool rightOk = !isIdentifierCharAt(line, pos + symbol.size());
            if (leftOk && rightOk) {
                return true;
            }
            searchFrom = pos + symbol.size();
        }
        return false;
    }

    std::vector<GlobalDeclarationInfo> inferGlobalDeclarations(const std::vector<std::string>& lines, const GlobalSymbolTable& table)
    {
        std::vector<GlobalDeclarationInfo> declarations;
        const auto globals = collectReferencedGlobals(table);
        declarations.reserve(globals.size());

        for (const auto& global : globals) {
            GlobalDeclarationInfo info;
            info.global = global;

            for (const auto& line : lines) {
                size_t pos = 0;
                if (!containsSymbolToken(line, global.symbol, pos)) {
                    continue;
                }

                size_t after = pos + global.symbol.size();
                while (after < line.size() && std::isspace(static_cast<unsigned char>(line[after])) != 0) {
                    ++after;
                }

                if (after < line.size() && line[after] == '(') {
                    info.isFunctionPointer = true;
                }

                if (after < line.size() && line[after] == ',') {
                    if (const auto length = parsePositiveIntegerAt(line, after + 1); length.has_value() && *length > 1) {
                        info.isArray     = true;
                        info.arrayLength = std::max(info.arrayLength, *length);
                    }
                }

                const std::string assignmentPrefix = global.symbol + " = sub_";
                if (line.find(assignmentPrefix) != std::string::npos) {
                    info.isFunctionPointer = true;
                }
            }

            declarations.push_back(std::move(info));
        }

        return declarations;
    }

    std::string renderInitializerValue(
          const DecompileContext& context, const std::vector<GlobalMemoryRegion>& regions, const ReferencedGlobal& global, const size_t sizeBytes)
    {
        const auto* region = findRegionForAddress(regions, global.address);
        if (region && isZeroInitializedRegion(*region)) {
            return "0";
        }

        ReferencedGlobal read = global;
        read.sizeBytes        = sizeBytes;
        if (const auto value = readUnsignedGlobalValue(context, regions, read); value.has_value()) {
            return std::to_string(*value);
        }
        return {};
    }

    std::vector<std::string> renderGlobalDeclarations(
          const DecompileContext& context, const GlobalSymbolTable& table, const std::vector<std::string>& generatedLines)
    {
        const auto& regions = table.regions;
        std::vector<std::string> declarations;
        const auto globals = inferGlobalDeclarations(generatedLines, table);
        declarations.reserve(globals.size() + 1);

        for (const auto& declaration : globals) {
            const auto& global = declaration.global;
            if (declaration.isFunctionPointer) {
                const auto* region = findRegionForAddress(regions, global.address);
                std::string line   = "int (*" + global.symbol + ")(int)";
                if (region && isZeroInitializedRegion(*region)) {
                    line += " = nullptr";
                }
                line += ";";
                declarations.push_back(std::move(line));
                continue;
            }

            if (declaration.isArray) {
                constexpr size_t elementSize = 4;
                std::string line             = "int32_t " + global.symbol + "[" + std::to_string(declaration.arrayLength) + "]";
                const auto values            = readUnsignedGlobalArray(context, regions, global, elementSize, declaration.arrayLength);
                if (!values.empty()) {
                    line += " = {";
                    for (size_t i = 0; i < values.size(); ++i) {
                        if (i != 0) {
                            line += ", ";
                        }
                        line += std::to_string(values[i]);
                    }
                    line += "}";
                }
                line += ";";
                declarations.push_back(std::move(line));
                continue;
            }

            const size_t sizeBytes = global.sizeBytes == 0 ? 1 : global.sizeBytes;
            std::string line       = globalTypeForSize(sizeBytes) + " " + global.symbol;
            if (const auto initializer = renderInitializerValue(context, regions, global, sizeBytes); !initializer.empty()) {
                line += " = " + initializer;
            }

            line += ";";
            declarations.push_back(std::move(line));
        }

        if (!declarations.empty()) {
            declarations.emplace_back();
        }
        return declarations;
    }

    bool needsSemicolon(const std::string& line)
    {
        const auto first = std::find_if_not(line.begin(), line.end(), [](const unsigned char ch) { return std::isspace(ch) != 0; });
        if (first == line.end()) {
            return false;
        }

        const auto last = std::find_if_not(line.rbegin(), line.rend(), [](const unsigned char ch) { return std::isspace(ch) != 0; }).base() - 1;
        const std::string trimmed(first, last + 1);

        if (trimmed.ends_with(';') || trimmed.ends_with('{') || trimmed == "}" || trimmed == "} else {") {
            return false;
        }
        if (trimmed.starts_with("/*") || trimmed.starts_with("//")) {
            return false;
        }
        return true;
    }

    bool isRuntimeReachabilityStop(const std::string& functionName)
    {
        return functionName == "__main" || functionName == "mainCRTStartup" || functionName == "WinMainCRTStartup" ||
               functionName.starts_with("__do_global_") || functionName.starts_with("__tmainCRTStartup");
    }

    bool isUserEntryPointName(const std::string& functionName)
    {
        return functionName == "main" || functionName == "_main" || functionName == "wmain" || functionName == "_wmain" || functionName == "WinMain" ||
               functionName == "_WinMain" || functionName == "wWinMain" || functionName == "_wWinMain";
    }

    std::string renderFunctionHeader(const DecompilerFunction& function)
    {
        const std::string returnType = function.signature.returns_value ? "int" : "void";

        std::string params;
        for (size_t p = 0; p < function.signature.parameters.size(); ++p) {
            if (p != 0) {
                params += ", ";
            }
            params += "int " + function.signature.parameters[p].name;
        }
        std::string name    = llvm::demangle(function.name);
        const auto parenPos = name.find('(');
        if (parenPos != std::string::npos) {
            name = name.substr(0, parenPos);
        }
        return returnType + " " + name + "(" + params + ") {";
    }

    std::vector<const DecompilerFunction*> allFunctionPointers(const std::vector<DecompilerFunction>& functions)
    {
        std::vector<const DecompilerFunction*> result;
        result.reserve(functions.size());
        for (const auto& function : functions) {
            result.push_back(&function);
        }
        return result;
    }

    std::vector<const DecompilerFunction*> filterEntryPointReachableFunctions(
          const std::vector<DecompilerFunction>& functions, const uint64_t entryPointAddress)
    {
        if (entryPointAddress == 0) {
            return allFunctionPointers(functions);
        }

        auto entryIt = functions.end();
        for (auto it = functions.begin(); it != functions.end(); ++it) {
            const auto next = std::next(it);
            if (entryPointAddress >= it->start_address && (next == functions.end() || entryPointAddress < next->start_address)) {
                entryIt = it;
                break;
            }
        }
        if (entryIt == functions.end()) {
            return allFunctionPointers(functions);
        }

        if (isRuntimeReachabilityStop(entryIt->name)) {
            const auto userEntryIt = std::ranges::find_if(functions, [](const DecompilerFunction& function) { return isUserEntryPointName(function.name); });
            if (userEntryIt != functions.end()) {
                entryIt = userEntryIt;
            }
        }

        std::unordered_map<uint64_t, const DecompilerFunction*> functionByAddress;
        functionByAddress.reserve(functions.size());
        for (const auto& function : functions) {
            functionByAddress.emplace(function.start_address, &function);
        }

        std::vector<uint64_t> worklist{ entryIt->start_address };
        std::unordered_set<uint64_t> reachable;
        reachable.reserve(functions.size());

        for (size_t index = 0; index < worklist.size(); ++index) {
            const uint64_t address = worklist[index];
            if (!reachable.insert(address).second) {
                continue;
            }

            const auto functionIt = functionByAddress.find(address);
            if (functionIt == functionByAddress.end()) {
                continue;
            }
            if (address != entryIt->start_address && isRuntimeReachabilityStop(functionIt->second->name)) {
                continue;
            }

            for (const auto& instruction : functionIt->second->instructions) {
                if (!IRProperties::isCall(instruction.type)) {
                    continue;
                }
                const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).value);
                if (target.has_value() && functionByAddress.contains(*target)) {
                    if (isRuntimeReachabilityStop(functionByAddress.at(*target)->name)) {
                        continue;
                    }
                    worklist.push_back(*target);
                }
            }
        }

        std::vector<const DecompilerFunction*> selected;
        selected.reserve(reachable.size());
        for (const auto& function : functions) {
            if (reachable.contains(function.start_address)) {
                selected.push_back(&function);
            }
        }
        return selected;
    }
} // namespace

std::vector<std::string> showFunctions(DecompilerProgram& program)
{
    const auto selectedFunctions = program.context.onlyEntryPointReachable
                                         ? filterEntryPointReachableFunctions(program.functions, program.context.entryPointAddress)
                                         : allFunctionPointers(program.functions);

    Optimizations::removeUnusedGlobals(program.globals, selectedFunctions);

    std::vector<std::string> result;

    for (size_t i = 0; i < selectedFunctions.size(); ++i) {
        const auto& function = *selectedFunctions[i];
        result.push_back(renderFunctionHeader(function));

        if (function.ast.empty()) {
            bool hasReturn = false;
            for (const auto& instruction : function.instructions) {
                if (IRProperties::isReturn(instruction.type)) {
                    hasReturn = true;
                    break;
                }
            }
            if (hasReturn && function.signature.returns_value) {
                result.push_back("    return 0;");
            } else if (!hasReturn) {
                result.push_back("    /* no high-level statements recovered */");
            }
        } else {
            for (const auto& node : function.ast) {
                for (const auto& line : node->print(0)) {
                    const std::string normalizedLine = substituteArgumentRegisters(line, function.signature, function.calling_convention);
                    result.push_back("    " + normalizedLine + (needsSemicolon(normalizedLine) ? ";" : ""));
                }
            }
        }

        result.push_back("}");
        if (i + 1 < selectedFunctions.size()) {
            result.emplace_back();
        }
    }

    const auto declarations = renderGlobalDeclarations(program.context, program.globals, result);
    result.insert(result.begin(), declarations.begin(), declarations.end());

    return result;
}
} // namespace Decompiler
