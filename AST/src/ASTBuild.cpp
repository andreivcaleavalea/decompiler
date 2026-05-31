#include "AST.h"

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

#include "ASTDetail.h"
#include "ControlGraphFlow.h"
#include "WindowsX64.h"

namespace Decompiler
{
namespace
{
    std::string conditionOperandText(const std::vector<IRInstruction>& instrs, const size_t beforeIndex, const IROperand& operand)
    {
        if (operand.isHeapDeref()) {
            return operand.name;
        }
        if (operand.tag == OperandTag::StackVar && !operand.arrayIndex.empty()) {
            const std::string indexText = ASTDetail::isTemporaryName(operand.arrayIndex)
                                                ? ASTDetail::substituteTempFromDefinitions(instrs, beforeIndex, operand.arrayIndex)
                                                : operand.arrayIndex;
            return operand.name + "[" + indexText + "]";
        }
        if (ASTDetail::isTemporaryName(operand.name)) {
            return ASTDetail::substituteTempFromDefinitions(instrs, beforeIndex, operand.name);
        }
        return operand.name;
    }

    bool isSyntheticSsaCopy(const IRInstruction& instruction)
    {
        return instruction.address == 0 && IRProperties::isAssign(instruction);
    }

    bool branchReentersHeader(const Graph& cfg, const size_t branchStart, const size_t header, const size_t mergeBlock)
    {
        if (!isValidBlockId(cfg, branchStart) || !isValidBlockId(cfg, header)) {
            return false;
        }
        return reaches(cfg, branchStart, header, mergeBlock);
    }

    bool branchReachesTarget(const Graph& cfg, const size_t branchStart, const size_t target, const size_t exclude = static_cast<size_t>(-1))
    {
        if (!isValidBlockId(cfg, branchStart) || !isValidBlockId(cfg, target)) {
            return false;
        }
        return branchStart == target || reaches(cfg, branchStart, target, exclude);
    }

    std::string simplifyArithmeticText(std::string expression)
    {
        size_t pos = 0;
        while ((pos = expression.find(" + -", pos)) != std::string::npos) {
            expression.replace(pos, 4, " - ");
            pos += 3;
        }
        return expression;
    }

    enum class LoopBoundaryKind { None, Break, Continue };

    struct LoopBoundaryBranch {
        LoopBoundaryKind kind = LoopBoundaryKind::None;
        size_t normal_block   = static_cast<size_t>(-1);
        bool invert_condition = false;
    };

    ComparisonOp comparisonOpFromCondition(const ConditionCode condition, const bool isFloat)
    {
        switch (condition) {
        case ConditionCode::E:
            return ComparisonOp::Equal;
        case ConditionCode::NE:
            return ComparisonOp::NotEqual;
        case ConditionCode::L:
            return ComparisonOp::Less;
        case ConditionCode::LE:
            return ComparisonOp::LessEqual;
        case ConditionCode::G:
            return ComparisonOp::Greater;
        case ConditionCode::GE:
            return ComparisonOp::GreaterEqual;
        case ConditionCode::B:
            return isFloat ? ComparisonOp::Less : ComparisonOp::UnsignedLess;
        case ConditionCode::BE:
            return isFloat ? ComparisonOp::LessEqual : ComparisonOp::UnsignedLessEqual;
        case ConditionCode::A:
            return isFloat ? ComparisonOp::Greater : ComparisonOp::UnsignedGreater;
        case ConditionCode::AE:
            return isFloat ? ComparisonOp::GreaterEqual : ComparisonOp::UnsignedGreaterEqual;
        case ConditionCode::None:
            return ComparisonOp::Equal;
        }
        return ComparisonOp::Equal;
    }

    std::unique_ptr<Expression> makeExtractedCondition(std::string lhs, const ConditionCode condition, std::string rhs, const bool isFloat = false)
    {
        lhs = simplifyArithmeticText(std::move(lhs));
        rhs = simplifyArithmeticText(std::move(rhs));

        auto lhsExpression = makeExpressionFromText(lhs);
        auto rhsExpression = makeExpressionFromText(rhs);
        if (!lhsExpression || !rhsExpression) {
            return nullptr;
        }

        return std::make_unique<ComparisonExpression>(comparisonOpFromCondition(condition, isFloat), std::move(lhsExpression), std::move(rhsExpression));
    }

    void assignCondition(IfNode& node, const Expression& condition)
    {
        node.condition = cloneExpression(&condition);
    }

