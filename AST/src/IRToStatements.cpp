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
#include "WindowsX64.h"
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
        case IRType::SMUL:
            return BinaryOp::Mul;
        case IRType::DIV:
        case IRType::SDIV:
            return BinaryOp::Div;
        case IRType::MOD:
        case IRType::SMOD:
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
        if (target == value) {
            return;
        }
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
        StatementLoweringContext(const std::vector<IRInstruction>& blockInstructions) : instructions(blockInstructions)
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

        static bool isSyntheticSsaCopy(const IRInstruction& instruction)
        {
            return instruction.address == 0 && IRProperties::isAssign(instruction);
        }

        bool isUsedLater(const std::string& value, const size_t fromIndex) const
        {
            if (value.empty()) {
                return false;
            }
            for (size_t j = fromIndex + 1; j < instructions.size(); ++j) {
                const auto& next = instructions[j];
                if (IRProperties::operandAt(next, 1).name == value || IRProperties::operandAt(next, 2).name == value ||
                    IRProperties::operandAt(next, 0).name == value) {
                    return true;
                }
            }
            return false;
        }

        bool isEpilogueNoise(const IRInstruction& instruction) const
        {
            const std::string dest      = IRProperties::operandAt(instruction, 0).name;
            const std::string source    = IRProperties::operandAt(instruction, 1).name;
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
            return operand;
        }

        std::optional<size_t> argumentIndexForRegister(const std::string& registerName) const
        {
            return argumentIndexForRegisterName(registerName);
        }

        std::string resolveRegisterBaseAt(const std::string& base, const size_t index) const
        {
            if (isTemporaryName(base)) {
                return substituteTempFromDefinitions(instructions, index, base);
            }

            for (size_t j = index; j-- > 0;) {
                const auto& previous = instructions[j];
                if (normalizedRegisterBase(IRProperties::operandAt(previous, 0).name) != base) {
                    continue;
                }

                if (previous.type == IRType::ASSIGN || previous.type == IRType::LOAD || previous.type == IRType::LOAD_CONST || previous.type == IRType::SEXT ||
                    previous.type == IRType::LEA) {
                    std::string value = normalizeOperand(IRProperties::operandAt(previous, 1).name);
                    if (IRProperties::operandAt(previous, 1).tag == OperandTag::SsaTemp) {
                        value = substituteTempFromDefinitions(instructions, j, IRProperties::operandAt(previous, 1).name);
                    }
                    return normalizeImmediateForDisplay(value);
                }
                break;
            }
            return base;
        }

        std::string renderMemoryAddress(const HeapDerefData& memory, const std::string& original, const size_t index) const
        {
            if (memory.ripRelative) {
                return normalizeOperand(original);
            }

            if (!memory.base.empty() && memory.index.empty() && memory.displacement == 0) {
                const std::string arrayAccess = ASTDetail::renderArrayAccess(instructions, index, memory.base);
                if (!arrayAccess.empty()) {
                    return arrayAccess;
                }
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
                if (baseExpr.find(' ') != std::string::npos) {
                    return "*(" + baseExpr + ")";
                }
                return "*" + baseExpr;
            }

            return normalizeOperand(original);
        }

        std::string renderMemoryOperand(const IROperand& operand, const size_t index) const
        {
            if (operand.isHeapDeref()) {
                return renderMemoryAddress(operand.heapDeref, operand.name, index);
            }
            return normalizeOperand(operand.name);
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

            bool insideIsIdentifier = !inside.empty();
            for (size_t ci = 0; insideIsIdentifier && ci < inside.size(); ++ci) {
                const unsigned char ch = static_cast<unsigned char>(inside[ci]);
                insideIsIdentifier     = std::isalnum(ch) != 0 || inside[ci] == '_';
            }
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
            if (operand.tag == OperandTag::HeapDeref) {
                return normalizeImmediateForDisplay(renderMemoryOperand(operand, index));
            }
            if (operand.tag == OperandTag::StackVar && !operand.arrayIndex.empty()) {
                std::string indexText = substituteTempFromDefinitions(instructions, index, operand.arrayIndex);
                if (indexText.empty()) {
                    indexText = normalizeOperand(operand.arrayIndex);
                }
                const bool indexIsElementIndex = operand.arrayIndexScale > 1 && static_cast<uint32_t>(operand.arrayIndexScale) == operand.sizeBytes;
                if (!indexIsElementIndex && operand.arrayIndexScale > 1) {
                    indexText += " * " + std::to_string(operand.arrayIndexScale);
                }
                return operand.name + "[" + indexText + "]";
            }
            std::string value = normalizeOperand(operand.name);
            if (operand.tag == OperandTag::SsaTemp || isTemporaryName(value)) {
                value = substituteTempFromDefinitions(instructions, index, operand.name);
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
                    if (flag.type == IRType::TEST && IRProperties::operandAt(flag, 1).name == IRProperties::operandAt(flag, 2).name) {
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
            return operand.tag == OperandTag::SsaTemp || (operand.tag == OperandTag::Register && isTemporaryName(normalizeOperand(operand.name)));
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
            if (inst.type == IRType::PUSH && isFramePointerRegisterName(IRProperties::operandAt(inst, 1).name)) {
                return true;
            }
            if (inst.type == IRType::POP && isFramePointerRegisterName(IRProperties::operandAt(inst, 0).name)) {
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
            const bool sourceIsReturnRegister = isReturnRegisterName(src.name);
            const bool destIsReturnRegister   = isReturnRegisterName(dest.name);
            const bool destIsTemporary        = dest.tag == OperandTag::SsaTemp;
            return sourceIsReturnRegister && !destIsReturnRegister && !destIsTemporary;
        }

        std::string buildCallName(const size_t index) const
        {
            const auto& call     = instructions[index];
            std::string callName = renderExpression(IRProperties::operandAt(call, 0), index);
            if (isTemporaryName(callName)) {
                callName = normalizeImmediateForDisplay(substituteTempFromDefinitions(instructions, index, IRProperties::operandAt(call, 0).name));
            }
            if (callName.empty()) {
                callName = "call";
            }

            callName                    = ASTDetail::stripReturnType(llvm::demangle(callName));
            const auto callNameParenPos = callName.find('(');
            if (callNameParenPos != std::string::npos) {
                callName = callName.substr(0, callNameParenPos);
            }
            return callName;
        }

        static bool isCallReturnTemp(const std::string& value)
        {
            return startsWithAny(value, { "rax_", "eax_", "ax_", "al_", "xmm0_" });
        }

        std::optional<size_t> findProducingCallIndex(const size_t beforeIndex) const
        {
            for (size_t k = beforeIndex; k-- > 0;) {
                if (instructions[k].type == IRType::CALL) {
                    return k;
                }
                const std::string destBase = normalizedRegisterBase(IRProperties::operandAt(instructions[k], 0).name);
                if (destBase == "rax" || destBase == "xmm0") {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        std::map<size_t, std::pair<size_t, std::string>> collectCallArguments(const size_t callIndex)
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
                if (!argDest.isAnyReg()) {
                    continue;
                }

                const auto argIdx = argumentIndexForRegister(argDest.name);
                if (!argIdx.has_value() || argsByIndex.contains(*argIdx)) {
                    continue;
                }

                const auto& srcOperand    = IRProperties::operandAt(candidate, 1);
                const std::string argExpr = renderExpression(srcOperand, j);
                if (argExpr.empty()) {
                    continue;
                }

                if (isCallReturnTemp(srcOperand.name) && argExpr == srcOperand.name) {
                    const auto innerCallOpt = findProducingCallIndex(j);
                    if (innerCallOpt.has_value() && !absorbedIndices.contains(*innerCallOpt)) {
                        const std::string inlinedExpr = buildCallExpression(*innerCallOpt);
                        absorbedIndices.insert(*innerCallOpt);
                        argsByIndex[*argIdx] = { j, inlinedExpr };
                        continue;
                    }
                }

                argsByIndex[*argIdx] = { j, argExpr };
            }

            if (argsByIndex.empty()) {
                for (size_t j = callIndex; j-- > 0;) {
                    const auto& candidate = instructions[j];
                    if (candidate.type == IRType::CALL || IRProperties::isControlFlow(candidate.type)) {
                        break;
                    }
                    if (candidate.type != IRType::STORE || !IRProperties::hasOperandAt(candidate, 0) || !IRProperties::hasOperandAt(candidate, 1)) {
                        continue;
                    }
                    const auto& argDest = candidate.operands[0];
                    if (argDest.tag != OperandTag::StackVar || argDest.stackOffset < 0) {
                        continue;
                    }
                    const size_t argIdx = static_cast<size_t>(argDest.stackOffset) / 4;
                    if (argsByIndex.contains(argIdx)) {
                        continue;
                    }
                    const std::string argExpr = renderExpression(IRProperties::operandAt(candidate, 1), j);
                    if (!argExpr.empty()) {
                        argsByIndex[argIdx] = { j, argExpr };
                    }
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
                   IRProperties::operandAt(instructions[nextIdx], 0).tag == OperandTag::SsaTemp &&
                   isReturnRegisterName(IRProperties::operandAt(instructions[nextIdx], 0).name)) {
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

            const std::string boolDest = normalizeOperand(IRProperties::operandAt(boolSpill, 0).name);
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

            const std::string spillDest = normalizeOperand(IRProperties::operandAt(spill, 0).name);
            pushAssignment(statements, spillDest, callExpr);
            absorbedIndices.insert(spillIndex);
            return true;
        }

        bool callResultIsConsumed(const size_t callIndex) const
        {
            for (size_t i = callIndex + 1; i < instructions.size(); ++i) {
                const auto& inst = instructions[i];
                for (size_t op = 0; op < inst.operands.size(); ++op) {
                    if (IRProperties::usesOperandAt(inst, op) && isReturnRegisterName(inst.operands[op].name)) {
                        return true;
                    }
                }
                if (IRProperties::isCall(inst.type)) {
                    return false;
                }
                if (IRProperties::definesOperandAt(inst, 0) && isReturnRegisterName(IRProperties::operandAt(inst, 0).name)) {
                    return false;
                }
            }
            return false;
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

            if (spillIndex < instructions.size()) {
                const auto& between        = instructions[spillIndex];
                const bool knownConversion = (between.type == IRType::SEXT || between.type == IRType::ASSIGN) && IRProperties::hasOperandAt(between, 1) &&
                                             isReturnRegisterName(IRProperties::operandAt(between, 1).name);
                const bool unknownConversion = between.type == IRType::UNKNOWN && IRProperties::hasOperandAt(between, 0) &&
                                               isReturnRegisterName(IRProperties::operandAt(between, 0).name) && IRProperties::hasOperandAt(between, 1) &&
                                               isReturnRegisterName(IRProperties::operandAt(between, 1).name);
                const bool isReturnConversion = knownConversion || unknownConversion;
                if (isReturnConversion) {
                    const size_t afterConversion = nextMeaningfulIndex(spillIndex + 1);
                    if (tryLowerCallSpill(callExpr, afterConversion)) {
                        absorbedIndices.insert(spillIndex);
                        return true;
                    }
                }
            }

            if (callResultIsConsumed(index)) {
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
                                              IRProperties::operandAt(forwarded, 1).name == IRProperties::operandAt(inst, 0).name;
            if (!forwardsToAssignment) {
                const bool forwardsToCall =
                      forwarded.type == IRType::CALL && IRProperties::operandAt(forwarded, 0).name == IRProperties::operandAt(inst, 0).name;
                if (forwardsToCall && tryBuildAssignedExpression(index).has_value()) {
                    return true;
                }
                return false;
            }

            std::string finalDest = normalizeOperand(IRProperties::operandAt(forwarded, 0).name);
            if (forwarded.type == IRType::STORE) {
                finalDest = renderExpression(IRProperties::operandAt(forwarded, 0), forwardedIndex);
            }

            const auto expression = tryBuildAssignedExpression(index);
            if (!expression.has_value()) {
                return false;
            }

            const size_t afterForward     = nextMeaningfulIndex(forwardedIndex + 1);
            const bool isReturnDest       = isReturnRegisterName(finalDest) || normalizedRegisterBase(finalDest) == "xmm0";
            const bool isReturnAssignment = isReturnDest && afterForward < instructions.size() && instructions[afterForward].type == IRType::RET;
            if (isReturnAssignment) {
                pushReturn(statements, *expression);
                index = afterForward;
                return true;
            }

            if (!finalDest.empty() && IRProperties::operandAt(forwarded, 0).tag != OperandTag::SsaTemp) {
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
            const bool forwardedUsesCurrentTemporary = IRProperties::operandAt(forwarded, 1).name == IRProperties::operandAt(inst, 0).name ||
                                                       IRProperties::operandAt(forwarded, 2).name == IRProperties::operandAt(inst, 0).name;
            return forwardedIsTemporaryExpression && forwardedUsesCurrentTemporary;
        }

        bool shouldSkipTemporaryUsedAsCallArgument(const size_t index) const
        {
            const auto& inst = instructions[index];
            if (!isComposableTemporary(IRProperties::operandAt(inst, 0))) {
                return false;
            }
            if (!argumentIndexForRegister(IRProperties::operandAt(inst, 0).name).has_value()) {
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
            if (IRProperties::operandAt(instructions[index + 1], 1).name != IRProperties::operandAt(inst, 0).name) {
                return false;
            }

            const std::string finalDest   = normalizeOperand(IRProperties::operandAt(instructions[index + 1], 0).name);
            const std::string rhs         = renderExpression(IRProperties::operandAt(inst, 2), index);
            const std::string expression  = buildBinaryExpressionText(inst.type, src, rhs);
            const size_t afterAssign      = nextMeaningfulIndex(index + 2);
            const bool isReturnAssignment = (isReturnRegisterName(finalDest) || normalizedRegisterBase(finalDest) == "xmm0") &&
                                            afterAssign < instructions.size() && instructions[afterAssign].type == IRType::RET;

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

        bool tryLowerBinaryReturn(const size_t index, const std::string& dest, const std::string& src)
        {
            const auto& inst = instructions[index];
            if (!IRProperties::isBinaryExpression(inst.type)) {
                return false;
            }
            if (!isReturnRegisterName(dest) && normalizedRegisterBase(dest) != "xmm0") {
                return false;
            }

            const size_t nextIdx     = nextMeaningfulIndex(index + 1);
            const bool atReturnPoint = nextIdx >= instructions.size() || instructions[nextIdx].type == IRType::RET;
            if (!atReturnPoint) {
                return false;
            }

            const std::string rhs        = renderExpression(IRProperties::operandAt(inst, 2), index);
            const std::string expression = buildBinaryExpressionText(inst.type, src, rhs);
            pushReturn(statements, expression);
            return true;
        }

        bool tryLowerDirectReturn(const size_t index, const std::string& dest, const std::string& src, const bool assignmentLike)
        {
            const auto& inst        = instructions[index];
            const bool isReturnDest = isReturnRegisterName(dest) || normalizedRegisterBase(dest) == "xmm0";
            if ((inst.type != IRType::LOAD_CONST && !assignmentLike) || !isReturnDest) {
                return false;
            }

            const size_t nextIdx     = nextMeaningfulIndex(index + 1);
            const bool atReturnPoint = nextIdx >= instructions.size() || instructions[nextIdx].type == IRType::RET || instructions[nextIdx].type == IRType::JMP;
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
            if (inst.type == IRType::LOAD_CONST && IRProperties::operandAt(inst, 0).tag == OperandTag::SsaTemp &&
                !isUsedLater(IRProperties::operandAt(inst, 0).name, index)) {
                return true;
            }
            if (IRProperties::isPureExpression(inst.type) && isComposableTemporary(IRProperties::operandAt(inst, 0))) {
                if (normalizedRegisterBase(IRProperties::operandAt(inst, 0).name) == "xmm0") {
                    const size_t nextIdx = nextMeaningfulIndex(index + 1);
                    if (nextIdx < instructions.size() && instructions[nextIdx].type == IRType::RET) {
                        return false;
                    }
                }
                return true;
            }
            if (assignmentLike && isFramePointerRegisterName(dest) && isStackPointerRegisterName(src)) {
                return true;
            }
            const bool assignsNamedVariableToTemporary =
                  assignmentLike && IRProperties::operandAt(inst, 0).tag == OperandTag::SsaTemp &&
                  (IRProperties::operandAt(inst, 1).tag == OperandTag::StackVar || IRProperties::operandAt(inst, 1).tag == OperandTag::GlobalVar);
            if (assignsNamedVariableToTemporary) {
                return true;
            }
            const bool unusedAddSubTemporary = (inst.type == IRType::ADD || inst.type == IRType::SUB) &&
                                               IRProperties::operandAt(inst, 0).tag == OperandTag::SsaTemp &&
                                               !isUsedLater(IRProperties::operandAt(inst, 0).name, index);
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

        bool tryLowerConvert(const size_t index)
        {
            const auto& inst = instructions[index];
            if (inst.type != IRType::CONVERT) {
                return false;
            }
            const auto& destOp = IRProperties::operandAt(inst, 0);
            for (size_t j = index + 1; j < instructions.size(); ++j) {
                if (isSyntheticSsaCopy(instructions[j]) || IRProperties::isNop(instructions[j])) {
                    continue;
                }
                if (IRProperties::operandAt(instructions[j], 0).name == destOp.name || IRProperties::operandAt(instructions[j], 1).name == destOp.name ||
                    IRProperties::operandAt(instructions[j], 2).name == destOp.name) {
                    return true;
                }
            }

            std::string value = renderExpression(IRProperties::operandAt(inst, 1), index);
            if (value.find(' ') != std::string::npos) {
                value = "(" + value + ")";
            }
            const std::string cast = "(" + ASTDetail::convertCastType(destOp) + ")" + value;

            const std::string dest  = normalizeOperand(destOp.name);
            const bool isReturnDest = isReturnRegisterName(dest) || normalizedRegisterBase(dest) == "xmm0";

            size_t nextIdx = index + 1;
            while (nextIdx < instructions.size() && (IRProperties::isNop(instructions[nextIdx]) || isSyntheticSsaCopy(instructions[nextIdx]) ||
                                                     shouldSkipStackFrameInstruction(instructions[nextIdx]))) {
                ++nextIdx;
            }
            const bool atReturnPoint = nextIdx >= instructions.size() || instructions[nextIdx].type == IRType::RET || instructions[nextIdx].type == IRType::JMP;
            if (isReturnDest && atReturnPoint) {
                pushReturn(statements, cast);
            } else {
                pushAssignment(statements, dest, cast);
            }
            return true;
        }

        void lowerInstruction(size_t& index)
        {
            if (shouldSkipBeforeLowering(index)) {
                return;
            }

            const auto& inst = instructions[index];
            if (tryLowerConvert(index)) {
                return;
            }
            if (tryLowerCall(index)) {
                return;
            }
            if (shouldSkipStackFrameInstruction(inst)) {
                return;
            }

            std::string dest = normalizeOperand(IRProperties::operandAt(inst, 0).name);
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
            if (tryLowerBinaryReturn(index, dest, src)) {
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

std::vector<std::unique_ptr<ASTNode>> lowerBlockToStatements(const std::vector<IRInstruction>& instructions)
{
    StatementLoweringContext context(instructions);
    return context.lower();
}
} // namespace Decompiler
