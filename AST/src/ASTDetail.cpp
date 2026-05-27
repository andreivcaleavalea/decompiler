#include "ASTDetail.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <unordered_set>
#include <utility>

namespace Decompiler::ASTDetail
{

std::string trimCopy(const std::string& value)
{
    const auto first = value.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(' ');
    return value.substr(first, last - first + 1);
}

bool parseSignedInteger(const std::string& raw, long long& out)
{
    const std::string value = trimCopy(raw);
    if (value.empty()) {
        return false;
    }

    const char* begin = value.c_str();
    char* end         = nullptr;
    out               = std::strtoll(begin, &end, 0);
    return end != begin && *end == '\0';
}

std::string operandForDisplay(const IROperand& operand, const StackFrameLayout layout)
{
    return normalizeOperandForDisplay(operand.value, layout);
}

std::string normalizeOperandForDisplay(const std::string& operand, const StackFrameLayout /*layout*/)
{
    return operand;
}

std::string normalizeImmediateForDisplay(const std::string& operand)
{
    std::string value = trimCopy(operand);
    bool isNegative   = false;
    if (!value.empty() && value[0] == '-') {
        isNegative = true;
        value      = value.substr(1);
    }

    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        const std::string digits = value.substr(2);
        if (!digits.empty() && digits.size() <= 16 && std::ranges::all_of(digits, [](const unsigned char c) { return std::isxdigit(c) != 0; })) {
            try {
                const auto parsed = std::stoull(digits, nullptr, 16);

                if (!isNegative) {
                    int64_t signedValue = static_cast<int64_t>(parsed);
                    if (digits.size() <= 2 && parsed >= 0x80ULL) {
                        signedValue = static_cast<int64_t>(static_cast<int8_t>(parsed));
                    } else if (digits.size() <= 4 && parsed >= 0x8000ULL) {
                        signedValue = static_cast<int64_t>(static_cast<int16_t>(parsed));
                    } else if (digits.size() <= 8 && parsed >= 0x80000000ULL) {
                        signedValue = static_cast<int64_t>(static_cast<int32_t>(parsed));
                    }
                    if (signedValue < 0) {
                        return std::to_string(signedValue);
                    }
                }

                const std::string decimal = std::to_string(parsed);
                return isNegative ? "-" + decimal : decimal;
            } catch (...) {
                return operand;
            }
        }
    }
    return trimCopy(operand);
}

std::string invertComparisonOperator(const std::string& op)
{
    if (op == "<=")
        return ">";
    if (op == "<")
        return ">=";
    if (op == ">=")
        return "<";
    if (op == ">")
        return "<=";
    if (op == "==")
        return "!=";
    if (op == "!=")
        return "==";
    return op;
}