    void assignCondition(WhileNode& node, std::unique_ptr<Expression> condition)
    {
        node.condition = std::move(condition);
    }

    void assignCondition(DoWhileNode& node, std::unique_ptr<Expression> condition)
    {
        node.condition = std::move(condition);
    }

    void invertCondition(IfNode& node)
    {
        if (node.condition) {
            node.condition = invertConditionExpression(*node.condition);
        }
    }

    LoopBoundaryKind boundaryKindForSuccessor(const Graph& cfg, const size_t successor, const size_t continueTarget, const size_t breakTarget)
    {
        if (successor == breakTarget && isValidBlockId(cfg, breakTarget)) {
            return LoopBoundaryKind::Break;
        }
        if (successor == continueTarget && isValidBlockId(cfg, continueTarget)) {
            return LoopBoundaryKind::Continue;
        }
        return LoopBoundaryKind::None;
    }

    std::optional<LoopBoundaryBranch> classifyLoopBoundaryBranch(
          const Graph& cfg, const size_t s0, const size_t s1, const size_t continueTarget, const size_t breakTarget)
    {
        const bool hasLoopContext = isValidBlockId(cfg, continueTarget) || isValidBlockId(cfg, breakTarget);
        if (!hasLoopContext) {
            return std::nullopt;
        }

        const LoopBoundaryKind s0Kind = boundaryKindForSuccessor(cfg, s0, continueTarget, breakTarget);
        const LoopBoundaryKind s1Kind = boundaryKindForSuccessor(cfg, s1, continueTarget, breakTarget);
        const bool s0IsBoundary       = s0Kind != LoopBoundaryKind::None;
        const bool s1IsBoundary       = s1Kind != LoopBoundaryKind::None;
        if (s0IsBoundary == s1IsBoundary) {
            return std::nullopt;
        }

        if (s0IsBoundary) {
            return LoopBoundaryBranch{ s0Kind, s1, false };
        }
        return LoopBoundaryBranch{ s1Kind, s0, true };
    }

    bool isPureConditionExpressionDef(const IRInstruction& instruction)
    {
        switch (instruction.type) {
        case IRType::ASSIGN:
        case IRType::LOAD_CONST:
        case IRType::LOAD:
        case IRType::SEXT:
        case IRType::LEA:
        case IRType::ADD:
        case IRType::SUB:
        case IRType::AND:
        case IRType::OR:
        case IRType::XOR:
        case IRType::SHL:
        case IRType::SHR:
        case IRType::SAR:
        case IRType::MUL:
        case IRType::DIV:
        case IRType::SMUL:
        case IRType::SDIV:
        case IRType::NEG:
        case IRType::NOT:
            return true;
        default:
            return false;
        }
    }

    void collectConditionTempDefinition(
          const std::vector<IRInstruction>& instrs, const size_t beforeIndex, const IROperand& operand, std::vector<size_t>& eraseIndices)
    {
        if (!ASTDetail::isTemporaryName(operand.name)) {
            return;
        }

        for (size_t i = beforeIndex; i-- > 0;) {
            if (IRProperties::operandAt(instrs[i], 0).name != operand.name) {
                continue;
            }
            if (!isPureConditionExpressionDef(instrs[i])) {
                return;
            }
            eraseIndices.push_back(i);

            collectConditionTempDefinition(instrs, i, IRProperties::operandAt(instrs[i], 1), eraseIndices);
            collectConditionTempDefinition(instrs, i, IRProperties::operandAt(instrs[i], 2), eraseIndices);

            const auto& src = IRProperties::operandAt(instrs[i], 1);
            if (ASTDetail::isTemporaryName(src.name) && ASTDetail::startsWithAny(src.name, { "rax_", "eax_", "xmm0_" })) {
                for (size_t k = i; k-- > 0;) {
                    if (IRProperties::isNop(instrs[k])) {
                        continue;
                    }
                    if (instrs[k].type == IRType::CALL) {
                        eraseIndices.push_back(k);
                        for (size_t m = k; m-- > 0;) {
                            if (IRProperties::isNop(instrs[m])) {
                                continue;
                            }
                            if (IRProperties::isControlFlow(instrs[m].type)) {
                                break;
                            }
                            if (!IRProperties::isSimpleValueProducer(instrs[m].type)) {
                                break;
                            }
                            if (!IRProperties::hasOperandAt(instrs[m], 0) || !instrs[m].operands[0].isAnyReg()) {
                                break;
                            }
                            if (argumentIndexForRegisterName(instrs[m].operands[0].name).has_value()) {
                                eraseIndices.push_back(m);
                            } else {
                                break;
                            }
                        }
                    }
                    break;
                }
            }
            return;
        }
    }

