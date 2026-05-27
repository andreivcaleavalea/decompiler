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
    std::unique_ptr<Expression> makeSimplifiedExpression(std::unique_ptr<Expression> expression)
    {
        auto* binary = dynamic_cast<BinaryExpression*>(expression.get());
        if (binary == nullptr) {
            return expression;
        }
        if (binary->op == BinaryOp::Sub) {
            if (const auto* constant = dynamic_cast<const ConstantExpression*>(binary->lhs.get()); constant != nullptr && constant->value == "0") {
                return std::make_unique<UnaryExpression>(UnaryOp::Negate, std::move(binary->rhs));
            }
        }
        if (binary->op == BinaryOp::BitXor) {
            if (const auto* constant = dynamic_cast<const ConstantExpression*>(binary->rhs.get()); constant != nullptr && constant->value == "4294967295") {
                return std::make_unique<UnaryExpression>(UnaryOp::BitwiseNot, std::move(binary->lhs));
            }
        }
        return expression;
    }

    std::string renderSimplifiedExpression(std::unique_ptr<Expression> expression)
    {
        if (!expression) {
            return {};
        }
        return makeSimplifiedExpression(std::move(expression))->render();
    }

    bool hasSameBinaryOperator(const std::string& expressionText, const BinaryOp op)
    {
        const auto expression = makeExpressionFromText(expressionText);
        const auto* binary    = dynamic_cast<const BinaryExpression*>(expression.get());
        return binary != nullptr && binary->op == op;
    }

    bool isCommutative(const BinaryOp op)
    {
        return op == BinaryOp::Add || op == BinaryOp::Mul || op == BinaryOp::BitAnd || op == BinaryOp::BitOr || op == BinaryOp::BitXor;
    }

    std::optional<BinaryOp> binaryOpFromIRType(const IRType type)
    {
        switch (type) {
        case IRType::ADD:
            return BinaryOp::Add;
        case IRType::SUB:
            return BinaryOp::Sub;
        case IRType::MUL:
            return BinaryOp::Mul;
        case IRType::DIV:
            return BinaryOp::Div;
        case IRType::MOD:
            return BinaryOp::Mod;
        case IRType::AND:
            return BinaryOp::BitAnd;
        case IRType::OR:
            return BinaryOp::BitOr;
        case IRType::XOR:
            return BinaryOp::BitXor;
        case IRType::SHL:
            return BinaryOp::Shl;
        case IRType::SHR:
        case IRType::SAR:
            return BinaryOp::Shr;
        default:
            return std::nullopt;
        }
    }

    std::string buildBinaryExpressionText(const IRType type, const std::string& lhsText, const std::string& rhsText)
    {
        const auto op = binaryOpFromIRType(type);
        if (!op.has_value()) {
            return {};
        }

        std::string leftText  = lhsText;
        std::string rightText = rhsText;
        if (isCommutative(*op) && hasSameBinaryOperator(rhsText, *op) && !hasSameBinaryOperator(lhsText, *op)) {
            leftText  = rhsText;
            rightText = lhsText;
        }

        auto lhs = makeExpressionFromText(leftText);
        auto rhs = makeExpressionFromText(rightText);
        if (!lhs || !rhs) {
            return {};
        }
        return renderSimplifiedExpression(std::make_unique<BinaryExpression>(*op, std::move(lhs), std::move(rhs)));
    }

    void pushAssignment(std::vector<std::unique_ptr<ASTNode>>& statements, const std::string& target, const std::string& value)
    {
        auto targetExpression = makeExpressionFromText(target);
        auto valueExpression  = makeExpressionFromText(value);
        if (!targetExpression || !valueExpression) {
            return;
        }
        statements.push_back(std::make_unique<AssignmentNode>(std::move(targetExpression), std::move(valueExpression)));
    }

    void pushReturn(std::vector<std::unique_ptr<ASTNode>>& statements, const std::string& value)
    {
        if (value.empty()) {
            statements.push_back(std::make_unique<ReturnNode>());
            return;
        }
        auto expression = makeExpressionFromText(value);
        if (expression) {
            statements.push_back(std::make_unique<ReturnNode>(std::move(expression)));
        }
    }

    void pushExpressionStatement(std::vector<std::unique_ptr<ASTNode>>& statements, const std::string& expression)
    {
        auto expressionNode = makeExpressionFromText(expression);
        if (expressionNode) {
            statements.push_back(std::make_unique<ExpressionStatementNode>(std::move(expressionNode)));
        }
    }

    void pushUpdate(std::vector<std::unique_ptr<ASTNode>>& statements, const std::string& target, const UnaryOp operation)
    {
        auto targetExpression = makeExpressionFromText(target);
        if (targetExpression) {
            statements.push_back(std::make_unique<ExpressionStatementNode>(std::make_unique<UnaryExpression>(operation, std::move(targetExpression))));
        }
    }
    using namespace ASTDetail;

    class StatementLoweringContext
    {
      public:
        StatementLoweringContext(const std::vector<IRInstruction>& blockInstructions, const CallingConvention requestedCallingConvention)
            : instructions(blockInstructions), callingConvention(resolveCallingConvention(blockInstructions, requestedCallingConvention)),
              stackFrameLayout(stackFrameLayoutForCallingConvention(callingConvention))
        {
        }

        std::vector<std::unique_ptr<ASTNode>> lower()
        {
            for (size_t index = 0; index < instructions.size(); ++index) {
                lowerInstruction(index);
            }
            return std::move(statements);
        }

      private:
        const std::vector<IRInstruction>& instructions;
        std::vector<std::unique_ptr<ASTNode>> statements;
        std::set<size_t> absorbedIndices;
        CallingConvention callingConvention;
        StackFrameLayout stackFrameLayout;

        static bool isSyntheticSsaCopy(const IRInstruction& instruction)
        {
            return instruction.address == 0 && IRProperties::isAssign(instruction);
        }

        static CallingConvention resolveCallingConvention(
              const std::vector<IRInstruction>& blockInstructions, const CallingConvention requestedCallingConvention)
        {
            if (requestedCallingConvention != CallingConvention::Unknown) {
                return requestedCallingConvention;
            }

            bool hasSystemVLeadRegs = false;
            bool hasWinLeadRegs     = false;

            for (const auto& instruction : blockInstructions) {
                for (const auto& operand : instruction.operands) {
                    const std::string base = normalizedRegisterBase(normalizeOperandForDisplay(operand.value));
                    if (base == "rdi" || base == "rsi") {
                        hasSystemVLeadRegs = true;
                    }
                    if (base == "rcx") {
                        hasWinLeadRegs = true;
                    }
                }
            }

            if (hasSystemVLeadRegs) {
                return CallingConvention::SystemV;
            }
            if (hasWinLeadRegs) {
                return CallingConvention::Win64;
            }
            return CallingConvention::Unknown;
        }

        bool isUsedLater(const std::string& value, const size_t fromIndex) const
        {
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
        }

        bool isEpilogueNoise(const IRInstruction& instruction) const
        {
            const std::string dest      = normalizeOperandForDisplay(IRProperties::operandAt(instruction, 0).value);
            const std::string source    = normalizeOperandForDisplay(IRProperties::operandAt(instruction, 1).value);
            const bool touchesFrameRegs = isStackOrFrameRegisterName(dest) || isStackOrFrameRegisterName(source);
            if (!touchesFrameRegs) {
                return false;
            }
            return instruction.type == IRType::ASSIGN || instruction.type == IRType::ADD || instruction.type == IRType::SUB ||
                   instruction.type == IRType::PUSH || instruction.type == IRType::POP;
        }

        size_t nextMeaningfulIndex(const size_t from) const
        {
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
        }

        std::string normalizeOperand(const std::string& operand) const
        {
            return normalizeOperandForDisplay(operand, stackFrameLayout);
        }

        std::optional<size_t> argumentIndexForRegister(const std::string& registerName) const
        {
            return argumentIndexForRegisterName(registerName, callingConvention);
        }

        std::string resolveRegisterBaseAt(const std::string& base, const size_t index) const
        {
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
        }

        std::string renderMemoryAddress(const IRMemoryAddress& memory, const std::string& original, const size_t index) const
        {
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
        }

        std::string renderMemoryOperand(const IROperand& operand, const size_t index) const
        {
            if (operand.memory.has_value()) {
                return renderMemoryAddress(*operand.memory, operand.value, index);
            }
            return normalizeOperand(operand.value);
        }

        std::string renderInlineMemoryText(const std::string& operand, const size_t index) const
        {
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
        }

        std::string normalizeMemoryReferences(std::string expression, const size_t index) const
        {
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
        }

        std::string renderExpression(const IROperand& operand, const size_t index) const
        {
            if (operand.type == OpType::MEM) {
                return normalizeImmediateForDisplay(renderMemoryOperand(operand, index));
            }
            std::string value = normalizeOperand(operand.value);
            if (operand.kind == OperandKind::SsaTemp || isTemporaryName(value)) {
                value = substituteTempFromDefinitions(instructions, index, operand.value, stackFrameLayout);
            }
            return renderSimplifiedExpression(makeExpressionFromText(normalizeImmediateForDisplay(normalizeMemoryReferences(value, index))));
        }

        std::optional<std::string> tryBuildAssignedExpression(const size_t index) const
        {
            const auto& inst           = instructions[index];
            const std::string srcExpr  = renderExpression(IRProperties::operandAt(inst, 1), index);
            const std::string src2Expr = renderExpression(IRProperties::operandAt(inst, 2), index);

            if (IRProperties::isSimpleValueProducer(inst.type)) {
                if (srcExpr.empty()) {
                    return std::nullopt;
                }
                return renderSimplifiedExpression(makeExpressionFromText(srcExpr));
            }
            if (IRProperties::isBinaryExpression(inst.type)) {
                if (srcExpr.empty() || src2Expr.empty()) {
                    return std::nullopt;
                }
                return buildBinaryExpressionText(inst.type, srcExpr, src2Expr);
            }
            if (inst.type == IRType::NEG || inst.type == IRType::NOT) {
                if (srcExpr.empty()) {
                    return std::nullopt;
                }
                const UnaryOp op = inst.type == IRType::NEG ? UnaryOp::Negate : UnaryOp::BitwiseNot;
                return renderSimplifiedExpression(std::make_unique<UnaryExpression>(op, makeExpressionFromText(srcExpr)));
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
        }

        bool isComposableTemporary(const IROperand& operand) const
        {
            return operand.kind == OperandKind::SsaTemp || (operand.type == OpType::REG && isTemporaryName(normalizeOperand(operand.value)));
        }

        bool shouldSkipBeforeLowering(const size_t index) const
        {
            if (absorbedIndices.contains(index)) {
                return true;
            }

            const auto& inst = instructions[index];
            if (isSyntheticSsaCopy(inst) || IRProperties::isPhi(inst) || IRProperties::isNop(inst)) {
                return true;
            }
            if (IRProperties::isJump(inst.type)) {
                return true;
            }

            if (inst.type == IRType::TEST || inst.type == IRType::CMP) {
                const size_t nextIdx = nextMeaningfulIndex(index + 1);
                return nextIdx < instructions.size() && instructions[nextIdx].type == IRType::SETCC;
            }
            return false;
        }

        bool shouldSkipStackFrameInstruction(const IRInstruction& inst) const
        {
            if (inst.type == IRType::PUSH && isFramePointerRegisterName(IRProperties::operandAt(inst, 1).value)) {
                return true;
            }
            if (inst.type == IRType::POP && isFramePointerRegisterName(IRProperties::operandAt(inst, 0).value)) {
                return true;
            }
            return inst.type == IRType::RET;
        }

        bool isReturnSpillInstruction(const IRInstruction& instruction) const
        {
            const bool isSpillType = instruction.type == IRType::ASSIGN || instruction.type == IRType::STORE;
            if (!isSpillType || !IRProperties::hasOperandAt(instruction, 0) || !IRProperties::hasOperandAt(instruction, 1)) {
                return false;
            }

            const auto& dest                  = IRProperties::operandAt(instruction, 0);
            const auto& src                   = IRProperties::operandAt(instruction, 1);
            const bool sourceIsReturnRegister = isReturnRegisterName(src.value);
            const bool destIsReturnRegister   = isReturnRegisterName(dest.value);
            const bool destIsTemporary        = dest.kind == OperandKind::SsaTemp;
            return sourceIsReturnRegister && !destIsReturnRegister && !destIsTemporary;
        }

        std::string buildCallName(const size_t index) const
        {
            const auto& call     = instructions[index];
            std::string callName = renderExpression(IRProperties::operandAt(call, 0), index);
            if (isTemporaryName(callName)) {
                callName = normalizeImmediateForDisplay(
                      substituteTempFromDefinitions(instructions, index, IRProperties::operandAt(call, 0).value, stackFrameLayout));
            }
            if (callName.empty()) {
                callName = "call";
            }

            callName                    = llvm::demangle(callName);
            const auto callNameParenPos = callName.find('(');
            if (callNameParenPos != std::string::npos) {
                callName = callName.substr(0, callNameParenPos);
            }
            return callName;
        }

        std::map<size_t, std::pair<size_t, std::string>> collectCallArguments(const size_t callIndex) const
        {
            std::map<size_t, std::pair<size_t, std::string>> argsByIndex;
            for (size_t j = callIndex; j-- > 0;) {
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
                if (!IRProperties::isSimpleValueProducer(candidate.type)) {
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
            return argsByIndex;
        }

        std::string buildCallExpression(const size_t callIndex)
        {
            std::string callExpr = buildCallName(callIndex) + "(";
            bool firstArg        = true;

            for (const auto& argument : collectCallArguments(callIndex)) {
                const auto& argInfo = argument.second;
                if (!firstArg) {
                    callExpr += ", ";
                }
                callExpr += argInfo.second;
                absorbedIndices.insert(argInfo.first);
                firstArg = false;
            }
            callExpr += ")";
            return callExpr;
        }

        bool tryLowerBoolCallSpill(const std::string& callExpr, const size_t testIndex)
        {
            if (testIndex >= instructions.size() || instructions[testIndex].type != IRType::TEST) {
                return false;
            }

            const size_t setccIdx = nextMeaningfulIndex(testIndex + 1);
            if (setccIdx >= instructions.size() || instructions[setccIdx].type != IRType::SETCC) {
                return false;
            }

            size_t nextIdx = nextMeaningfulIndex(setccIdx + 1);
            std::vector<size_t> intermediateIndices;
            while (nextIdx < instructions.size() && (instructions[nextIdx].type == IRType::ASSIGN || instructions[nextIdx].type == IRType::LOAD) &&
                   IRProperties::operandAt(instructions[nextIdx], 0).kind == OperandKind::SsaTemp &&
                   isReturnRegisterName(IRProperties::operandAt(instructions[nextIdx], 0).value)) {
                intermediateIndices.push_back(nextIdx);
                nextIdx = nextMeaningfulIndex(nextIdx + 1);
            }

            if (nextIdx >= instructions.size()) {
                return false;
            }

            const auto& boolSpill = instructions[nextIdx];
            if (!isReturnSpillInstruction(boolSpill)) {
                return false;
            }

            const std::string boolDest = normalizeOperand(IRProperties::operandAt(boolSpill, 0).value);
            pushAssignment(statements, boolDest, callExpr);
            absorbedIndices.insert(testIndex);
            absorbedIndices.insert(setccIdx);
            for (const size_t index : intermediateIndices) {
                absorbedIndices.insert(index);
            }
            absorbedIndices.insert(nextIdx);
            return true;
        }

        bool tryLowerCallSpill(const std::string& callExpr, const size_t spillIndex)
        {
            if (spillIndex >= instructions.size()) {
                return false;
            }

            const auto& spill = instructions[spillIndex];
            if (!isReturnSpillInstruction(spill)) {
                return false;
            }

            const std::string spillDest = normalizeOperand(IRProperties::operandAt(spill, 0).value);
            pushAssignment(statements, spillDest, callExpr);
            absorbedIndices.insert(spillIndex);
            return true;
        }

        bool tryLowerCall(const size_t index)
        {
            if (instructions[index].type != IRType::CALL) {
                return false;
            }

            const std::string callExpr = buildCallExpression(index);
            const size_t spillIndex    = nextMeaningfulIndex(index + 1);

            if (tryLowerBoolCallSpill(callExpr, spillIndex)) {
                return true;
            }
            if (tryLowerCallSpill(callExpr, spillIndex)) {
                return true;
            }

            pushExpressionStatement(statements, callExpr);
            return true;
        }

        bool tryLowerForwardedTemporary(size_t& index)
        {
            const auto& inst = instructions[index];
            if (!isComposableTemporary(IRProperties::operandAt(inst, 0))) {
                return false;
            }

            const size_t forwardedIndex = nextMeaningfulIndex(index + 1);
            if (forwardedIndex >= instructions.size()) {
                return false;
            }

            const auto& forwarded           = instructions[forwardedIndex];
            const bool forwardsToAssignment = (forwarded.type == IRType::ASSIGN || forwarded.type == IRType::STORE) &&
                                              IRProperties::operandAt(forwarded, 1).value == IRProperties::operandAt(inst, 0).value;
            if (!forwardsToAssignment) {
                const bool forwardsToCall =
                      forwarded.type == IRType::CALL && IRProperties::operandAt(forwarded, 0).value == IRProperties::operandAt(inst, 0).value;
                if (forwardsToCall && tryBuildAssignedExpression(index).has_value()) {
                    return true;
                }
                return false;
            }

            std::string finalDest = normalizeOperand(IRProperties::operandAt(forwarded, 0).value);
            if (forwarded.type == IRType::STORE) {
                finalDest = renderExpression(IRProperties::operandAt(forwarded, 0), forwardedIndex);
            }

            const auto expression = tryBuildAssignedExpression(index);
            if (!expression.has_value()) {
                return false;
            }

            const size_t afterForward = nextMeaningfulIndex(forwardedIndex + 1);
            const bool isReturnAssignment =
                  isReturnRegisterName(finalDest) && afterForward < instructions.size() && instructions[afterForward].type == IRType::RET;
            if (isReturnAssignment) {
                pushReturn(statements, *expression);
                index = afterForward;
                return true;
            }

            if (!finalDest.empty() && IRProperties::operandAt(forwarded, 0).kind != OperandKind::SsaTemp) {
                pushAssignment(statements, finalDest, *expression);
                index = forwardedIndex;
                return true;
            }

            return false;
        }

        bool shouldSkipTemporaryFeedingAnotherTemporary(const size_t index) const
        {
            const auto& inst = instructions[index];
            if (!isComposableTemporary(IRProperties::operandAt(inst, 0))) {
                return false;
            }

            const size_t forwardedIndex = nextMeaningfulIndex(index + 1);
            if (forwardedIndex >= instructions.size()) {
                return false;
            }

            const auto& forwarded = instructions[forwardedIndex];
            const bool forwardedIsTemporaryExpression =
                  IRProperties::isPureExpression(forwarded.type) && isComposableTemporary(IRProperties::operandAt(forwarded, 0));
            const bool forwardedUsesCurrentTemporary = IRProperties::operandAt(forwarded, 1).value == IRProperties::operandAt(inst, 0).value ||
                                                       IRProperties::operandAt(forwarded, 2).value == IRProperties::operandAt(inst, 0).value;
            return forwardedIsTemporaryExpression && forwardedUsesCurrentTemporary;
        }

        bool shouldSkipTemporaryUsedAsCallArgument(const size_t index) const
        {
            const auto& inst = instructions[index];
            if (!isComposableTemporary(IRProperties::operandAt(inst, 0))) {
                return false;
            }
            if (!argumentIndexForRegister(IRProperties::operandAt(inst, 0).value).has_value()) {
                return false;
            }

            const size_t nextIdx = nextMeaningfulIndex(index + 1);
            return nextIdx < instructions.size() && instructions[nextIdx].type == IRType::CALL;
        }

        bool tryLowerForwardedAddSub(size_t& index, const std::string& src)
        {
            const auto& inst = instructions[index];
            if (inst.type != IRType::ADD && inst.type != IRType::SUB) {
                return false;
            }
            if (index + 1 >= instructions.size() || instructions[index + 1].type != IRType::ASSIGN) {
                return false;
            }
            if (IRProperties::operandAt(instructions[index + 1], 1).value != IRProperties::operandAt(inst, 0).value) {
                return false;
            }

            const std::string finalDest  = normalizeOperand(IRProperties::operandAt(instructions[index + 1], 0).value);
            const std::string rhs        = renderExpression(IRProperties::operandAt(inst, 2), index);
            const std::string expression = buildBinaryExpressionText(inst.type, src, rhs);
            const size_t afterAssign     = nextMeaningfulIndex(index + 2);
            const bool isReturnAssignment =
                  isReturnRegisterName(finalDest) && afterAssign < instructions.size() && instructions[afterAssign].type == IRType::RET;

            if (isReturnAssignment) {
                pushReturn(statements, expression);
                index = afterAssign;
                return true;
            }

            if (inst.type == IRType::ADD) {
                lowerAdd(finalDest, src, rhs);
            } else {
                lowerSub(finalDest, src, rhs);
            }

            index += 1;
            return true;
        }

        bool tryLowerAddSubReturn(const size_t index, const std::string& dest, const std::string& src)
        {
            const auto& inst = instructions[index];
            if (inst.type != IRType::ADD && inst.type != IRType::SUB) {
                return false;
            }
            if (!isReturnRegisterName(dest)) {
                return false;
            }

            const size_t nextIdx = nextMeaningfulIndex(index + 1);
            if (nextIdx >= instructions.size() || instructions[nextIdx].type != IRType::RET) {
                return false;
            }

            const std::string rhs        = normalizeImmediateForDisplay(normalizeOperand(IRProperties::operandAt(inst, 2).value));
            const std::string expression = buildBinaryExpressionText(inst.type, src, rhs);
            pushReturn(statements, expression);
            return true;
        }

        bool tryLowerDirectReturn(const size_t index, const std::string& dest, const std::string& src, const bool assignmentLike)
        {
            const auto& inst = instructions[index];
            if ((inst.type != IRType::LOAD_CONST && !assignmentLike) || !isReturnRegisterName(dest)) {
                return false;
            }

            const size_t nextIdx = nextMeaningfulIndex(index + 1);
            const bool atReturnPoint =
                  nextIdx < instructions.size() && (instructions[nextIdx].type == IRType::RET || instructions[nextIdx].type == IRType::JMP);
            if (!atReturnPoint) {
                return false;
            }

            if (src.empty()) {
                pushReturn(statements, {});
            } else {
                pushReturn(statements, normalizeImmediateForDisplay(src));
            }
            return true;
        }

        bool shouldSkipExpressionNoise(const size_t index, const std::string& dest, const std::string& src, const bool assignmentLike) const
        {
            const auto& inst = instructions[index];
            if (inst.type == IRType::LOAD_CONST && IRProperties::operandAt(inst, 0).kind == OperandKind::SsaTemp &&
                !isUsedLater(IRProperties::operandAt(inst, 0).value, index)) {
                return true;
            }
            if (IRProperties::isPureExpression(inst.type) && isComposableTemporary(IRProperties::operandAt(inst, 0))) {
                return true;
            }
            if (assignmentLike && isFramePointerRegisterName(dest) && isStackPointerRegisterName(src)) {
                return true;
            }
            const bool assignsNamedVariableToTemporary =
                  assignmentLike && IRProperties::operandAt(inst, 0).kind == OperandKind::SsaTemp &&
                  (IRProperties::operandAt(inst, 1).kind == OperandKind::StackVar || IRProperties::operandAt(inst, 1).kind == OperandKind::GlobalVar);
            if (assignsNamedVariableToTemporary) {
                return true;
            }
            const bool unusedAddSubTemporary = (inst.type == IRType::ADD || inst.type == IRType::SUB) &&
                                               IRProperties::operandAt(inst, 0).kind == OperandKind::SsaTemp &&
                                               !isUsedLater(IRProperties::operandAt(inst, 0).value, index);
            return unusedAddSubTemporary;
        }

        void lowerAdd(const std::string& dest, const std::string& src, const std::string& rhs)
        {
            if (dest == src && rhs == "1") {
                pushUpdate(statements, dest, UnaryOp::PostIncrement);
            } else if (dest == src && rhs == "-1") {
                pushUpdate(statements, dest, UnaryOp::PostDecrement);
            } else {
                pushAssignment(statements, dest, src + " + " + rhs);
            }
        }

        void lowerSub(const std::string& dest, const std::string& src, const std::string& rhs)
        {
            if (dest == src && rhs == "1") {
                pushUpdate(statements, dest, UnaryOp::PostDecrement);
            } else {
                pushAssignment(statements, dest, src + " - " + rhs);
            }
        }

        void lowerBinaryAssignment(const IRInstruction& inst, const size_t index, const std::string& dest, const std::string& src)
        {
            const std::string rhs        = renderExpression(IRProperties::operandAt(inst, 2), index);
            const std::string expression = buildBinaryExpressionText(inst.type, src, rhs);
            if (!expression.empty()) {
                pushAssignment(statements, dest, expression);
            }
        }

        void lowerRegularInstruction(const size_t index, const std::string& dest, const std::string& src, const bool assignmentLike)
        {
            const auto& inst = instructions[index];
            if (assignmentLike) {
                pushAssignment(statements, dest, src);
            } else if (inst.type == IRType::ADD) {
                lowerAdd(dest, src, renderExpression(IRProperties::operandAt(inst, 2), index));
            } else if (inst.type == IRType::SUB) {
                lowerSub(dest, src, renderExpression(IRProperties::operandAt(inst, 2), index));
            } else if (IRProperties::isBinaryExpression(inst.type)) {
                lowerBinaryAssignment(inst, index, dest, src);
            } else if (inst.type == IRType::NEG || inst.type == IRType::NOT) {
                if (!src.empty()) {
                    const UnaryOp op = inst.type == IRType::NEG ? UnaryOp::Negate : UnaryOp::BitwiseNot;
                    pushAssignment(statements, dest, renderSimplifiedExpression(std::make_unique<UnaryExpression>(op, makeExpressionFromText(src))));
                }
            } else if (inst.type == IRType::SETCC) {
                if (const auto expression = tryBuildAssignedExpression(index); expression.has_value()) {
                    pushAssignment(statements, dest, *expression);
                }
            }
        }

        void lowerInstruction(size_t& index)
        {
            if (shouldSkipBeforeLowering(index)) {
                return;
            }

            const auto& inst = instructions[index];
            if (tryLowerCall(index)) {
                return;
            }
            if (shouldSkipStackFrameInstruction(inst)) {
                return;
            }

            std::string dest = normalizeOperand(IRProperties::operandAt(inst, 0).value);
            std::string src  = renderExpression(IRProperties::operandAt(inst, 1), index);
            if (inst.type == IRType::STORE) {
                dest = renderExpression(IRProperties::operandAt(inst, 0), index);
            }

            const bool assignmentLike = IRProperties::isAssignmentLike(inst.type);
            if (isStackPointerRegisterName(dest) || isStackPointerRegisterName(src)) {
                return;
            }

            const auto destArgIndex = parseStackArgumentSymbolIndex(dest);
            const auto srcArgIndex  = argumentIndexForRegister(src);
            if (assignmentLike && destArgIndex.has_value() && srcArgIndex.has_value() && *destArgIndex == *srcArgIndex) {
                return;
            }

            if (tryLowerForwardedTemporary(index)) {
                return;
            }
            if (shouldSkipTemporaryFeedingAnotherTemporary(index)) {
                return;
            }
            if (shouldSkipTemporaryUsedAsCallArgument(index)) {
                return;
            }
            if (tryLowerForwardedAddSub(index, src)) {
                return;
            }
            if (tryLowerAddSubReturn(index, dest, src)) {
                return;
            }
            if (tryLowerDirectReturn(index, dest, src, assignmentLike)) {
                return;
            }
            if (shouldSkipExpressionNoise(index, dest, src, assignmentLike)) {
                return;
            }
            if (assignmentLike && !src.empty() && dest == src) {
                return;
            }

            lowerRegularInstruction(index, dest, src, assignmentLike);
        }
    };
} // namespace

std::vector<std::unique_ptr<ASTNode>> lowerBlockToStatements(const std::vector<IRInstruction>& instructions, const CallingConvention calling_convention)
{
    StatementLoweringContext context(instructions, calling_convention);
    return context.lower();
}
} // namespace Decompiler
