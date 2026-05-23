#include "AST.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ASTDetail.h"
#include "llvm/Demangle/Demangle.h"

namespace Decompiler
{
namespace
{
    std::string simplifyExpressionText(std::string expression)
    {
        size_t pos = 0;
        while ((pos = expression.find(" + -", pos)) != std::string::npos) {
            expression.replace(pos, 4, " - ");
            pos += 3;
        }

        constexpr std::string_view negPrefix = "0 - ";
        if (expression.starts_with(negPrefix)) {
            expression = "-" + expression.substr(negPrefix.size());
        }

        constexpr std::string_view allBits = " ^ 4294967295";
        if (expression.ends_with(allBits)) {
            expression = "~" + expression.substr(0, expression.size() - allBits.size());
        }
        return expression;
    }

    bool expressionNeedsParensForMultiplicative(const std::string& expression)
    {
        return expression.find(" + ") != std::string::npos || expression.find(" - ") != std::string::npos;
    }

    std::string parenthesizeForOperator(const std::string& expression, const std::string& op)
    {
        if ((op == "*" || op == "/") && expressionNeedsParensForMultiplicative(expression)) {
            return "(" + expression + ")";
        }
        return expression;
    }

    void pushText(std::vector<std::unique_ptr<ASTNode>>& statements, std::string text)
    {
        statements.push_back(std::make_unique<TextNode>(std::move(text)));
    }
} // namespace

std::vector<std::unique_ptr<ASTNode>> lowerBlockToStatements(const std::vector<IRInstruction>& instructions, const CallingConvention calling_convention)
{
    using namespace ASTDetail;

    std::vector<std::unique_ptr<ASTNode>> statements;
    std::set<size_t> absorbedIndices;
    const auto isUsedLater = [&](const std::string& value, const size_t fromIndex) {
        if (value.empty()) {
            return false;
        }
        for (size_t j = fromIndex + 1; j < instructions.size(); ++j) {
            const auto& next = instructions[j];
            if (IRProperties::operandAt(next, 1).value == value || IRProperties::operandAt(next, 2).value == value ||
                IRProperties::operandAt(next, 0).value == value) {
                return true;
            }
        }
        return false;
    };
    const auto isSyntheticSsaCopy     = [](const IRInstruction& instruction) { return instruction.address == 0 && IRProperties::isAssign(instruction); };
    const auto isReturnRegister       = [](const std::string& value) { return value == "eax" || value == "rax" || startsWithAny(value, { "eax_", "rax_" }); };
    const auto isStackPointerRegister = [](const std::string& value) {
        return value == "rsp" || value == "esp" || value == "sp" || startsWithAny(value, { "rsp_", "esp_", "sp_" });
    };
    const auto isFramePointerRegister = [](const std::string& value) {
        return value == "rbp" || value == "ebp" || value == "bp" || startsWithAny(value, { "rbp_", "ebp_", "bp_" });
    };
    const auto isEpilogueNoise = [&](const IRInstruction& instruction) {
        const std::string d         = normalizeOperandForDisplay(IRProperties::operandAt(instruction, 0).value);
        const std::string s         = normalizeOperandForDisplay(IRProperties::operandAt(instruction, 1).value);
        const bool touchesFrameRegs = isStackPointerRegister(d) || isStackPointerRegister(s) || isFramePointerRegister(d) || isFramePointerRegister(s);
        if (!touchesFrameRegs) {
            return false;
        }
        return instruction.type == IRType::ASSIGN || instruction.type == IRType::ADD || instruction.type == IRType::SUB || instruction.type == IRType::PUSH ||
               instruction.type == IRType::POP;
    };
    const auto nextMeaningfulIndex = [&](const size_t from) {
        size_t index = from;
        while (index < instructions.size()) {
            const auto& candidate = instructions[index];
            if (IRProperties::isTerminator(candidate.type)) {
                return index;
            }
            if (isSyntheticSsaCopy(candidate)) {
                ++index;
                continue;
            }
            if (!isEpilogueNoise(candidate)) {
                return index;
            }
            ++index;
        }
        return instructions.size();
    };
    const auto callingConvention = [&]() {
        if (calling_convention != CallingConvention::Unknown) {
            return calling_convention;
        }

        bool hasSystemVLeadRegs = false;
        bool hasWinLeadRegs     = false;

        const auto observeOperand = [&](const std::string& operand) {
            const std::string base = normalizedRegisterBase(normalizeOperandForDisplay(operand));
            if (base == "rdi" || base == "rsi") {
                hasSystemVLeadRegs = true;
            }
            if (base == "rcx") {
                hasWinLeadRegs = true;
            }
        };

        for (const auto& instruction : instructions) {
            for (const auto& operand : instruction.operands) {
                observeOperand(operand.value);
            }
        }

        if (hasSystemVLeadRegs) {
            return CallingConvention::SystemV;
        }
        if (hasWinLeadRegs) {
            return CallingConvention::Win64;
        }
        return CallingConvention::Unknown;
    }();
    const auto stackFrameLayout         = stackFrameLayoutForCallingConvention(callingConvention);
    const auto normalizeOperand         = [&](const std::string& operand) { return normalizeOperandForDisplay(operand, stackFrameLayout); };
    const auto argumentIndexForRegister = [&](const std::string& registerName) -> std::optional<size_t> {
        return argumentIndexForRegisterName(registerName, callingConvention);
    };
    const auto resolveRegisterBaseAt = [&](const std::string& base, const size_t index) {
        for (size_t j = index; j-- > 0;) {
            const auto& previous = instructions[j];
            if (normalizedRegisterBase(IRProperties::operandAt(previous, 0).value) != base) {
                continue;
            }

            if (previous.type == IRType::ASSIGN || previous.type == IRType::LOAD || previous.type == IRType::LOAD_CONST || previous.type == IRType::SEXT ||
                previous.type == IRType::LEA) {
                std::string value = normalizeOperand(IRProperties::operandAt(previous, 1).value);
                if (IRProperties::operandAt(previous, 1).kind == OperandKind::SsaTemp) {
                    value = substituteTempFromDefinitions(instructions, j, IRProperties::operandAt(previous, 1).value, stackFrameLayout);
                }
                return normalizeImmediateForDisplay(value);
            }
            break;
        }
        return base;
    };
    const auto renderMemoryAddress = [&](const IRMemoryAddress& memory, const std::string& original, const size_t index) {
        if (memory.ripRelative) {
            return normalizeOperand(original);
        }

        const std::string baseExpr  = memory.base.empty() ? std::string{} : resolveRegisterBaseAt(memory.base, index);
        const std::string indexExpr = memory.index.empty() ? std::string{} : resolveRegisterBaseAt(memory.index, index);

        if (!baseExpr.empty() && !indexExpr.empty() && memory.displacement == 0) {
            if (memory.scale == 1 || memory.scale == 4) {
                return baseExpr + "[" + indexExpr + "]";
            }
            return baseExpr + "[" + indexExpr + " * " + std::to_string(memory.scale) + "]";
        }

        if (!baseExpr.empty() && memory.index.empty() && memory.displacement == 0 && baseExpr != memory.base) {
            return "*" + baseExpr;
        }

        return normalizeOperand(original);
    };
    const auto renderMemoryOperand = [&](const IROperand& operand, const size_t index) {
        if (operand.memory.has_value()) {
            return renderMemoryAddress(*operand.memory, operand.value, index);
        }
        return normalizeOperand(operand.value);
    };
    const auto renderInlineMemoryText = [&](const std::string& operand, const size_t index) {
        const auto lb = operand.find('[');
        const auto rb = operand.find(']');
        if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1) {
            return normalizeOperand(operand);
        }

        std::string inside;
        inside.reserve(rb - lb - 1);
        for (size_t i = lb + 1; i < rb; ++i) {
            if (operand[i] != ' ') {
                inside += static_cast<char>(std::tolower(static_cast<unsigned char>(operand[i])));
            }
        }

        if (inside.starts_with("rip")) {
            return normalizeOperand(operand);
        }

        const auto plus = inside.find('+');
        if (plus != std::string::npos) {
            const std::string base = inside.substr(0, plus);
            const std::string rest = inside.substr(plus + 1);
            const auto scalePos    = rest.find('*');
            if (scalePos != std::string::npos) {
                const std::string indexBase = rest.substr(0, scalePos);
                const std::string scale     = rest.substr(scalePos + 1);
                const std::string baseExpr  = resolveRegisterBaseAt(base, index);
                const std::string indexExpr = resolveRegisterBaseAt(indexBase, index);
                if (!baseExpr.empty() && !indexExpr.empty()) {
                    if (scale == "1" || scale == "4") {
                        return baseExpr + "[" + indexExpr + "]";
                    }
                    return baseExpr + "[" + indexExpr + " * " + scale + "]";
                }
            }
        }

        const bool insideIsIdentifier =
              !inside.empty() && std::all_of(inside.begin(), inside.end(), [](const unsigned char ch) { return std::isalnum(ch) != 0 || ch == '_'; });
        if (insideIsIdentifier) {
            const std::string baseExpr = resolveRegisterBaseAt(inside, index);
            if (!baseExpr.empty() && baseExpr != inside) {
                return "*" + baseExpr;
            }
        }

        return normalizeOperand(operand);
    };
    const auto normalizeMemoryReferences = [&](std::string expression, const size_t index) {
        size_t searchFrom = 0;
        while (true) {
            const auto lb = expression.find('[', searchFrom);
            if (lb == std::string::npos) {
                break;
            }
            const auto rb = expression.find(']', lb + 1);
            if (rb == std::string::npos) {
                break;
            }

            size_t start = lb;
            while (start > 0) {
                const unsigned char ch = static_cast<unsigned char>(expression[start - 1]);
                if (std::isalnum(ch) == 0 && expression[start - 1] != '_' && expression[start - 1] != ' ') {
                    break;
                }
                --start;
            }
            while (start < lb && expression[start] == ' ') {
                ++start;
            }

            const std::string replacement = renderInlineMemoryText(expression.substr(start, rb - start + 1), index);
            if (replacement == expression.substr(start, rb - start + 1)) {
                searchFrom = rb + 1;
                continue;
            }
            expression.replace(start, rb - start + 1, replacement);
            searchFrom = start + replacement.size();
        }
        return expression;
    };