    void eraseConditionInstructions(std::vector<IRInstruction>& instrs, std::vector<size_t> eraseIndices)
    {
        eraseIndices.push_back(instrs.size() - 1);
        std::sort(eraseIndices.begin(), eraseIndices.end());
        eraseIndices.erase(std::unique(eraseIndices.begin(), eraseIndices.end()), eraseIndices.end());
        for (auto it = eraseIndices.rbegin(); it != eraseIndices.rend(); ++it) {
            if (*it < instrs.size()) {
                instrs.erase(instrs.begin() + *it);
            }
        }
    }

    void eraseOrphanedConditionCalls(std::vector<IRInstruction>& instrs)
    {
        size_t i = 0;
        while (i < instrs.size()) {
            if (instrs[i].type != IRType::CALL) {
                ++i;
                continue;
            }

            size_t start = i;
            while (start > 0) {
                const auto& prev = instrs[start - 1];
                if (IRProperties::isNop(prev)) {
                    --start;
                    continue;
                }
                if (!IRProperties::isSimpleValueProducer(prev.type)) {
                    break;
                }
                if (prev.operands.empty() || !prev.operands[0].isAnyReg()) {
                    break;
                }
                if (argumentIndexForRegisterName(prev.operands[0].name).has_value()) {
                    --start;
                } else {
                    break;
                }
            }

            size_t end = i + 1;
            while (end < instrs.size()) {
                const auto& next = instrs[end];
                if (IRProperties::isNop(next)) {
                    ++end;
                    continue;
                }
                if (!IRProperties::isSimpleValueProducer(next.type)) {
                    break;
                }
                if (next.operands.empty() || !next.operands[0].isAnyReg()) {
                    break;
                }
                const std::string base = normalizedRegisterBase(next.operands[0].name);
                if (base == "rax" || base == "eax" || base == "xmm0") {
                    ++end;
                } else {
                    break;
                }
            }

            instrs.erase(instrs.begin() + start, instrs.begin() + end);
            i = start;
        }
    }

    std::unique_ptr<Expression> try_extract_condition_and_trim(std::vector<IRInstruction>& instrs, const bool invertOperator)
    {
        if (instrs.empty()) {
            return nullptr;
        }

        const auto& jumpInst = instrs.back();
        if (!IRProperties::isConditionalJump(jumpInst.type)) {
            return nullptr;
        }

        ConditionCode condition = jumpInst.condition;
        if (condition == ConditionCode::None) {
            return nullptr;
        }
        if (invertOperator) {
            condition = invertCondition(condition);
        }
        const std::string op_str = conditionToOperator(condition);

        size_t condIndex = instrs.size() - 1;
        while (condIndex > 0) {
            --condIndex;
            const auto& candidate = instrs[condIndex];
            if (IRProperties::isNop(candidate) || isSyntheticSsaCopy(candidate)) {
                continue;
            }

            if (candidate.type == IRType::CMP) {
                const std::string lhs = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 1));
                const std::string rhs = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 2));
                const bool isFloat    = ASTDetail::startsWithAny(IRProperties::operandAt(candidate, 1).name, { "xmm" }) ||
                                     ASTDetail::startsWithAny(IRProperties::operandAt(candidate, 2).name, { "xmm" });
                std::vector<size_t> eraseIndices = { condIndex };
                collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 1), eraseIndices);
                collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 2), eraseIndices);
                eraseConditionInstructions(instrs, std::move(eraseIndices));
                eraseOrphanedConditionCalls(instrs);
                return makeExtractedCondition(lhs, condition, rhs, isFloat);
            }

            if (candidate.type == IRType::TEST && (op_str == "==" || op_str == "!=")) {
                const std::string lhs            = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 1));
                const std::string rhs            = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 2));
                std::vector<size_t> eraseIndices = { condIndex };
                collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 1), eraseIndices);
                collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 2), eraseIndices);
                eraseConditionInstructions(instrs, std::move(eraseIndices));
                eraseOrphanedConditionCalls(instrs);
                if (lhs == rhs) {
                    return makeExtractedCondition(lhs, condition, "0");
                }
                return makeExtractedCondition("(" + lhs + " & " + rhs + ")", condition, "0");
            }

            if ((candidate.type == IRType::SUB || candidate.type == IRType::ADD) && ASTDetail::isTemporaryName(IRProperties::operandAt(candidate, 0).name)) {
                const std::string lhs            = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 1));
                const std::string rhs            = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 2));
                std::vector<size_t> eraseIndices = { condIndex };
                collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 1), eraseIndices);
                collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 2), eraseIndices);
                eraseConditionInstructions(instrs, std::move(eraseIndices));
                eraseOrphanedConditionCalls(instrs);

                if (candidate.type == IRType::SUB) {
                    return makeExtractedCondition(lhs, condition, rhs);
                }
                return makeExtractedCondition("(" + lhs + " + " + rhs + ")", condition, "0");
            }

            break;
        }

        return nullptr;
    }
} // namespace

