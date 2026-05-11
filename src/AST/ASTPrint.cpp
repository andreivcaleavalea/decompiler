#include "AST.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ASTDetail.h"

namespace Decompiler {
    namespace {
        bool containsCondition(const std::vector<std::string>& assumptions,
                                              const std::string& condition) {
            return std::find(assumptions.begin(), assumptions.end(), condition) != assumptions.end();
        }

        std::string trimCopy(const std::string& value) {
            const auto first = value.find_first_not_of(' ');
            if (first == std::string::npos) {
                return {};
            }
            const auto last = value.find_last_not_of(' ');
            return value.substr(first, last - first + 1);
        }

        std::string simplifyExpressionText(std::string expression) {
            size_t pos = 0;
            while ((pos = expression.find(" + -", pos)) != std::string::npos) {
                expression.replace(pos, 4, " - ");
                pos += 3;
            }

            std::string_view negPrefix = "0 - ";
            if (expression.starts_with(negPrefix)) {
                expression = "-" + expression.substr(negPrefix.size());
            }

            std::string_view allBits = " ^ 4294967295";
            if (expression.ends_with(allBits)) {
                expression = "~" + expression.substr(0, expression.size() - allBits.size());
            }
            return expression;
        }

        bool isContinueLine(const std::string& line) {
            return trimCopy(line) == "continue";
        }

        bool expressionNeedsParensForMultiplicative(const std::string& expression) {
            return expression.find(" + ") != std::string::npos ||
                   expression.find(" - ") != std::string::npos;
        }

        std::string parenthesizeForOperator(const std::string& expression,
                                                          const std::string& op) {
            if ((op == "*" || op == "/") && expressionNeedsParensForMultiplicative(expression)) {
                return "(" + expression + ")";
            }
            return expression;
        }

        std::vector<std::string> printNodesWithAssumptions(
            const std::vector<std::unique_ptr<ASTNode>>& nodes,
            int indent,
            const std::vector<std::string>& assumptions);

        std::vector<std::string> printIfWithAssumptions(const IfNode& node,
                                                                      const int indent,
                                                                      const std::vector<std::string>& assumptions) {
            const std::string condition = simplifyExpressionText(node.condition_expr);
            if (containsCondition(assumptions, condition)) {
                return printNodesWithAssumptions(node.true_branch, indent, assumptions);
            }

            const std::string inverted = simplifyExpressionText(ASTDetail::invertConditionExpr(condition));
            if (containsCondition(assumptions, inverted)) {
                return printNodesWithAssumptions(node.false_branch, indent, assumptions);
            }

            std::vector<std::string> trueAssumptions = assumptions;
            trueAssumptions.push_back(condition);
            std::vector<std::string> falseAssumptions = assumptions;
            falseAssumptions.push_back(inverted);

            std::vector<std::string> trueLines =
                printNodesWithAssumptions(node.true_branch, indent + 4, trueAssumptions);
            std::vector<std::string> falseLines =
                printNodesWithAssumptions(node.false_branch, indent + 4, falseAssumptions);

            const std::string spaces(indent, ' ');
            std::vector<std::string> result;
            if (!trueLines.empty() && trueLines == falseLines) {
                return trueLines;
            }
            if (trueLines.empty() && !falseLines.empty()) {
                result.push_back(spaces + "if (" + inverted + ") {");
                result.insert(result.end(), falseLines.begin(), falseLines.end());
                result.push_back(spaces + "}");
                return result;
            }

            result.push_back(spaces + "if (" + condition + ") {");
            result.insert(result.end(), trueLines.begin(), trueLines.end());

            if (!falseLines.empty()) {
                result.push_back(spaces + "} else {");
                result.insert(result.end(), falseLines.begin(), falseLines.end());
            }
            result.push_back(spaces + "}");
            return result;
        }

        std::vector<std::string> printNodesWithAssumptions(
            const std::vector<std::unique_ptr<ASTNode>>& nodes,
            const int indent,
            const std::vector<std::string>& assumptions) {
            std::vector<std::string> result;
            for (const auto& child : nodes) {
                std::vector<std::string> lines;
                if (const auto* ifNode = dynamic_cast<const IfNode*>(child.get())) {
                    lines = printIfWithAssumptions(*ifNode, indent, assumptions);
                } else {
                    lines = child->print(indent);
                }
                result.insert(result.end(), lines.begin(), lines.end());
            }
            return result;
        }
    }