    const auto renderExpression = [&](const IROperand& operand, const size_t index) {
        if (operand.type == OpType::MEM) {
            return normalizeImmediateForDisplay(renderMemoryOperand(operand, index));
        }
        std::string value = normalizeOperand(operand.value);
        if (operand.kind == OperandKind::SsaTemp) {
            value = substituteTempFromDefinitions(instructions, index, operand.value, stackFrameLayout);
        }
        return simplifyExpressionText(normalizeImmediateForDisplay(normalizeMemoryReferences(value, index)));
    };
    const auto tryBuildAssignedExpression = [&](const size_t index) -> std::optional<std::string> {
        const auto& inst           = instructions[index];
        const std::string srcExpr  = renderExpression(IRProperties::operandAt(inst, 1), index);
        const std::string src2Expr = renderExpression(IRProperties::operandAt(inst, 2), index);

        if (inst.type == IRType::ASSIGN || inst.type == IRType::LOAD_CONST || inst.type == IRType::LOAD || inst.type == IRType::SEXT ||
            inst.type == IRType::LEA) {
            if (srcExpr.empty()) {
                return std::nullopt;
            }
            return simplifyExpressionText(srcExpr);
        }
        if (inst.type == IRType::ADD || inst.type == IRType::SUB || inst.type == IRType::AND || inst.type == IRType::OR || inst.type == IRType::XOR ||
            inst.type == IRType::SHL || inst.type == IRType::SHR || inst.type == IRType::SAR || inst.type == IRType::MUL || inst.type == IRType::DIV) {
            if (srcExpr.empty() || src2Expr.empty()) {
                return std::nullopt;
            }
            const std::string op = inst.type == IRType::ADD   ? "+"
                                   : inst.type == IRType::SUB ? "-"
                                   : inst.type == IRType::AND ? "&"
                                   : inst.type == IRType::OR  ? "|"
                                   : inst.type == IRType::XOR ? "^"
                                   : inst.type == IRType::SHL ? "<<"
                                   : inst.type == IRType::MUL ? "*"
                                   : inst.type == IRType::DIV ? "/"
                                                              : ">>";
            return simplifyExpressionText(parenthesizeForOperator(srcExpr, op) + " " + op + " " + parenthesizeForOperator(src2Expr, op));
        }
        if (inst.type == IRType::NEG || inst.type == IRType::NOT) {
            if (srcExpr.empty()) {
                return std::nullopt;
            }
            return simplifyExpressionText(std::string(inst.type == IRType::NEG ? "-" : "~") + srcExpr);
        }
        if (inst.type == IRType::SETCC) {
            for (size_t k = index; k-- > 0;) {
                const auto& flag = instructions[k];
                if (flag.type != IRType::TEST && flag.type != IRType::CMP) {
                    continue;
                }
                const std::string lhs = renderExpression(IRProperties::operandAt(flag, 1), k);
                const std::string op  = conditionToOperator(inst.condition);
                if (flag.type == IRType::TEST && IRProperties::operandAt(flag, 1).value == IRProperties::operandAt(flag, 2).value) {
                    return "(" + lhs + " " + op + " 0)";
                }
                if (flag.type == IRType::CMP) {
                    const std::string rhs = renderExpression(IRProperties::operandAt(flag, 2), k);
                    return "(" + lhs + " " + op + " " + rhs + ")";
                }
                return std::nullopt;
            }
            return std::nullopt;
        }
        return std::nullopt;
    };
    const auto isPureExpressionInstruction = [](const IRInstruction& instruction) {
        return instruction.type == IRType::ASSIGN || instruction.type == IRType::LOAD_CONST || instruction.type == IRType::LOAD ||
               instruction.type == IRType::SEXT || instruction.type == IRType::LEA || instruction.type == IRType::ADD || instruction.type == IRType::SUB ||
               instruction.type == IRType::AND || instruction.type == IRType::OR || instruction.type == IRType::XOR || instruction.type == IRType::SHL ||
               instruction.type == IRType::SHR || instruction.type == IRType::SAR || instruction.type == IRType::MUL || instruction.type == IRType::DIV ||
               instruction.type == IRType::NEG || instruction.type == IRType::NOT;
    };
    const auto isComposableTemporary = [](const IROperand& operand) { return operand.kind == OperandKind::SsaTemp; };