class LoopNodeBuilder
{
  public:
    LoopNodeBuilder(
          const Graph& cfg,
          const std::vector<std::set<size_t>>& doms,
          const std::vector<size_t>& postdoms,
          const size_t stop_block,
          std::vector<std::unique_ptr<ASTNode>>& ast)
        : cfg_(cfg), doms_(doms), postdoms_(postdoms), stop_block_(stop_block), ast_(ast)
    {
    }

    bool try_create_while(size_t& curr, const bool is_loop_header, std::vector<IRInstruction> instrs)
    {
        const auto& block = cfg_.blocks[curr];
        if (!is_loop_header || block.successors.size() != 2 || instrs.empty()) {
            return false;
        }
        const size_t s0 = block.successors[0];
        const size_t s1 = block.successors[1];
        if (!isValidBlockId(cfg_, s0) || !isValidBlockId(cfg_, s1)) {
            return false;
        }

        const auto loopTargets = classifyHeaderSuccessors(curr, s0, s1);
        if (!loopTargets.has_value()) {
            return false;
        }

        const size_t body_start  = loopTargets->loop_body_start;
        const size_t loop_exit   = loopTargets->loop_exit;
        const bool has_self_loop = (body_start == curr);
        const bool invert_cond   = (body_start != s0);

        auto condition = try_extract_condition_and_trim(instrs, invert_cond);
        if (!condition) {
            return false;
        }

        auto while_node = std::make_unique<WhileNode>();
        assignCondition(*while_node, std::move(condition));

        if (has_self_loop) {
            if (!instrs.empty()) {
                return false;
            }
            ast_.push_back(std::move(while_node));
            curr = loop_exit;
            return true;
        }

        while_node->body = build_ast(cfg_, doms_, postdoms_, body_start, curr, curr, loop_exit);
        if (!instrs.empty()) {
            auto seq         = std::make_unique<SimpleBlockNode>();
            seq->block_index = curr;
            seq->statements  = lowerBlockToStatements(instrs);
            while_node->body.insert(while_node->body.begin(), std::move(seq));
        }

        ast_.push_back(std::move(while_node));
        curr = loop_exit;
        return true;
    }

    bool try_create_do_while(size_t& curr, const bool is_loop_header, std::vector<IRInstruction> instrs)
    {
        const auto& block = cfg_.blocks[curr];
        if (block.successors.size() != 2 || instrs.empty()) {
            return false;
        }
        const size_t s0 = block.successors[0];
        const size_t s1 = block.successors[1];
        if (!isValidBlockId(cfg_, s0) || !isValidBlockId(cfg_, s1)) {
            return false;
        }

        if (is_loop_header) {
            const bool has_self_loop = (s0 == curr || s1 == curr);
            if (!has_self_loop) {
                return false;
            }

            const size_t loop_exit     = (s0 == curr) ? s1 : s0;
            const bool invertCondition = (s1 == curr);
            auto condition             = try_extract_condition_and_trim(instrs, invertCondition);
            if (!condition) {
                return false;
            }

            if (instrs.empty()) {
                return false;
            }

            auto do_while_node = std::make_unique<DoWhileNode>();
            assignCondition(*do_while_node, std::move(condition));

            auto seq         = std::make_unique<SimpleBlockNode>();
            seq->block_index = curr;
            seq->statements  = lowerBlockToStatements(instrs);
            do_while_node->body.push_back(std::move(seq));

            ast_.push_back(std::move(do_while_node));
            curr = loop_exit;
            return true;
        }

        const bool s0_is_back_target = doms_[curr].contains(s0);
        const bool s1_is_back_target = doms_[curr].contains(s1);
        if (s0_is_back_target == s1_is_back_target) {
            return false;
        }

        const size_t loop_header   = s0_is_back_target ? s0 : s1;
        const size_t loop_exit     = (loop_header == s0) ? s1 : s0;
        const bool invertCondition = !s0_is_back_target;
        auto condition             = try_extract_condition_and_trim(instrs, invertCondition);
        if (!condition) {
            return false;
        }
        auto do_while_node = std::make_unique<DoWhileNode>();
        assignCondition(*do_while_node, std::move(condition));
        do_while_node->body = build_ast(cfg_, doms_, postdoms_, loop_header, curr, loop_header, loop_exit);

        if (!instrs.empty()) {
            auto seq         = std::make_unique<SimpleBlockNode>();
            seq->block_index = curr;
            seq->statements  = lowerBlockToStatements(instrs);
            do_while_node->body.push_back(std::move(seq));
        }

        ast_.push_back(std::move(do_while_node));
        curr = loop_exit;
        return true;
    }