    std::vector<std::string> SimpleBlockNode::print(int indent) const {
        using namespace ASTDetail;

        std::vector<std::string> result;
        std::string spaces(indent, ' ');
        const auto isUsedLater = [this](const std::string& value, const size_t fromIndex) {
            if (value.empty()) {
                return false;
            }
            for (size_t j = fromIndex + 1; j < instructions.size(); ++j) {
                const auto& next = instructions[j];
                if (IRProperties::operandAt(next, 1).value == value || IRProperties::operandAt(next, 2).value == value || IRProperties::operandAt(next, 0).value == value) {
                    return true;
                }
            }
            return false;
        };
        const auto isSyntheticSsaCopy = [](const IRInstruction& instruction) {
            return instruction.address == 0 && IRProperties::isAssign(instruction);
        };
        const auto isReturnRegister = [](const std::string& value) {
            return value == "eax" || value == "rax" || startsWithAny(value, {"eax_", "rax_"});
        };
        const auto isStackPointerRegister = [](const std::string& value) {
            return value == "rsp" || value == "esp" || value == "sp" || startsWithAny(value, {"rsp_", "esp_", "sp_"});
        };
        const auto isFramePointerRegister = [](const std::string& value) {
            return value == "rbp" || value == "ebp" || value == "bp" || startsWithAny(value, {"rbp_", "ebp_", "bp_"});
        };
        const auto isEpilogueNoise = [&](const IRInstruction& instruction) {
            const std::string d = normalizeOperandForDisplay(IRProperties::operandAt(instruction, 0).value);
            const std::string s = normalizeOperandForDisplay(IRProperties::operandAt(instruction, 1).value);
            const bool touchesFrameRegs =
                isStackPointerRegister(d) || isStackPointerRegister(s) ||
                isFramePointerRegister(d) || isFramePointerRegister(s);
            if (!touchesFrameRegs) {
                return false;
            }
            return instruction.type == IRType::ASSIGN || instruction.type == IRType::ADD || instruction.type == IRType::SUB || instruction.type == IRType::PUSH || instruction.type == IRType::POP;
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
        enum class CallingConvention {
            Unknown,
            SystemV,
            Win64
        };
        const auto normalizedRegisterBase = [](std::string value) {
            const auto first = value.find_first_not_of(' ');
            if (first == std::string::npos) {
                return std::string{};
            }
            const auto last = value.find_last_not_of(' ');
            value = value.substr(first, last - first + 1);

            const auto underscore = value.find('_');
            if (underscore != std::string::npos) {
                value = value.substr(0, underscore);
            }
            std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        };
        const auto callingConvention = [&]() {
            bool hasSystemVLeadRegs = false;
            bool hasWinLeadRegs = false;

            const auto observeOperand = [&](const std::string& operand) {
                const std::string base = normalizedRegisterBase(normalizeOperandForDisplay(operand));
                if (base == "rdi" || base == "edi" || base == "rsi" || base == "esi") {
                    hasSystemVLeadRegs = true;
                }
                if (base == "rcx" || base == "ecx") {
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
        const auto argumentIndexForRegister = [&](const std::string& registerName) -> std::optional<size_t> {
            const std::string base = normalizedRegisterBase(registerName);
            if (base.empty()) {
                return std::nullopt;
            }

            if (callingConvention == CallingConvention::SystemV) {
                if (base == "rdi" || base == "edi") return 0;
                if (base == "rsi" || base == "esi") return 1;
                if (base == "rdx" || base == "edx") return 2;
                if (base == "rcx" || base == "ecx") return 3;
                if (base == "r8"  || base == "r8d") return 4;
                if (base == "r9"  || base == "r9d") return 5;
                return std::nullopt;
            }
            if (callingConvention == CallingConvention::Win64) {
                if (base == "rcx" || base == "ecx") return 0;
                if (base == "rdx" || base == "edx") return 1;
                if (base == "r8"  || base == "r8d") return 2;
                if (base == "r9"  || base == "r9d") return 3;
                return std::nullopt;
            }

            if (base == "rdi" || base == "edi" || base == "rcx" || base == "ecx") return 0;
            if (base == "rsi" || base == "esi" || base == "rdx" || base == "edx") return 1;
            if (base == "r8"  || base == "r8d") return 2;
            if (base == "r9"  || base == "r9d") return 3;
            return std::nullopt;
        };
        const auto resolveRegisterBaseAt = [&](const std::string& base, const size_t index) {
            for (size_t j = index; j-- > 0;) {
                const auto& previous = instructions[j];
                if (normalizedRegisterBase(IRProperties::operandAt(previous, 0).value) != base) {
                    continue;
                }

                if (previous.type == IRType::ASSIGN || previous.type == IRType::LOAD || previous.type == IRType::LOAD_CONST || previous.type == IRType::SEXT || previous.type == IRType::LEA) {
                    std::string value = normalizeOperandForDisplay(IRProperties::operandAt(previous, 1).value);
                    if (isTemporaryName(value)) {
                        value = substituteTempFromDefinitions(instructions, j, IRProperties::operandAt(previous, 1).value);
                    }
                    return normalizeImmediateForDisplay(value);
                }
                break;
            }
            return base;
        };
        const auto renderMemoryAddress = [&](const IRMemoryAddress& memory,
                                             const std::string& original,
                                             const size_t index) {
            if (memory.absolute.has_value()) {
                if (const std::string globalSymbol = globalSymbolForAddress(*memory.absolute, memory.sizeBytes); !globalSymbol.empty()) {
                    return globalSymbol;
                }
            }

            if (memory.ripRelative) {
                return normalizeOperandForDisplay(original);
            }

            const std::string baseExpr = memory.base.empty() ? std::string{} : resolveRegisterBaseAt(memory.base, index);
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

            return normalizeOperandForDisplay(original);
        };
        const auto renderMemoryOperand = [&](const IROperand& operand, const size_t index) {
            if (operand.memory.has_value()) {
                return renderMemoryAddress(*operand.memory, operand.value, index);
            }
            return normalizeOperandForDisplay(operand.value);
        };
        const auto renderInlineMemoryText = [&](const std::string& operand, const size_t index) {
            const auto lb = operand.find('[');
            const auto rb = operand.find(']');
            if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1) {
                return normalizeOperandForDisplay(operand);
            }

            std::string inside;
            inside.reserve(rb - lb - 1);
            for (size_t i = lb + 1; i < rb; ++i) {
                if (operand[i] != ' ') {
                    inside += static_cast<char>(std::tolower(static_cast<unsigned char>(operand[i])));
                }
            }

            if (inside.starts_with("rip")) {
                return normalizeOperandForDisplay(operand);
            }

            const auto plus = inside.find('+');
            if (plus != std::string::npos) {
                const std::string base = inside.substr(0, plus);
                const std::string rest = inside.substr(plus + 1);
                const auto scalePos = rest.find('*');
                if (scalePos != std::string::npos) {
                    const std::string indexBase = rest.substr(0, scalePos);
                    const std::string scale = rest.substr(scalePos + 1);
                    const std::string baseExpr = resolveRegisterBaseAt(base, index);
                    const std::string indexExpr = resolveRegisterBaseAt(indexBase, index);
                    if (!baseExpr.empty() && !indexExpr.empty()) {
                        if (scale == "1" || scale == "4") {
                            return baseExpr + "[" + indexExpr + "]";
                        }
                        return baseExpr + "[" + indexExpr + " * " + scale + "]";
                    }
                }
            }

            if (std::ranges::all_of(inside, [](const unsigned char ch) {
                    return std::isalnum(ch) != 0 || ch == '_';
                })) {
                const std::string baseExpr = resolveRegisterBaseAt(inside, index);
                if (!baseExpr.empty() && baseExpr != inside) {
                    return "*" + baseExpr;
                }
            }

            return normalizeOperandForDisplay(operand);
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

        struct PendingCallArgument {
            size_t setup_index = 0;
            std::string register_name;
            std::string expression;
        };
        std::unordered_map<size_t, PendingCallArgument> pendingCallArguments;
        std::unordered_map<size_t, std::string> callResultExpressions;
        const auto flushPendingCallArguments = [&]() {
            if (pendingCallArguments.empty()) {
                return;
            }

            std::vector<PendingCallArgument> ordered;
            ordered.reserve(pendingCallArguments.size());
            for (const auto& [_, argument] : pendingCallArguments) {
                ordered.push_back(argument);
            }
            std::ranges::sort(ordered, [](const PendingCallArgument& lhs, const PendingCallArgument& rhs) {
                return lhs.setup_index < rhs.setup_index;
            });

            for (const auto& argument : ordered) {
                result.push_back(spaces + argument.register_name + " = " + argument.expression);
            }
            pendingCallArguments.clear();
        };
        const auto buildCallExpression = [&](const std::string& callName) {
            std::vector<std::pair<size_t, PendingCallArgument>> orderedArguments;
            orderedArguments.reserve(pendingCallArguments.size());
            for (const auto& [argIndex, argument] : pendingCallArguments) {
                orderedArguments.push_back({argIndex, argument});
            }
            std::ranges::sort(orderedArguments, [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });

            std::vector<std::string> args;
            args.reserve(orderedArguments.size());
            for (const auto& [_, argument] : orderedArguments) {
                args.push_back(argument.expression);
            }

            std::string expression = callName.empty() ? "call" : callName;
            expression += "(";
            for (size_t index = 0; index < args.size(); ++index) {
                if (index != 0) {
                    expression += ", ";
                }
                expression += args[index];
            }
            expression += ")";
            return expression;
        };
        const auto operandIsReturnRegister = [&](const IROperand& operand) {
            const std::string base = normalizedRegisterBase(operand.value);
            return base == "eax" || base == "rax";
        };
        const auto instructionConsumesReturnRegister = [&](const IRInstruction& instruction) {
            return operandIsReturnRegister(IRProperties::operandAt(instruction, 1)) || operandIsReturnRegister(IRProperties::operandAt(instruction, 2));
        };
        const auto callExpressionForReturnRegisterAt = [&](const size_t index) -> std::optional<std::string> {
            for (size_t j = index; j-- > 0;) {
                const auto& previous = instructions[j];
                if (isSyntheticSsaCopy(previous) || IRProperties::isNop(previous)) {
                    continue;
                }
                if (previous.type == IRType::CALL) {
                    if (const auto callIt = callResultExpressions.find(j); callIt != callResultExpressions.end()) {
                        return callIt->second;
                    }
                    return std::nullopt;
                }
                if (normalizedRegisterBase(IRProperties::operandAt(previous, 0).value) == "eax" ||
                    normalizedRegisterBase(IRProperties::operandAt(previous, 0).value) == "rax") {
                    return std::nullopt;
                }
                if (!isEpilogueNoise(previous)) {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        };
        const auto renderExpression = [&](const IROperand& operand, const size_t index) {
            if (operand.type == OpType::MEM) {
                return normalizeImmediateForDisplay(renderMemoryOperand(operand, index));
            }
            if (operandIsReturnRegister(operand)) {
                if (const auto callExpression = callExpressionForReturnRegisterAt(index); callExpression.has_value()) {
                    return *callExpression;
                }
            }
            std::string value = normalizeOperandForDisplay(operand.value);
            if (isTemporaryName(value)) {
                value = substituteTempFromDefinitions(instructions, index, operand.value);
            }
            return simplifyExpressionText(normalizeImmediateForDisplay(normalizeMemoryReferences(value, index)));
        };
        const auto tryBuildAssignedExpression = [&](const size_t index) -> std::optional<std::string> {
            const auto& inst = instructions[index];
            const std::string srcExpr = renderExpression(IRProperties::operandAt(inst, 1), index);
            const std::string src2Expr = renderExpression(IRProperties::operandAt(inst, 2), index);

            if (inst.type == IRType::ASSIGN || inst.type == IRType::LOAD_CONST || inst.type == IRType::LOAD || inst.type == IRType::SEXT || inst.type == IRType::LEA) {
                if (srcExpr.empty()) {
                    return std::nullopt;
                }
                return simplifyExpressionText(srcExpr);
            }
            if (inst.type == IRType::ADD || inst.type == IRType::SUB || inst.type == IRType::AND || inst.type == IRType::OR || inst.type == IRType::XOR || inst.type == IRType::SHL || inst.type == IRType::SHR || inst.type == IRType::SAR || inst.type == IRType::MUL || inst.type == IRType::DIV) {
                if (srcExpr.empty() || src2Expr.empty()) {
                    return std::nullopt;
                }
                const std::string op =
                    inst.type == IRType::ADD ? "+" :
                    inst.type == IRType::SUB ? "-" :
                    inst.type == IRType::AND ? "&" :
                    inst.type == IRType::OR ? "|" :
                    inst.type == IRType::XOR ? "^" :
                    inst.type == IRType::SHL ? "<<" :
                    inst.type == IRType::MUL ? "*" :
                    inst.type == IRType::DIV ? "/" : ">>";
                return simplifyExpressionText(
                    parenthesizeForOperator(srcExpr, op) + " " + op + " " + parenthesizeForOperator(src2Expr, op));
            }
            if (inst.type == IRType::NEG || inst.type == IRType::NOT) {
                if (srcExpr.empty()) {
                    return std::nullopt;
                }
                return simplifyExpressionText(std::string(inst.type == IRType::NEG ? "-" : "~") + srcExpr);
            }
            return std::nullopt;
        };
        const auto isPureExpressionInstruction = [](const IRInstruction& instruction) {
            return instruction.type == IRType::ASSIGN || instruction.type == IRType::LOAD_CONST || instruction.type == IRType::LOAD || instruction.type == IRType::SEXT || instruction.type == IRType::LEA || instruction.type == IRType::ADD || instruction.type == IRType::SUB || instruction.type == IRType::AND || instruction.type == IRType::OR || instruction.type == IRType::XOR || instruction.type == IRType::SHL || instruction.type == IRType::SHR || instruction.type == IRType::SAR || instruction.type == IRType::MUL || instruction.type == IRType::DIV || instruction.type == IRType::NEG || instruction.type == IRType::NOT;
        };
        const auto isComposableTemporary = [&](const std::string& value) {
            return isTemporaryName(normalizeOperandForDisplay(value));
        };

        for (size_t i = 0; i < instructions.size(); ++i) {
            const auto& inst = instructions[i];
            if (isSyntheticSsaCopy(inst) || IRProperties::isPhi(inst) || IRProperties::isNop(inst)) {
                continue;
            }
            if (IRProperties::isJump(inst.type)) {
                flushPendingCallArguments();
                continue;
            }

            if (inst.type == IRType::CALL) {
                std::string callName = renderExpression(IRProperties::operandAt(inst, 0), i);
                if (isTemporaryName(callName)) {
                    callName = normalizeImmediateForDisplay(substituteTempFromDefinitions(instructions, i, IRProperties::operandAt(inst, 0).value));
                }
                const std::string callExpr = buildCallExpression(callName);
                const size_t spillIndex = nextMeaningfulIndex(i + 1);

                if (spillIndex < instructions.size()) {
                    const auto& spill = instructions[spillIndex];
                    const std::string spillDest = normalizeOperandForDisplay(IRProperties::operandAt(spill, 0).value);
                    const std::string spillSrc = normalizeOperandForDisplay(IRProperties::operandAt(spill, 1).value);
                    const bool capturesReturnValue =
                        (spill.type == IRType::ASSIGN || spill.type == IRType::STORE) &&
                        !spillDest.empty() &&
                        !isStackPointerRegister(spillDest) &&
                        !isFramePointerRegister(spillDest) &&
                        isReturnRegister(spillSrc);

                    if (capturesReturnValue) {
                        result.push_back(spaces + spillDest + " = " + callExpr);
                        pendingCallArguments.clear();
                        i = spillIndex;
                        continue;
                    }

                    if (spill.type == IRType::RET) {
                        result.push_back(spaces + "return " + callExpr);
                        pendingCallArguments.clear();
                        i = spillIndex;
                        continue;
                    }

                    if (instructionConsumesReturnRegister(spill)) {
                        callResultExpressions[i] = callExpr;
                        pendingCallArguments.clear();
                        continue;
                    }
                }

                result.push_back(spaces + callExpr);
                pendingCallArguments.clear();
                continue;
            }

            if (inst.type == IRType::PUSH && startsWithAny(IRProperties::operandAt(inst, 1).value, {"rbp_"})) {
                continue;
            }
            if (inst.type == IRType::POP && startsWithAny(IRProperties::operandAt(inst, 0).value, {"rbp_"})) {
                continue;
            }
            if (inst.type == IRType::RET) {
                flushPendingCallArguments();
                continue;
            }
            if (i + 2 < instructions.size()) {
                const auto& addInst = instructions[i + 1];
                const auto& storeInst = instructions[i + 2];

                std::string loadedVar;
                std::string storedVar;
                if (inst.type == IRType::LOAD
                    && addInst.type == IRType::ADD
                    && storeInst.type == IRType::STORE
                    && toStackSymbol(IRProperties::operandAt(inst, 1).value, loadedVar)
                    && toStackSymbol(IRProperties::operandAt(storeInst, 0).value, storedVar)
                    && loadedVar == storedVar
                    && IRProperties::operandAt(storeInst, 1).value == IRProperties::operandAt(addInst, 0).value) {
                    const std::string rhs = normalizeImmediateForDisplay(normalizeOperandForDisplay(IRProperties::operandAt(addInst, 2).value));
                    result.push_back(spaces + loadedVar + " = " + loadedVar + " + " + rhs);
                    i += 2;
                    continue;
                }
            }

            std::string op = inst.op;
            std::string dest = normalizeOperandForDisplay(IRProperties::operandAt(inst, 0).value);
            std::string src = simplifyExpressionText(renderExpression(IRProperties::operandAt(inst, 1), i));

            std::string stackSymbol;
            if (inst.type == IRType::LOAD) {
                op = "assign";
                if (toStackSymbol(IRProperties::operandAt(inst, 1).value, stackSymbol)) {
                    src = stackSymbol;
                }
            } else if (inst.type == IRType::STORE && toStackSymbol(IRProperties::operandAt(inst, 0).value, stackSymbol)) {
                op = "assign";
                dest = stackSymbol;
            } else if (inst.type == IRType::STORE) {
                op = "assign";
                dest = renderExpression(IRProperties::operandAt(inst, 0), i);
            } else if (inst.type == IRType::SEXT) {
                op = "assign";
            }
            bool rendersAssignment = inst.type == IRType::ASSIGN || inst.type == IRType::LOAD || inst.type == IRType::STORE || inst.type == IRType::SEXT;

            if (argumentIndexForRegister(dest).has_value()) {
                const size_t argIndex = *argumentIndexForRegister(dest);
                if (const auto expression = tryBuildAssignedExpression(i); expression.has_value()) {
                    pendingCallArguments[argIndex] = PendingCallArgument{i, dest, *expression};
                    continue;
                }
            }

            if (!pendingCallArguments.empty() && !isEpilogueNoise(inst)) {
                flushPendingCallArguments();
            }

            if (isStackPointerRegister(dest) || isStackPointerRegister(src)) {
                continue;
            }

            if (isComposableTemporary(IRProperties::operandAt(inst, 0).value)) {
                const size_t forwardedIndex = nextMeaningfulIndex(i + 1);
                if (forwardedIndex < instructions.size()) {
                    const auto& forwarded = instructions[forwardedIndex];
                    if ((forwarded.type == IRType::ASSIGN || forwarded.type == IRType::STORE) && IRProperties::operandAt(forwarded, 1).value == IRProperties::operandAt(inst, 0).value) {
                        std::string finalDest = normalizeOperandForDisplay(IRProperties::operandAt(forwarded, 0).value);
                        if (forwarded.type == IRType::STORE) {
                            std::string stackSymbol;
                            if (toStackSymbol(IRProperties::operandAt(forwarded, 0).value, stackSymbol)) {
                                finalDest = stackSymbol;
                            } else {
                                finalDest = renderExpression(IRProperties::operandAt(forwarded, 0), forwardedIndex);
                            }
                        }
                        if (const auto expression = tryBuildAssignedExpression(i); expression.has_value()) {
                            const size_t afterForward = nextMeaningfulIndex(forwardedIndex + 1);
                            const bool isReturnAssignment =
                                isReturnRegister(finalDest)
                                && afterForward < instructions.size()
                                && instructions[afterForward].type == IRType::RET;

                            if (isReturnAssignment) {
                                result.push_back(spaces + "return " + *expression);
                                i = afterForward;
                                continue;
                            }

                            if (!finalDest.empty() && !isTemporaryName(finalDest)) {
                                result.push_back(spaces + finalDest + " = " + *expression);
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

            if (isComposableTemporary(IRProperties::operandAt(inst, 0).value)) {
                const size_t forwardedIndex = nextMeaningfulIndex(i + 1);
                if (forwardedIndex < instructions.size()) {
                    const auto& forwarded = instructions[forwardedIndex];
                    const bool feedsForwardedTemporary =
                        isPureExpressionInstruction(forwarded) &&
                        isComposableTemporary(IRProperties::operandAt(forwarded, 0).value) &&
                        (IRProperties::operandAt(forwarded, 1).value == IRProperties::operandAt(inst, 0).value || IRProperties::operandAt(forwarded, 2).value == IRProperties::operandAt(inst, 0).value);
                    if (feedsForwardedTemporary) {
                        continue;
                    }
                }
            }

            if ((inst.type == IRType::ADD || inst.type == IRType::SUB)
                && i + 1 < instructions.size()
                && instructions[i + 1].type == IRType::ASSIGN
                && IRProperties::operandAt(instructions[i + 1], 1).value == IRProperties::operandAt(inst, 0).value) {
                const std::string finalDest = normalizeOperandForDisplay(IRProperties::operandAt(instructions[i + 1], 0).value);
                const std::string rhs = normalizeImmediateForDisplay(normalizeOperandForDisplay(IRProperties::operandAt(inst, 2).value));
                const std::string expression = (inst.type == IRType::ADD)
                    ? (src + " + " + rhs)
                    : (src + " - " + rhs);
                const size_t afterAssign = nextMeaningfulIndex(i + 2);
                const bool isReturnAssignment =
                    isReturnRegister(finalDest)
                    && afterAssign < instructions.size()
                    && instructions[afterAssign].type == IRType::RET;

                if (isReturnAssignment) {
                    result.push_back(spaces + "return " + expression);
                    i = afterAssign;
                    continue;
                }

                    if (inst.type == IRType::ADD) {
                        if (finalDest == src && rhs == "1") {
                            result.push_back(spaces + finalDest + "++");
                        } else if (finalDest == src && rhs == "-1") {
                            result.push_back(spaces + finalDest + "--");
                        } else {
                            result.push_back(spaces + finalDest + " = " + src + " + " + rhs);
                        }
                    } else {
                        if (finalDest == src && rhs == "1") {
                            result.push_back(spaces + finalDest + "--");
                        } else {
                            result.push_back(spaces + finalDest + " = " + src + " - " + rhs);
                        }
                    }
                i += 1;
                continue;
            }

                if ((inst.type == IRType::ADD || inst.type == IRType::SUB)
                    && isReturnRegister(dest)
                    && nextMeaningfulIndex(i + 1) < instructions.size()
                    && instructions[nextMeaningfulIndex(i + 1)].type == IRType::RET) {
                    const std::string rhs = normalizeImmediateForDisplay(normalizeOperandForDisplay(IRProperties::operandAt(inst, 2).value));
                    const std::string expression = (inst.type == IRType::ADD)
                        ? (src + " + " + rhs)
                        : (src + " - " + rhs);
                    result.push_back(spaces + "return " + expression);
                    continue;
                }

                if ((inst.type == IRType::LOAD_CONST || rendersAssignment)
                    && isReturnRegister(dest)
                    && nextMeaningfulIndex(i + 1) < instructions.size()
                    && instructions[nextMeaningfulIndex(i + 1)].type == IRType::RET) {
                    if (src.empty()) {
                        result.push_back(spaces + "return");
                    } else {
                        result.push_back(spaces + "return " + normalizeImmediateForDisplay(src));
                    }
                    continue;
                }
                if (inst.type == IRType::LOAD_CONST && isTemporaryName(dest) && !isUsedLater(IRProperties::operandAt(inst, 0).value, i)) {
                    continue;
                }

                if (rendersAssignment && startsWithAny(dest, {"rbp_"}) && startsWithAny(src, {"rsp_"})) {
                    continue;
                }
                if (rendersAssignment && isTemporaryName(dest) && startsWithAny(src, {"var_", "arg_"})) {
                    continue;
                }
                if ((inst.type == IRType::ADD || inst.type == IRType::SUB) && isTemporaryName(dest) && !isUsedLater(IRProperties::operandAt(inst, 0).value, i)) {
                    continue;
                }

                if (rendersAssignment && !src.empty() && dest == src) {
                    continue;
                }
            if (rendersAssignment) {
                result.push_back(spaces + dest + (src.empty() ? " =" : " = " + simplifyExpressionText(src)));
            } else if (inst.type == IRType::ADD) {
                const std::string rhs = normalizeImmediateForDisplay(normalizeOperandForDisplay(IRProperties::operandAt(inst, 2).value));
                if (dest == src && rhs == "1") {
                    result.push_back(spaces + dest + "++");
                } else if (dest == src && rhs == "-1") {
                    result.push_back(spaces + dest + "--");
                } else {
                    result.push_back(spaces + dest + " = " + simplifyExpressionText(src + " + " + rhs));
                }
            } else if (inst.type == IRType::SUB) {
                const std::string rhs = normalizeImmediateForDisplay(normalizeOperandForDisplay(IRProperties::operandAt(inst, 2).value));
                if (dest == src && rhs == "1") {
                    result.push_back(spaces + dest + "--");
                } else {
                    result.push_back(spaces + dest + " = " + simplifyExpressionText(src + " - " + rhs));
                }
            } else if (inst.type == IRType::AND || inst.type == IRType::OR || inst.type == IRType::XOR || inst.type == IRType::SHL || inst.type == IRType::SHR || inst.type == IRType::SAR) {
                const std::string rhs = normalizeImmediateForDisplay(normalizeOperandForDisplay(IRProperties::operandAt(inst, 2).value));
                const std::string opSymbol =
                    inst.type == IRType::AND ? "&" :
                    inst.type == IRType::OR ? "|" :
                    inst.type == IRType::XOR ? "^" :
                    inst.type == IRType::SHL ? "<<" :
                    inst.type == IRType::SHR ? ">>" : ">>";
                result.push_back(spaces + dest + " = " + simplifyExpressionText(src + " " + opSymbol + " " + rhs));
            } else if (inst.type == IRType::MUL) {
                const std::string rhs = normalizeImmediateForDisplay(normalizeOperandForDisplay(IRProperties::operandAt(inst, 2).value));
                if (!src.empty() && !rhs.empty()) {
                    result.push_back(spaces + dest + " = " + simplifyExpressionText(
                        parenthesizeForOperator(src, "*") + " * " + parenthesizeForOperator(rhs, "*")));
                } else {
                    result.push_back(spaces + op + " " + dest);
                }
            } else if (inst.type == IRType::DIV) {
                const std::string rhs = normalizeImmediateForDisplay(normalizeOperandForDisplay(IRProperties::operandAt(inst, 2).value));
                if (!src.empty() && !rhs.empty()) {
                    result.push_back(spaces + dest + " = " + simplifyExpressionText(
                        parenthesizeForOperator(src, "/") + " / " + parenthesizeForOperator(rhs, "/")));
                } else {
                    result.push_back(spaces + op + " " + dest);
                }
            } else if (inst.type == IRType::PUSH) {
                result.push_back(spaces + "push " + src);
            } else if (inst.type == IRType::CMP || inst.type == IRType::TEST) {
                const std::string rhs = normalizeImmediateForDisplay(normalizeOperandForDisplay(IRProperties::operandAt(inst, 2).value));
                if (!src.empty() && !rhs.empty()) {
                    result.push_back(spaces + op + " " + src + ", " + rhs);
                } else if (!src.empty()) {
                    result.push_back(spaces + op + " " + src);
                } else {
                    result.push_back(spaces + op);
                }
            } else {
                std::string line = spaces + op;
                if (!dest.empty()) {
                    line += " " + dest;
                }
                if (!src.empty()) {
                    line += dest.empty() ? " " : ", ";
                    line += src;
                }
                result.push_back(std::move(line));
            }
        }
        flushPendingCallArguments();
        return result;
    }

    std::vector<std::string> IfNode::print(int indent) const {
        return printIfWithAssumptions(*this, indent, {});
    }

    std::vector<std::string> WhileNode::print(int indent) const {
        std::vector<std::string> result;
        std::string spaces(indent, ' ');
        result.push_back(spaces + "while (" + condition_expr + ") {");

        for (const auto& node : body) {
            auto lines = node->print(indent + 4);
            result.insert(result.end(), lines.begin(), lines.end());
        }
        if (!result.empty() && isContinueLine(result.back())) {
            result.pop_back();
        }

        result.push_back(spaces + "}");
        return result;
    }

    std::vector<std::string> DoWhileNode::print(int indent) const {
        std::vector<std::string> result;
        std::string spaces(indent, ' ');
        result.push_back(spaces + "do {");

        for (const auto& node : body) {
            auto lines = node->print(indent + 4);
            result.insert(result.end(), lines.begin(), lines.end());
        }
        if (!result.empty() && isContinueLine(result.back())) {
            result.pop_back();
        }

        result.push_back(spaces + "} while (" + condition_expr + ");");
        return result;
    }

    std::vector<std::string> BreakNode::print(int indent) const {
        return {std::string(indent, ' ') + "break"};
    }

    std::vector<std::string> ContinueNode::print(int indent) const {
        return {std::string(indent, ' ') + "continue"};
    }
}