    for (size_t i = 0; i < instructions.size(); ++i) {
        if (absorbedIndices.contains(i)) {
            continue;
        }
        const auto& inst = instructions[i];
        if (isSyntheticSsaCopy(inst) || IRProperties::isPhi(inst) || IRProperties::isNop(inst)) {
            continue;
        }
        if (IRProperties::isJump(inst.type)) {
            continue;
        }

        if (inst.type == IRType::TEST || inst.type == IRType::CMP) {
            const size_t nextIdx = nextMeaningfulIndex(i + 1);
            if (nextIdx < instructions.size() && instructions[nextIdx].type == IRType::SETCC) {
                continue;
            }
        }

        if (inst.type == IRType::CALL) {
            std::string callName = renderExpression(IRProperties::operandAt(inst, 0), i);
            if (isTemporaryName(callName)) {
                callName =
                      normalizeImmediateForDisplay(substituteTempFromDefinitions(instructions, i, IRProperties::operandAt(inst, 0).value, stackFrameLayout));
            }
            if (callName.empty()) {
                callName = "call";
            }
            callName                    = llvm::demangle(callName);
            const auto callNameParenPos = callName.find('(');
            if (callNameParenPos != std::string::npos) {
                callName = callName.substr(0, callNameParenPos);
            }

            std::map<size_t, std::pair<size_t, std::string>> argsByIndex;
            for (size_t j = i; j-- > 0;) {
                if (absorbedIndices.contains(j)) {
                    continue;
                }
                const auto& candidate = instructions[j];
                if (isSyntheticSsaCopy(candidate)) {
                    continue;
                }
                if (candidate.type == IRType::CALL || IRProperties::isControlFlow(candidate.type)) {
                    break;
                }
                if (candidate.type != IRType::ASSIGN && candidate.type != IRType::LOAD && candidate.type != IRType::LOAD_CONST &&
                    candidate.type != IRType::SEXT && candidate.type != IRType::LEA) {
                    continue;
                }
                if (!IRProperties::hasOperandAt(candidate, 0) || !IRProperties::hasOperandAt(candidate, 1)) {
                    continue;
                }
                const auto& argDest = candidate.operands[0];
                if (argDest.type != OpType::REG) {
                    continue;
                }
                const auto argIdx = argumentIndexForRegister(argDest.value);
                if (!argIdx.has_value() || argsByIndex.contains(*argIdx)) {
                    continue;
                }
                const std::string argExpr = renderExpression(IRProperties::operandAt(candidate, 1), j);
                if (!argExpr.empty()) {
                    argsByIndex[*argIdx] = { j, argExpr };
                }
            }

            std::string callExpr = callName + "(";
            bool firstArg        = true;
            for (const auto& [argIdx, argInfo] : argsByIndex) {
                if (!firstArg) {
                    callExpr += ", ";
                }
                callExpr += argInfo.second;
                absorbedIndices.insert(argInfo.first);
                firstArg = false;
            }
            callExpr += ")";

            size_t spillIdx = nextMeaningfulIndex(i + 1);

            // Pattern: CALL → TEST → SETCC → [SsaTemp copies] → STORE(named, rax)
            // This is the bool-return pattern: the compiler normalizes the bool via test+setne.
            // The whole chain is equivalent to just the call's return value.
            if (spillIdx < instructions.size() && instructions[spillIdx].type == IRType::TEST) {
                const size_t setccIdx = nextMeaningfulIndex(spillIdx + 1);
                if (setccIdx < instructions.size() && instructions[setccIdx].type == IRType::SETCC) {
                    // Skip intermediate SsaTemp return-register copies (e.g., from movzx eax, al)
                    size_t nextIdx = nextMeaningfulIndex(setccIdx + 1);
                    std::vector<size_t> intermediateIndices;
                    while (nextIdx < instructions.size() && (instructions[nextIdx].type == IRType::ASSIGN || instructions[nextIdx].type == IRType::LOAD) &&
                           IRProperties::operandAt(instructions[nextIdx], 0).kind == OperandKind::SsaTemp &&
                           isReturnRegister(IRProperties::operandAt(instructions[nextIdx], 0).value)) {
                        intermediateIndices.push_back(nextIdx);
                        nextIdx = nextMeaningfulIndex(nextIdx + 1);
                    }
                    if (nextIdx < instructions.size()) {
                        const auto& boolSpill = instructions[nextIdx];
                        if ((boolSpill.type == IRType::ASSIGN || boolSpill.type == IRType::STORE) && IRProperties::hasOperandAt(boolSpill, 0) &&
                            IRProperties::hasOperandAt(boolSpill, 1) && isReturnRegister(IRProperties::operandAt(boolSpill, 1).value) &&
                            !isReturnRegister(IRProperties::operandAt(boolSpill, 0).value) &&
                            IRProperties::operandAt(boolSpill, 0).kind != OperandKind::SsaTemp) {
                            const std::string boolDest = normalizeOperand(IRProperties::operandAt(boolSpill, 0).value);
                            pushText(statements, boolDest + " = " + callExpr);
                            absorbedIndices.insert(spillIdx);
                            absorbedIndices.insert(setccIdx);
                            for (const size_t idx : intermediateIndices) {
                                absorbedIndices.insert(idx);
                            }
                            absorbedIndices.insert(nextIdx);
                            continue;
                        }
                    }
                }
            }

            if (spillIdx < instructions.size()) {
                const auto& spill = instructions[spillIdx];
                if ((spill.type == IRType::ASSIGN || spill.type == IRType::STORE) && IRProperties::hasOperandAt(spill, 0) &&
                    IRProperties::hasOperandAt(spill, 1) && isReturnRegister(IRProperties::operandAt(spill, 1).value) &&
                    !isReturnRegister(IRProperties::operandAt(spill, 0).value) && IRProperties::operandAt(spill, 0).kind != OperandKind::SsaTemp) {
                    const std::string spillDest = normalizeOperand(IRProperties::operandAt(spill, 0).value);
                    pushText(statements, spillDest + " = " + callExpr);
                    absorbedIndices.insert(spillIdx);
                    continue;
                }
            }

            pushText(statements, callExpr);
            continue;
        }

        if (inst.type == IRType::PUSH && startsWithAny(IRProperties::operandAt(inst, 1).value, { "rbp_" })) {
            continue;
        }
        if (inst.type == IRType::POP && startsWithAny(IRProperties::operandAt(inst, 0).value, { "rbp_" })) {
            continue;
        }
        if (inst.type == IRType::RET) {
            continue;
        }

        std::string op   = inst.op;
        std::string dest = normalizeOperand(IRProperties::operandAt(inst, 0).value);
        std::string src  = simplifyExpressionText(renderExpression(IRProperties::operandAt(inst, 1), i));

        if (inst.type == IRType::LOAD) {
            op = "assign";
        } else if (inst.type == IRType::STORE) {
            op   = "assign";
            dest = renderExpression(IRProperties::operandAt(inst, 0), i);
        } else if (inst.type == IRType::SEXT) {
            op = "assign";
        }
        const bool rendersAssignment = inst.type == IRType::ASSIGN || inst.type == IRType::LOAD || inst.type == IRType::STORE || inst.type == IRType::SEXT;

        if (isStackPointerRegister(dest) || isStackPointerRegister(src)) {
            continue;
        }

        if (rendersAssignment) {
            const auto destArgIndex = parseStackArgumentSymbolIndex(dest);
            const auto srcArgIndex  = argumentIndexForRegister(src);
            if (destArgIndex.has_value() && srcArgIndex.has_value() && *destArgIndex == *srcArgIndex) {
                continue;
            }
        }

        if (isComposableTemporary(IRProperties::operandAt(inst, 0))) {
            const size_t forwardedIndex = nextMeaningfulIndex(i + 1);
            if (forwardedIndex < instructions.size()) {
                const auto& forwarded = instructions[forwardedIndex];
                if ((forwarded.type == IRType::ASSIGN || forwarded.type == IRType::STORE) &&
                    IRProperties::operandAt(forwarded, 1).value == IRProperties::operandAt(inst, 0).value) {
                    std::string finalDest = normalizeOperand(IRProperties::operandAt(forwarded, 0).value);
                    if (forwarded.type == IRType::STORE) {
                        finalDest = renderExpression(IRProperties::operandAt(forwarded, 0), forwardedIndex);
                    }
                    if (const auto expression = tryBuildAssignedExpression(i); expression.has_value()) {
                        const size_t afterForward = nextMeaningfulIndex(forwardedIndex + 1);
                        const bool isReturnAssignment =
                              isReturnRegister(finalDest) && afterForward < instructions.size() && instructions[afterForward].type == IRType::RET;

                        if (isReturnAssignment) {
                            pushText(statements, "return " + *expression);
                            i = afterForward;
                            continue;
                        }

                        if (!finalDest.empty() && IRProperties::operandAt(forwarded, 0).kind != OperandKind::SsaTemp) {
                            pushText(statements, finalDest + " = " + *expression);
                            i = forwardedIndex;
                            continue;
                        }
                    }
                }

                if (forwarded.type == IRType::CALL && IRProperties::operandAt(forwarded, 0).value == IRProperties::operandAt(inst, 0).value) {
                    if (tryBuildAssignedExpression(i).has_value()) {
                        continue;
                    }
                }
            }
        }

        if (isComposableTemporary(IRProperties::operandAt(inst, 0))) {
            const size_t forwardedIndex = nextMeaningfulIndex(i + 1);
            if (forwardedIndex < instructions.size()) {
                const auto& forwarded              = instructions[forwardedIndex];
                const bool feedsForwardedTemporary = isPureExpressionInstruction(forwarded) && isComposableTemporary(IRProperties::operandAt(forwarded, 0)) &&
                                                     (IRProperties::operandAt(forwarded, 1).value == IRProperties::operandAt(inst, 0).value ||
                                                      IRProperties::operandAt(forwarded, 2).value == IRProperties::operandAt(inst, 0).value);
                if (feedsForwardedTemporary) {
                    continue;
                }
            }
        }

        if (isComposableTemporary(IRProperties::operandAt(inst, 0)) && argumentIndexForRegister(IRProperties::operandAt(inst, 0).value).has_value()) {
            const size_t nextIdx = nextMeaningfulIndex(i + 1);
            if (nextIdx < instructions.size() && instructions[nextIdx].type == IRType::CALL) {
                continue;
            }
        }

        if ((inst.type == IRType::ADD || inst.type == IRType::SUB) && i + 1 < instructions.size() && instructions[i + 1].type == IRType::ASSIGN &&
            IRProperties::operandAt(instructions[i + 1], 1).value == IRProperties::operandAt(inst, 0).value) {
            const std::string finalDest   = normalizeOperand(IRProperties::operandAt(instructions[i + 1], 0).value);
            const std::string rhs         = renderExpression(IRProperties::operandAt(inst, 2), i);
            const std::string expression  = (inst.type == IRType::ADD) ? (src + " + " + rhs) : (src + " - " + rhs);
            const size_t afterAssign      = nextMeaningfulIndex(i + 2);
            const bool isReturnAssignment = isReturnRegister(finalDest) && afterAssign < instructions.size() && instructions[afterAssign].type == IRType::RET;

            if (isReturnAssignment) {
                pushText(statements, "return " + expression);
                i = afterAssign;
                continue;
            }

            if (inst.type == IRType::ADD) {
                if (finalDest == src && rhs == "1") {
                    pushText(statements, finalDest + "++");
                } else if (finalDest == src && rhs == "-1") {
                    pushText(statements, finalDest + "--");
                } else {
                    pushText(statements, finalDest + " = " + src + " + " + rhs);
                }
            } else {
                if (finalDest == src && rhs == "1") {
                    pushText(statements, finalDest + "--");
                } else {
                    pushText(statements, finalDest + " = " + src + " - " + rhs);
                }
            }
            i += 1;
            continue;
        }

        if ((inst.type == IRType::ADD || inst.type == IRType::SUB) && isReturnRegister(dest) && nextMeaningfulIndex(i + 1) < instructions.size() &&
            instructions[nextMeaningfulIndex(i + 1)].type == IRType::RET) {
            const std::string rhs        = normalizeImmediateForDisplay(normalizeOperand(IRProperties::operandAt(inst, 2).value));
            const std::string expression = (inst.type == IRType::ADD) ? (src + " + " + rhs) : (src + " - " + rhs);
            pushText(statements, "return " + expression);
            continue;
        }

        if ((inst.type == IRType::LOAD_CONST || rendersAssignment) && isReturnRegister(dest)) {
            const size_t nextIdx = nextMeaningfulIndex(i + 1);
            const bool atReturnPoint =
                  nextIdx < instructions.size() && (instructions[nextIdx].type == IRType::RET || instructions[nextIdx].type == IRType::JMP);
            if (atReturnPoint) {
                if (src.empty()) {
                    pushText(statements, "return");
                } else {
                    pushText(statements, "return " + normalizeImmediateForDisplay(src));
                }
                continue;
            }
        }

        if (inst.type == IRType::LOAD_CONST && IRProperties::operandAt(inst, 0).kind == OperandKind::SsaTemp &&
            !isUsedLater(IRProperties::operandAt(inst, 0).value, i)) {
            continue;
        }

        if (rendersAssignment && startsWithAny(dest, { "rbp_" }) && startsWithAny(src, { "rsp_" })) {
            continue;
        }
        if (rendersAssignment && IRProperties::operandAt(inst, 0).kind == OperandKind::SsaTemp &&
            (IRProperties::operandAt(inst, 1).kind == OperandKind::StackVar || IRProperties::operandAt(inst, 1).kind == OperandKind::GlobalVar)) {
            continue;
        }
        if ((inst.type == IRType::ADD || inst.type == IRType::SUB) && IRProperties::operandAt(inst, 0).kind == OperandKind::SsaTemp &&
            !isUsedLater(IRProperties::operandAt(inst, 0).value, i)) {
            continue;
        }

        if (rendersAssignment && !src.empty() && dest == src) {
            continue;
        }

        if (rendersAssignment) {
            pushText(statements, dest + (src.empty() ? " =" : " = " + simplifyExpressionText(src)));
        } else if (inst.type == IRType::ADD) {
            const std::string rhs = renderExpression(IRProperties::operandAt(inst, 2), i);
            if (dest == src && rhs == "1") {
                pushText(statements, dest + "++");
            } else if (dest == src && rhs == "-1") {
                pushText(statements, dest + "--");
            } else {
                pushText(statements, dest + " = " + simplifyExpressionText(src + " + " + rhs));
            }
        } else if (inst.type == IRType::SUB) {
            const std::string rhs = renderExpression(IRProperties::operandAt(inst, 2), i);
            if (dest == src && rhs == "1") {
                pushText(statements, dest + "--");
            } else {
                pushText(statements, dest + " = " + simplifyExpressionText(src + " - " + rhs));
            }
        } else if (
              inst.type == IRType::AND || inst.type == IRType::OR || inst.type == IRType::XOR || inst.type == IRType::SHL || inst.type == IRType::SHR ||
              inst.type == IRType::SAR) {
            const std::string rhs      = renderExpression(IRProperties::operandAt(inst, 2), i);
            const std::string opSymbol = inst.type == IRType::AND   ? "&"
                                         : inst.type == IRType::OR  ? "|"
                                         : inst.type == IRType::XOR ? "^"
                                         : inst.type == IRType::SHL ? "<<"
                                         : inst.type == IRType::SHR ? ">>"
                                                                    : ">>";
            pushText(statements, dest + " = " + simplifyExpressionText(src + " " + opSymbol + " " + rhs));
        } else if (inst.type == IRType::MUL) {
            const std::string rhs = renderExpression(IRProperties::operandAt(inst, 2), i);
            if (!src.empty() && !rhs.empty()) {
                pushText(statements, dest + " = " + simplifyExpressionText(parenthesizeForOperator(src, "*") + " * " + parenthesizeForOperator(rhs, "*")));
            } else {
                pushText(statements, op + " " + dest);
            }
        } else if (inst.type == IRType::DIV) {
            const std::string rhs = renderExpression(IRProperties::operandAt(inst, 2), i);
            if (!src.empty() && !rhs.empty()) {
                pushText(statements, dest + " = " + simplifyExpressionText(parenthesizeForOperator(src, "/") + " / " + parenthesizeForOperator(rhs, "/")));
            } else {
                pushText(statements, op + " " + dest);
            }
        } else if (inst.type == IRType::NEG || inst.type == IRType::NOT) {
            if (!src.empty()) {
                pushText(statements, dest + " = " + simplifyExpressionText(std::string(inst.type == IRType::NEG ? "-" : "~") + src));
            } else {
                pushText(statements, op + " " + dest);
            }
        } else if (inst.type == IRType::PUSH) {
            pushText(statements, "push " + src);
        } else if (inst.type == IRType::CMP || inst.type == IRType::TEST) {
            const std::string rhs = normalizeImmediateForDisplay(normalizeOperand(IRProperties::operandAt(inst, 2).value));
            if (!src.empty() && !rhs.empty()) {
                pushText(statements, op + " " + src + ", " + rhs);
            } else if (!src.empty()) {
                pushText(statements, op + " " + src);
            } else {
                pushText(statements, op);
            }
        } else if (inst.type == IRType::SETCC) {
            if (const auto expression = tryBuildAssignedExpression(i); expression.has_value()) {
                pushText(statements, dest + " = " + *expression);
            }
        } else {
            std::string line = op;
            if (!dest.empty()) {
                line += " " + dest;
            }
            if (!src.empty()) {
                line += dest.empty() ? " " : ", ";
                line += src;
            }
            pushText(statements, std::move(line));
        }
    }

    return statements;
}
} // namespace Decompiler