  private:
    struct HeaderLoopTargets {
        size_t loop_body_start = 0;
        size_t loop_exit       = 0;
    };

    bool successorStaysInLoop(const size_t header, const size_t successor) const
    {
        if (successor == header) {
            return true;
        }
        if (successor >= doms_.size() || successor >= cfg_.blocks.size()) {
            return false;
        }
        if (!doms_[successor].contains(header)) {
            return false;
        }
        return reaches(cfg_, successor, header, stop_block_);
    }

    std::optional<HeaderLoopTargets> classifyHeaderSuccessors(const size_t header, const size_t s0, const size_t s1) const
    {
        const bool s0_in_loop = successorStaysInLoop(header, s0);
        const bool s1_in_loop = successorStaysInLoop(header, s1);
        if (s0_in_loop == s1_in_loop) {
            return std::nullopt;
        }

        if (s0_in_loop) {
            return HeaderLoopTargets{ s0, s1 };
        }
        return HeaderLoopTargets{ s1, s0 };
    }

    const Graph& cfg_;
    const std::vector<std::set<size_t>>& doms_;
    const std::vector<size_t>& postdoms_;
    size_t stop_block_;
    std::vector<std::unique_ptr<ASTNode>>& ast_;
};

std::vector<std::unique_ptr<ASTNode>> build_ast(
      const Graph& cfg,
      const std::vector<std::set<size_t>>& doms,
      const std::vector<size_t>& postdoms,
      size_t current_block,
      size_t stop_block,
      size_t continue_target,
      size_t break_target)
{
    std::vector<std::unique_ptr<ASTNode>> ast;
    if (current_block == continue_target && isValidBlockId(cfg, continue_target)) {
        ast.push_back(std::make_unique<ContinueNode>());
        return ast;
    }
    if (current_block == break_target && isValidBlockId(cfg, break_target)) {
        ast.push_back(std::make_unique<BreakNode>());
        return ast;
    }

    size_t curr = current_block;
    std::set<size_t> visited;
    LoopNodeBuilder loopNodeBuilder(cfg, doms, postdoms, stop_block, ast);

    while (curr != stop_block && curr != static_cast<size_t>(-1) && curr < cfg.blocks.size() && !visited.contains(curr)) {
        if (curr == continue_target && isValidBlockId(cfg, continue_target)) {
            ast.push_back(std::make_unique<ContinueNode>());
            break;
        }
        if (curr == break_target && isValidBlockId(cfg, break_target)) {
            ast.push_back(std::make_unique<BreakNode>());
            break;
        }

        visited.insert(curr);
        const auto& block = cfg.blocks[curr];

        std::vector<IRInstruction> instrs = block.instructions;

        bool is_loop_header = false;
        for (const auto pred : block.predecessors) {
            if (pred < doms.size() && doms[pred].contains(curr)) {
                is_loop_header = true;
                break;
            }
        }

        if (loopNodeBuilder.try_create_while(curr, is_loop_header, instrs)) {
            continue;
        }

        if (loopNodeBuilder.try_create_do_while(curr, is_loop_header, instrs)) {
            continue;
        }

        if (block.successors.size() == 2 && !instrs.empty()) {
            const size_t s0 = block.successors[0];
            const size_t s1 = block.successors[1];
            if (!isValidBlockId(cfg, s0) || !isValidBlockId(cfg, s1)) {
                break;
            }

            auto condition = try_extract_condition_and_trim(instrs, false);
            if (!condition) {
                auto seq         = std::make_unique<SimpleBlockNode>();
                seq->block_index = curr;
                seq->statements  = lowerBlockToStatements(instrs);
                ast.push_back(std::move(seq));
                break;
            }

            const bool has_loop_context = isValidBlockId(cfg, continue_target) || isValidBlockId(cfg, break_target);
            if (const auto boundary = classifyLoopBoundaryBranch(cfg, s0, s1, continue_target, break_target)) {
                auto if_node = std::make_unique<IfNode>();
                assignCondition(*if_node, *condition);

                if (boundary->invert_condition) {
                    invertCondition(*if_node);
                }

                if (boundary->kind == LoopBoundaryKind::Break) {
                    if_node->true_branch.push_back(std::make_unique<BreakNode>());
                } else {
                    if_node->true_branch.push_back(std::make_unique<ContinueNode>());
                }

                if (!instrs.empty()) {
                    auto seq         = std::make_unique<SimpleBlockNode>();
                    seq->block_index = curr;
                    seq->statements  = lowerBlockToStatements(instrs);
                    ast.push_back(std::move(seq));
                }
                ast.push_back(std::move(if_node));
                curr = boundary->normal_block;
                continue;
            }

            auto if_node = std::make_unique<IfNode>();
            assignCondition(*if_node, *condition);

            size_t merge_block = curr < postdoms.size() ? postdoms[curr] : static_cast<size_t>(-1);
            if (merge_block == curr || (merge_block >= cfg.blocks.size() && merge_block != static_cast<size_t>(-1))) {
                merge_block = static_cast<size_t>(-1);
            }

            size_t true_block      = s0;
            size_t false_block     = s1;
            bool omit_false_branch = false;

            if (merge_block != static_cast<size_t>(-1)) {
                if (s0 == merge_block && s1 != merge_block) {
                    invertCondition(*if_node);
                    true_block        = s1;
                    false_block       = s0;
                    omit_false_branch = true;
                } else if (s1 == merge_block && s0 != merge_block) {
                    true_block        = s0;
                    false_block       = s1;
                    omit_false_branch = true;
                }
            }

            const bool true_reenters_header  = branchReentersHeader(cfg, true_block, curr, merge_block);
            const bool false_reenters_header = false_block != merge_block && branchReentersHeader(cfg, false_block, curr, merge_block);
            const bool true_reaches_loop_boundary =
                  branchReachesTarget(cfg, true_block, continue_target, merge_block) || branchReachesTarget(cfg, true_block, break_target, merge_block);
            const bool false_reaches_loop_boundary = false_block != merge_block && (branchReachesTarget(cfg, false_block, continue_target, merge_block) ||
                                                                                    branchReachesTarget(cfg, false_block, break_target, merge_block));

            if (has_loop_context && merge_block == static_cast<size_t>(-1) && (true_reaches_loop_boundary || false_reaches_loop_boundary)) {
                merge_block = continue_target;
            }

            if (!has_loop_context && (true_reenters_header || false_reenters_header)) {
                auto seq         = std::make_unique<SimpleBlockNode>();
                seq->block_index = curr;
                seq->statements  = lowerBlockToStatements(instrs);
                ast.push_back(std::move(seq));
                break;
            }

            if (!instrs.empty()) {
                auto seq         = std::make_unique<SimpleBlockNode>();
                seq->block_index = curr;
                seq->statements  = lowerBlockToStatements(instrs);
                ast.push_back(std::move(seq));
            }

            if_node->true_branch = build_ast(cfg, doms, postdoms, true_block, merge_block, continue_target, break_target);

            if (!omit_false_branch && false_block != merge_block) {
                if_node->false_branch = build_ast(cfg, doms, postdoms, false_block, merge_block, continue_target, break_target);
            }

            ast.push_back(std::move(if_node));

            if (has_loop_context && (merge_block == continue_target || merge_block == break_target)) {
                break;
            }
            curr = merge_block;
        } else {
            auto seq         = std::make_unique<SimpleBlockNode>();
            seq->block_index = curr;
            seq->statements  = lowerBlockToStatements(instrs);
            ast.push_back(std::move(seq));

            if (block.successors.empty())
                break;
            if (!isValidBlockId(cfg, block.successors[0]))
                break;
            if (block.successors[0] == continue_target && isValidBlockId(cfg, continue_target)) {
                ast.push_back(std::make_unique<ContinueNode>());
                break;
            }
            if (block.successors[0] == break_target && isValidBlockId(cfg, break_target)) {
                ast.push_back(std::make_unique<BreakNode>());
                break;
            }
            curr = block.successors[0];
        }
    }

    return ast;
}
} // namespace Decompiler