bool startsWithAny(const std::string& value, const std::initializer_list<std::string_view> prefixes)
{
    for (const auto prefix : prefixes) {
        if (value.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

bool isTemporaryName(const std::string& value)
{
    return startsWithAny(
          value, { "rax_",  "eax_",  "ax_",  "al_",   "rbx_",  "ebx_",  "bx_",   "bl_",   "rcx_",  "ecx_",  "cx_",   "cl_",   "rdx_",  "edx_",
                   "dx_",   "dl_",   "rdi_", "edi_",  "di_",   "dil_",  "rsi_",  "esi_",  "si_",   "sil_",  "r8_",   "r8d_",  "r8w_",  "r8b_",
                   "r9_",   "r9d_",  "r9w_", "r9b_",  "r10_",  "r10d_", "r10w_", "r10b_", "r11_",  "r11d_", "r11w_", "r11b_", "r12_",  "r12d_",
                   "r12w_", "r12b_", "r13_", "r13d_", "r13w_", "r13b_", "r14_",  "r14d_", "r14w_", "r14b_", "r15_",  "r15d_", "r15w_", "r15b_" });
}

std::string normalizedValueForDisplay(const std::string& operand, const StackFrameLayout layout)
{
    return normalizeImmediateForDisplay(normalizeOperandForDisplay(operand, layout));
}

namespace
{
    std::optional<size_t> previousMeaningfulInstructionIndex(const std::vector<IRInstruction>& instrs, const size_t from)
    {
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

    std::string resolveOperandFromDefinitions(
          const std::vector<IRInstruction>& instrs,
          const size_t cmpIndex,
          const std::string& operand,
          const StackFrameLayout layout,
          std::unordered_set<std::string>& active)
    {
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
                if (startsWithAny(sourceOperand.value, { "eax_", "rax_" }) && previousMeaningfulInstructionIndex(instrs, i).has_value()) {
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
                const std::string lhs =
                      resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 1).value, layout), layout, active);
                const std::string rhs =
                      resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout), layout, active);
                active.erase(operand);

                if (lhs == operand && rhs == normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout)) {
                    return normalizedValueForDisplay(operand, layout);
                }
                return lhs + " + " + rhs;
            }
            if (inst.type == IRType::SUB) {
                const std::string lhs =
                      resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 1).value, layout), layout, active);
                const std::string rhs =
                      resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout), layout, active);
                active.erase(operand);

                if (lhs == operand && rhs == normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout)) {
                    return normalizedValueForDisplay(operand, layout);
                }
                return lhs + " - " + rhs;
            }
            if (inst.type == IRType::AND || inst.type == IRType::OR || inst.type == IRType::XOR || inst.type == IRType::SHL || inst.type == IRType::SHR ||
                inst.type == IRType::SAR || inst.type == IRType::MUL || inst.type == IRType::DIV || inst.type == IRType::MOD) {
                const std::string lhs =
                      resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 1).value, layout), layout, active);
                const std::string rhs =
                      resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 2).value, layout), layout, active);
                active.erase(operand);

                const std::string op = inst.type == IRType::AND   ? "&"
                                       : inst.type == IRType::OR  ? "|"
                                       : inst.type == IRType::XOR ? "^"
                                       : inst.type == IRType::SHL ? "<<"
                                       : inst.type == IRType::MUL ? "*"
                                       : inst.type == IRType::DIV ? "/"
                                       : inst.type == IRType::MOD ? "%"
                                                                  : ">>";
                return "(" + lhs + " " + op + " " + rhs + ")";
            }
            if (inst.type == IRType::NEG || inst.type == IRType::NOT) {
                const std::string value =
                      resolveOperandFromDefinitions(instrs, i, normalizedValueForDisplay(IRProperties::operandAt(inst, 1).value, layout), layout, active);
                active.erase(operand);
                return (inst.type == IRType::NEG ? "-" : "~") + value;
            }
            if (inst.type == IRType::LEA) {
                active.erase(operand);
                return normalizeOperandForDisplay(IRProperties::operandAt(inst, 1).value, layout);
            }

            if (inst.type == IRType::SETCC) {
                for (size_t k = i; k-- > 0;) {
                    const auto& flag = instrs[k];
                    if (flag.type != IRType::TEST && flag.type != IRType::CMP) {
                        continue;
                    }
                    const std::string lhs = resolveOperandFromDefinitions(instrs, k, IRProperties::operandAt(flag, 1).value, layout, active);
                    const std::string op  = conditionToOperator(inst.condition);
                    active.erase(operand);
                    if (flag.type == IRType::TEST && IRProperties::operandAt(flag, 1).value == IRProperties::operandAt(flag, 2).value) {
                        return "(" + lhs + " " + op + " 0)";
                    }
                    if (flag.type == IRType::CMP) {
                        const std::string rhs = resolveOperandFromDefinitions(instrs, k, IRProperties::operandAt(flag, 2).value, layout, active);
                        return "(" + lhs + " " + op + " " + rhs + ")";
                    }
                    return normalizedValueForDisplay(operand, layout);
                }
                active.erase(operand);
                return normalizedValueForDisplay(operand, layout);
            }

            break;
        }
        active.erase(operand);
        return normalizedValueForDisplay(operand, layout);
    }
} // namespace

std::string substituteTempFromDefinitions(
      const std::vector<IRInstruction>& instrs, const size_t cmpIndex, const std::string& operand, const StackFrameLayout layout)
{
    std::unordered_set<std::string> active;
    return resolveOperandFromDefinitions(instrs, cmpIndex, operand, layout, active);
}

std::string invertConditionExpr(const std::string& expr)
{
    static std::array<std::string_view, 6> ops = { "<=", ">=", "==", "!=", "<", ">" };
    for (const auto op : ops) {
        const std::string marker = " " + std::string(op) + " ";
        const auto pos           = expr.find(marker);
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
} // namespace Decompiler::ASTDetail
