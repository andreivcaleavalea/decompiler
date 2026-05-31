#include "ASTDetail.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <map>
#include <unordered_set>
#include <utility>

#include "WindowsX64.h"
#include "llvm/Demangle/Demangle.h"

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

std::optional<std::string> tryRenderPackedString(const uint64_t value)
{
    std::string text;
    bool terminated = false;
    for (int i = 0; i < 8; ++i) {
        const unsigned char byte = static_cast<unsigned char>((value >> (i * 8)) & 0xFFU);
        if (byte == 0) {
            terminated = true;
            continue;
        }
        if (terminated || byte < 0x20 || byte > 0x7E) {
            return std::nullopt;
        }
        text.push_back(static_cast<char>(byte));
    }
    if (text.size() < 5) {
        return std::nullopt;
    }
    return "\"" + text + "\"";
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
        bool allHex              = !digits.empty() && digits.size() <= 16;
        for (size_t i = 0; allHex && i < digits.size(); ++i) {
            allHex = std::isxdigit(static_cast<unsigned char>(digits[i])) != 0;
        }
        if (allHex) {
            try {
                const auto parsed = std::stoull(digits, nullptr, 16);
                if (!isNegative) {
                    if (const auto packed = tryRenderPackedString(parsed); packed.has_value()) {
                        return *packed;
                    }
                    int64_t signedValue = static_cast<int64_t>(parsed);
                    if (digits.size() <= 8 && parsed >= 0x80000000ULL) {
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

    if (!isNegative && !value.empty() && std::isdigit(static_cast<unsigned char>(value[0])) != 0) {
        try {
            size_t consumed   = 0;
            const auto parsed = std::stoull(value, &consumed, 10);
            if (consumed == value.size()) {
                if (const auto packed = tryRenderPackedString(parsed); packed.has_value()) {
                    return *packed;
                }
            }
        } catch (...) {
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
    return startsWithAny(value, { "rax_",  "eax_",  "ax_",   "al_",   "rbx_",  "ebx_",  "bx_",    "bl_",    "rcx_",   "ecx_",   "cx_",    "cl_",
                                  "rdx_",  "edx_",  "dx_",   "dl_",   "rdi_",  "edi_",  "di_",    "dil_",   "rsi_",   "esi_",   "si_",    "sil_",
                                  "r8_",   "r8d_",  "r8w_",  "r8b_",  "r9_",   "r9d_",  "r9w_",   "r9b_",   "r10_",   "r10d_",  "r10w_",  "r10b_",
                                  "r11_",  "r11d_", "r11w_", "r11b_", "r12_",  "r12d_", "r12w_",  "r12b_",  "r13_",   "r13d_",  "r13w_",  "r13b_",
                                  "r14_",  "r14d_", "r14w_", "r14b_", "r15_",  "r15d_", "r15w_",  "r15b_",  "xmm0_",  "xmm1_",  "xmm2_",  "xmm3_",
                                  "xmm4_", "xmm5_", "xmm6_", "xmm7_", "xmm8_", "xmm9_", "xmm10_", "xmm11_", "xmm12_", "xmm13_", "xmm14_", "xmm15_" });
}

namespace
{
    std::string renderHeapDerefAddress(
          const std::vector<IRInstruction>& instrs, size_t cmpIndex, const IROperand& operand, std::unordered_set<std::string>& active);

    std::string reconstructCallExpression(const std::vector<IRInstruction>& instrs, size_t callIndex, std::unordered_set<std::string>& active);

    std::string renderArrayAccessImpl(
          const std::vector<IRInstruction>& instrs, size_t cmpIndex, const std::string& addressReg, std::unordered_set<std::string>& active);

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
          const std::vector<IRInstruction>& instrs, const size_t cmpIndex, const std::string& operand, std::unordered_set<std::string>& active)
    {
        if (!isTemporaryName(operand) || cmpIndex == 0 || active.contains(operand)) {
            return normalizeImmediateForDisplay(operand);
        }

        active.insert(operand);
        for (size_t i = cmpIndex; i-- > 0;) {
            const auto& inst = instrs[i];
            if (inst.type == IRType::CALL && startsWithAny(operand, { "eax_", "rax_", "xmm0_" })) {
                active.erase(operand);
                return reconstructCallExpression(instrs, i, active);
            }
            if (IRProperties::operandAt(inst, 0).name != operand) {
                continue;
            }

            if (inst.type == IRType::ASSIGN || inst.type == IRType::SEXT || inst.type == IRType::LOAD || inst.type == IRType::LOAD_CONST) {
                const auto& sourceOperand = IRProperties::operandAt(inst, 1);
                if (startsWithAny(sourceOperand.name, { "eax_", "rax_", "xmm0_" }) && previousMeaningfulInstructionIndex(instrs, i).has_value()) {
                    const auto prevIndex = *previousMeaningfulInstructionIndex(instrs, i);
                    if (instrs[prevIndex].type == IRType::CALL) {
                        active.erase(operand);
                        return reconstructCallExpression(instrs, prevIndex, active);
                    }
                }
                if (isTemporaryName(sourceOperand.name) && sourceOperand.name != operand) {
                    const std::string value = resolveOperandFromDefinitions(instrs, i, sourceOperand.name, active);
                    active.erase(operand);
                    return value;
                }
                if (sourceOperand.tag == OperandTag::StackVar && !sourceOperand.arrayIndex.empty()) {
                    const std::string indexValue = resolveOperandFromDefinitions(instrs, i, sourceOperand.arrayIndex, active);
                    active.erase(operand);
                    return sourceOperand.name + "[" + indexValue + "]";
                }
                if (sourceOperand.isHeapDeref()) {
                    const std::string address = renderHeapDerefAddress(instrs, i, sourceOperand, active);
                    active.erase(operand);
                    return address;
                }
                active.erase(operand);
                return normalizeImmediateForDisplay(sourceOperand.name);
            }

            if (inst.type == IRType::ADD) {
                const std::string lhs = resolveOperandFromDefinitions(instrs, i, normalizeImmediateForDisplay(IRProperties::operandAt(inst, 1).name), active);
                const std::string rhs = resolveOperandFromDefinitions(instrs, i, normalizeImmediateForDisplay(IRProperties::operandAt(inst, 2).name), active);
                active.erase(operand);

                if (lhs == operand && rhs == normalizeImmediateForDisplay(IRProperties::operandAt(inst, 2).name)) {
                    return normalizeImmediateForDisplay(operand);
                }
                return lhs + " + " + rhs;
            }
            if (inst.type == IRType::SUB) {
                const std::string lhs = resolveOperandFromDefinitions(instrs, i, normalizeImmediateForDisplay(IRProperties::operandAt(inst, 1).name), active);
                const std::string rhs = resolveOperandFromDefinitions(instrs, i, normalizeImmediateForDisplay(IRProperties::operandAt(inst, 2).name), active);
                active.erase(operand);

                if (lhs == operand && rhs == normalizeImmediateForDisplay(IRProperties::operandAt(inst, 2).name)) {
                    return normalizeImmediateForDisplay(operand);
                }
                return lhs + " - " + rhs;
            }
            if (inst.type == IRType::AND || inst.type == IRType::OR || inst.type == IRType::XOR || inst.type == IRType::SHL || inst.type == IRType::SHR ||
                inst.type == IRType::SAR || inst.type == IRType::MUL || inst.type == IRType::DIV || inst.type == IRType::MOD || inst.type == IRType::SMUL ||
                inst.type == IRType::SDIV || inst.type == IRType::SMOD) {
                const std::string lhs = resolveOperandFromDefinitions(instrs, i, normalizeImmediateForDisplay(IRProperties::operandAt(inst, 1).name), active);
                const std::string rhs = resolveOperandFromDefinitions(instrs, i, normalizeImmediateForDisplay(IRProperties::operandAt(inst, 2).name), active);
                active.erase(operand);

                const std::string op = inst.type == IRType::AND                                  ? "&"
                                       : inst.type == IRType::OR                                 ? "|"
                                       : inst.type == IRType::XOR                                ? "^"
                                       : inst.type == IRType::SHL                                ? "<<"
                                       : (inst.type == IRType::MUL || inst.type == IRType::SMUL) ? "*"
                                       : (inst.type == IRType::DIV || inst.type == IRType::SDIV) ? "/"
                                       : (inst.type == IRType::MOD || inst.type == IRType::SMOD) ? "%"
                                                                                                 : ">>";
                return "(" + lhs + " " + op + " " + rhs + ")";
            }
            if (inst.type == IRType::NEG || inst.type == IRType::NOT) {
                const std::string value = resolveOperandFromDefinitions(instrs, i, normalizeImmediateForDisplay(IRProperties::operandAt(inst, 1).name), active);
                active.erase(operand);
                return (inst.type == IRType::NEG ? "-" : "~") + value;
            }
            if (inst.type == IRType::LEA) {
                active.erase(operand);
                return IRProperties::operandAt(inst, 1).name;
            }

            if (inst.type == IRType::CONVERT) {
                std::string value = resolveOperandFromDefinitions(instrs, i, IRProperties::operandAt(inst, 1).name, active);
                active.erase(operand);
                if (value.find(' ') != std::string::npos) {
                    value = "(" + value + ")";
                }
                return "(" + convertCastType(IRProperties::operandAt(inst, 0)) + ")" + value;
            }

            if (inst.type == IRType::SETCC) {
                for (size_t k = i; k-- > 0;) {
                    const auto& flag = instrs[k];
                    if (flag.type != IRType::TEST && flag.type != IRType::CMP) {
                        continue;
                    }
                    const std::string lhs = resolveOperandFromDefinitions(instrs, k, IRProperties::operandAt(flag, 1).name, active);
                    const std::string op  = conditionToOperator(inst.condition);
                    active.erase(operand);
                    if (flag.type == IRType::TEST && IRProperties::operandAt(flag, 1).name == IRProperties::operandAt(flag, 2).name) {
                        return "(" + lhs + " " + op + " 0)";
                    }
                    if (flag.type == IRType::CMP) {
                        const std::string rhs = resolveOperandFromDefinitions(instrs, k, IRProperties::operandAt(flag, 2).name, active);
                        return "(" + lhs + " " + op + " " + rhs + ")";
                    }
                    return normalizeImmediateForDisplay(operand);
                }
                active.erase(operand);
                return normalizeImmediateForDisplay(operand);
            }

            break;
        }
        active.erase(operand);
        return normalizeImmediateForDisplay(operand);
    }

    std::string reconstructCallExpression(const std::vector<IRInstruction>& instrs, const size_t callIndex, std::unordered_set<std::string>& active)
    {
        std::string callName        = normalizeImmediateForDisplay(IRProperties::operandAt(instrs[callIndex], 0).name);
        callName                    = stripReturnType(llvm::demangle(callName));
        const auto callNameParenPos = callName.find('(');
        if (callNameParenPos != std::string::npos) {
            callName = callName.substr(0, callNameParenPos);
        }

        std::map<size_t, std::string> argMap;
        for (size_t j = callIndex; j-- > 0;) {
            const auto& argInst = instrs[j];
            if (IRProperties::isControlFlow(argInst.type)) {
                break;
            }
            if (!IRProperties::isSimpleValueProducer(argInst.type)) {
                continue;
            }
            if (!IRProperties::hasOperandAt(argInst, 0) || !IRProperties::hasOperandAt(argInst, 1)) {
                continue;
            }
            const auto& argDest = argInst.operands[0];
            if (!argDest.isAnyReg()) {
                continue;
            }
            const auto argIdx = argumentIndexForRegisterName(argDest.name);
            if (!argIdx.has_value() || argMap.count(*argIdx)) {
                continue;
            }
            argMap[*argIdx] = resolveOperandFromDefinitions(instrs, j, IRProperties::operandAt(argInst, 1).name, active);
        }

        if (argMap.empty()) {
            for (size_t j = callIndex; j-- > 0;) {
                const auto& argInst = instrs[j];
                if (IRProperties::isControlFlow(argInst.type) || IRProperties::isCall(argInst.type)) {
                    break;
                }
                if (argInst.type != IRType::STORE || !IRProperties::hasOperandAt(argInst, 0) || !IRProperties::hasOperandAt(argInst, 1)) {
                    continue;
                }
                const auto& argDest = argInst.operands[0];
                if (argDest.tag != OperandTag::StackVar || argDest.stackOffset < 0) {
                    continue;
                }
                const size_t argIdx = static_cast<size_t>(argDest.stackOffset) / 4;
                if (argMap.count(argIdx)) {
                    continue;
                }
                argMap[argIdx] = resolveOperandFromDefinitions(instrs, j, IRProperties::operandAt(argInst, 1).name, active);
            }
        }

        std::string callExpr = callName + "(";
        bool firstArg        = true;
        for (const auto& [_, argVal] : argMap) {
            if (!firstArg) {
                callExpr += ", ";
            }
            callExpr += argVal;
            firstArg = false;
        }
        callExpr += ")";
        return callExpr;
    }

    std::optional<std::string> scaledIndexSource(const std::vector<IRInstruction>& instrs, const size_t fromIndex, const std::string& reg)
    {
        if (!isTemporaryName(reg)) {
            return std::nullopt;
        }
        for (size_t i = fromIndex; i-- > 0;) {
            if (IRProperties::operandAt(instrs[i], 0).name != reg) {
                continue;
            }
            if (instrs[i].type == IRType::SHL || instrs[i].type == IRType::MUL || instrs[i].type == IRType::SMUL) {
                return IRProperties::operandAt(instrs[i], 1).name;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::string renderArrayAccessImpl(
          const std::vector<IRInstruction>& instrs, const size_t cmpIndex, const std::string& addressReg, std::unordered_set<std::string>& active)
    {
        if (!isTemporaryName(addressReg)) {
            return {};
        }
        for (size_t i = cmpIndex; i-- > 0;) {
            if (IRProperties::operandAt(instrs[i], 0).name != addressReg) {
                continue;
            }
            if (instrs[i].type != IRType::ADD) {
                return {};
            }
            const std::string opA = IRProperties::operandAt(instrs[i], 1).name;
            const std::string opB = IRProperties::operandAt(instrs[i], 2).name;
            for (int order = 0; order < 2; ++order) {
                const std::string& indexReg = order == 0 ? opA : opB;
                const std::string& baseReg  = order == 0 ? opB : opA;
                const auto indexSource      = scaledIndexSource(instrs, i, indexReg);
                if (!indexSource.has_value()) {
                    continue;
                }
                const std::string baseExpr  = resolveOperandFromDefinitions(instrs, i, baseReg, active);
                const std::string indexExpr = resolveOperandFromDefinitions(instrs, i, *indexSource, active);
                return baseExpr + "[" + indexExpr + "]";
            }
            return {};
        }
        return {};
    }

    std::string renderHeapDerefAddress(
          const std::vector<IRInstruction>& instrs, const size_t cmpIndex, const IROperand& operand, std::unordered_set<std::string>& active)
    {
        const auto& memory = operand.heapDeref;
        if (memory.ripRelative) {
            return normalizeImmediateForDisplay(operand.name);
        }

        if (!memory.base.empty() && memory.index.empty() && memory.displacement == 0) {
            const std::string arrayAccess = renderArrayAccessImpl(instrs, cmpIndex, memory.base, active);
            if (!arrayAccess.empty()) {
                return arrayAccess;
            }
        }

        const std::string baseExpr  = memory.base.empty() ? std::string{} : resolveOperandFromDefinitions(instrs, cmpIndex, memory.base, active);
        const std::string indexExpr = memory.index.empty() ? std::string{} : resolveOperandFromDefinitions(instrs, cmpIndex, memory.index, active);

        if (!baseExpr.empty() && !indexExpr.empty() && memory.displacement == 0) {
            if (memory.scale == 1 || memory.scale == 4) {
                return baseExpr + "[" + indexExpr + "]";
            }
            return baseExpr + "[" + indexExpr + " * " + std::to_string(memory.scale) + "]";
        }

        if (!baseExpr.empty() && memory.index.empty() && memory.displacement == 0 && baseExpr != memory.base) {
            if (baseExpr.find(' ') != std::string::npos) {
                return "*(" + baseExpr + ")";
            }
            return "*" + baseExpr;
        }

        return normalizeImmediateForDisplay(operand.name);
    }
} // namespace

std::string substituteTempFromDefinitions(const std::vector<IRInstruction>& instrs, const size_t cmpIndex, const std::string& operand)
{
    std::unordered_set<std::string> active;
    return resolveOperandFromDefinitions(instrs, cmpIndex, operand, active);
}

std::string renderArrayAccess(const std::vector<IRInstruction>& instrs, const size_t cmpIndex, const std::string& addressReg)
{
    std::unordered_set<std::string> active;
    return renderArrayAccessImpl(instrs, cmpIndex, addressReg, active);
}

std::string convertCastType(const IROperand& dest)
{
    if (dest.name.starts_with("xmm")) {
        return dest.sizeBytes == 4 ? "float" : "double";
    }
    return dest.sizeBytes == 8 ? "int64_t" : "int32_t";
}

std::string stripReturnType(const std::string& demangledName)
{
    int depth        = 0;
    size_t lastSpace = std::string::npos;
    for (size_t i = 0; i < demangledName.size(); ++i) {
        const char c = demangledName[i];
        if (c == '<') {
            ++depth;
        } else if (c == '>') {
            if (depth > 0) {
                --depth;
            }
        } else if (c == ' ' && depth == 0) {
            lastSpace = i;
        }
    }
    if (lastSpace == std::string::npos) {
        return demangledName;
    }
    const std::string after = demangledName.substr(lastSpace + 1);
    if (!after.empty() && (std::isalpha(static_cast<unsigned char>(after.front())) || after.front() == '_')) {
        return after;
    }
    return demangledName;
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
