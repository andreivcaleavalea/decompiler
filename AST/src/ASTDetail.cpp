#include "ASTDetail.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace Decompiler::ASTDetail {
    namespace {
        std::vector<GlobalMemoryRegion> g_globalMemoryRegions;
        std::unordered_map<uint64_t, std::string> g_namedGlobals;
        std::map<uint64_t, ReferencedGlobal> g_referencedGlobals;

        std::string sanitizeSymbolPart(std::string value) {
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

        std::string hexAddress(const uint64_t address) {
            std::ostringstream oss;
            oss << std::hex << address;
            return oss.str();
        }
    }

    void setGlobalMemoryRegions(std::vector<GlobalMemoryRegion> regions, std::unordered_map<uint64_t, std::string> namedGlobals) {
        g_globalMemoryRegions = std::move(regions);
        g_namedGlobals = std::move(namedGlobals);
        g_referencedGlobals.clear();
    }

    std::vector<ReferencedGlobal> referencedGlobals() {
        std::vector<ReferencedGlobal> globals;
        globals.reserve(g_referencedGlobals.size());
        for (const auto& [_, global] : g_referencedGlobals) {
            globals.push_back(global);
        }
        return globals;
    }

    std::string trimCopy(const std::string& value) {
        const auto first = value.find_first_not_of(' ');
        if (first == std::string::npos) {
            return "";
        }
        const auto last = value.find_last_not_of(' ');
        return value.substr(first, last - first + 1);
    }

    bool parseSignedInteger(const std::string& raw, long long& out) {
        const std::string value = trimCopy(raw);
        if (value.empty()) {
            return false;
        }

        const char* begin = value.c_str();
        char* end = nullptr;
        out = std::strtoll(begin, &end, 0);
        return end != begin && *end == '\0';
    }

    bool toStackSymbol(const std::string& operand, std::string& symbol, const StackFrameLayout layout) {
        const auto lb = operand.find('[');
        const auto rb = operand.find(']');
        if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1) {
            return false;
        }

        std::string inside;
        inside.reserve(rb - lb - 1);
        for (size_t i = lb + 1; i < rb; ++i) {
            if (operand[i] != ' ') {
                inside += static_cast<char>(std::tolower(static_cast<unsigned char>(operand[i])));
            }
        }

        if (!inside.starts_with("rbp")) {
            return false;
        }

        if (inside == "rbp") {
            symbol = "var_0";
            return true;
        }

        if (inside.size() < 5 || (inside[3] != '-' && inside[3] != '+')) {
            return false;
        }

        long long offset = 0;
        if (!parseSignedInteger(inside.substr(4), offset)) {
            return false;
        }

        if (inside[3] == '-') {
            symbol = "var_" + std::to_string(offset);
            return true;
        }

        if (layout == StackFrameLayout::Win64HomeSlots) {
            const auto homeSlotArgument = argumentIndexForWin64HomeSlot(offset);
            if (homeSlotArgument.has_value()) {
                symbol = "arg_" + std::to_string(*homeSlotArgument);
                return true;
            }
        }

        symbol = "arg_" + std::to_string(offset);
        return true;
    }

    std::string globalSymbolForAddress(const uint64_t address, const size_t sizeBytes) {
        for (const auto& region : g_globalMemoryRegions) {
            if (region.size == 0) {
                continue;
            }
            if (address >= region.start && address - region.start < region.size) {
                const auto namedGlobalIt = g_namedGlobals.find(address);
                const std::string symbol = (namedGlobalIt != g_namedGlobals.end())
                                               ? namedGlobalIt->second
                                               : "global_" + sanitizeSymbolPart(region.sectionName) + "_" + hexAddress(address);
                auto& global = g_referencedGlobals[address];
                global.address = address;
                global.symbol = symbol;
                global.sizeBytes = std::max(global.sizeBytes, sizeBytes);
                return symbol;
            }
        }
        return {};
    }

    std::string operandForDisplay(const IROperand& operand, const StackFrameLayout layout) {
        if (operand.type == OpType::MEM && operand.memory.has_value() && operand.memory->absolute.has_value()) {
            if (const std::string symbol = globalSymbolForAddress(*operand.memory->absolute, operand.memory->sizeBytes); !symbol.empty()) {
                return symbol;
            }
        }
        return normalizeOperandForDisplay(operand.value, layout);
    }

    std::string normalizeOperandForDisplay(const std::string& operand, const StackFrameLayout layout) {
        std::string symbol;
        if (toStackSymbol(operand, symbol, layout)) {
            return symbol;
        }
        return operand;
    }

    std::string normalizeImmediateForDisplay(const std::string& operand) {
        std::string value = trimCopy(operand);
        bool isNegative = false;
        if (!value.empty() && value[0] == '-') {
            isNegative = true;
            value = value.substr(1);
        }

        if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
            const std::string digits = value.substr(2);
            if (!digits.empty() && digits.size() <= 16 &&
                std::ranges::all_of(digits, [](const unsigned char c) { return std::isxdigit(c) != 0; })) {
                try {
                    const auto parsed = std::stoull(digits, nullptr, 16);
                    const std::string decimal = std::to_string(parsed);
                    return isNegative ? "-" + decimal : decimal;
                } catch (...) {
                    return operand;
                }
            }
        }
        return trimCopy(operand);
    }

    std::string invertComparisonOperator(const std::string& op) {
        if (op == "<=") return ">";
        if (op == "<") return ">=";
        if (op == ">=") return "<";
        if (op == ">") return "<=";
        if (op == "==") return "!=";
        if (op == "!=") return "==";
        return op;
    }

    bool startsWithAny(const std::string& value, const std::initializer_list<std::string_view> prefixes) {
        for (const auto prefix : prefixes) {
            if (value.rfind(prefix, 0) == 0) {
                return true;
            }
        }
        return false;
    }

    bool isTemporaryName(const std::string& value) {
        return startsWithAny(value, {
            "rax_", "eax_", "ax_", "al_",
            "rbx_", "ebx_", "bx_", "bl_",
            "rcx_", "ecx_", "cx_", "cl_",
            "rdx_", "edx_", "dx_", "dl_",
            "rdi_", "edi_", "di_", "dil_",
            "rsi_", "esi_", "si_", "sil_",
            "r8_", "r8d_", "r8w_", "r8b_",
            "r9_", "r9d_", "r9w_", "r9b_",
            "r10_", "r10d_", "r10w_", "r10b_",
            "r11_", "r11d_", "r11w_", "r11b_",
            "r12_", "r12d_", "r12w_", "r12b_",
            "r13_", "r13d_", "r13w_", "r13b_",
            "r14_", "r14d_", "r14w_", "r14b_",
            "r15_", "r15d_", "r15w_", "r15b_"
        });
    }

    std::string normalizedValueForDisplay(const std::string& operand, const StackFrameLayout layout) {
        return normalizeImmediateForDisplay(normalizeOperandForDisplay(operand, layout));
    }

    namespace {
        std::optional<size_t> previousMeaningfulInstructionIndex(const std::vector<IRInstruction>& instrs,
                                                                               const size_t from) {
            if (from == 0) {
                return std::nullopt;
            }

            size_t index = from;
            while (index-- > 0) {
                if (IRProperties::isNop(instrs[index])) {
                    continue;
                }
                return index;
            }

            return std::nullopt;
        }

        std::string resolveOperandFromDefinitions(const std::vector<IRInstruction>& instrs,
                                                  const size_t cmpIndex,
                                                  const std::string& operand,
                                                  const StackFrameLayout layout,
                                                  std::unordered_set<std::string>& active) {
            if (!isTemporaryName(operand) || cmpIndex == 0 || active.contains(operand)) {
                return normalizedValueForDisplay(operand, layout);
            }

            active.insert(operand);
            for (size_t i = cmpIndex; i-- > 0;) {
                const auto& inst = instrs[i];
                if (IRProperties::operandAt(inst, 0).value != operand) {
                    continue;
                }

                if (inst.type == IRType::ASSIGN || inst.type == IRType::SEXT || inst.type == IRType::LOAD || inst.type == IRType::LOAD_CONST) {
                    const auto& sourceOperand = IRProperties::operandAt(inst, 1);
                    if (startsWithAny(sourceOperand.value, {"eax_", "rax_"}) && previousMeaningfulInstructionIndex(instrs, i).has_value()) {
                        const auto prevIndex = *previousMeaningfulInstructionIndex(instrs, i);
                        if (instrs[prevIndex].type == IRType::CALL) {
                            active.erase(operand);
                            return normalizedValueForDisplay(operand, layout);
                        }
                    }
                    if (isTemporaryName(sourceOperand.value) && sourceOperand.value != operand) {
                        const std::string value = resolveOperandFromDefinitions(instrs, i, sourceOperand.value, layout, active);
                        active.erase(operand);
                        return value;
                    }
                    active.erase(operand);
                    return normalizeImmediateForDisplay(operandForDisplay(sourceOperand, layout));
                }

                if (inst.type == IRType::ADD) {
                    const std::string lhs = resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 1).value, layout), layout, active);
                    const std::string rhs = resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout), layout, active);
                    active.erase(operand);

                    if (lhs == operand && rhs == normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout)) {
                        return normalizedValueForDisplay(operand, layout);
                    }
                    return lhs + " + " + rhs;
                }
                if (inst.type == IRType::SUB) {
                    const std::string lhs = resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 1).value, layout), layout, active);
                    const std::string rhs = resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout), layout, active);
                    active.erase(operand);

                    if (lhs == operand && rhs == normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout)) {
                        return normalizedValueForDisplay(operand, layout);
                    }
                    return lhs + " - " + rhs;
                }
                if (inst.type == IRType::AND || inst.type == IRType::OR || inst.type == IRType::XOR || inst.type == IRType::SHL || inst.type == IRType::SHR || inst.type == IRType::SAR || inst.type == IRType::MUL || inst.type == IRType::DIV) {
                    const std::string lhs = resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 1).value, layout), layout, active);
                    const std::string rhs = resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout), layout, active);
                    active.erase(operand);

                    const std::string op =
                        inst.type == IRType::AND ? "&" :
                        inst.type == IRType::OR ? "|" :
                        inst.type == IRType::XOR ? "^" :
                        inst.type == IRType::SHL ? "<<" :
                        inst.type == IRType::MUL ? "*" :
                        inst.type == IRType::DIV ? "/" : ">>";
                    return "(" + lhs + " " + op + " " + rhs + ")";
                }
                if (inst.type == IRType::NEG || inst.type == IRType::NOT) {
                    const std::string value = resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 1).value, layout), layout, active);
                    active.erase(operand);
                    return (inst.type == IRType::NEG ? "-" : "~") + value;
                }
                if (inst.type == IRType::LEA) {
                    active.erase(operand);
                    return normalizeOperandForDisplay(IRProperties::operandAt(inst, 1).value, layout);
                }

                break;
            }
            active.erase(operand);
            return normalizedValueForDisplay(operand, layout);
        }
    }

    std::string substituteTempFromDefinitions(const std::vector<IRInstruction>& instrs,
                                              const size_t cmpIndex,
                                              const std::string& operand,
                                              const StackFrameLayout layout) {
        std::unordered_set<std::string> active;
        return resolveOperandFromDefinitions(instrs, cmpIndex, operand, layout, active);
    }

    std::string invertConditionExpr(const std::string& expr) {
        static std::array<std::string_view, 6> ops = {"<=", ">=", "==", "!=", "<", ">"};
        for (const auto op : ops) {
            const std::string marker = " " + std::string(op) + " ";
            const auto pos = expr.find(marker);
            if (pos == std::string::npos) {
                continue;
            }
            const std::string lhs = expr.substr(0, pos);
            const std::string rhs = expr.substr(pos + marker.size());
            const std::string inv = invertComparisonOperator(std::string(op));
            return lhs + " " + inv + " " + rhs;
        }
        return "!(" + expr + ")";
    }
}
