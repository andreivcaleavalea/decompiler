#include "AST.h"

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

#include "ASTDetail.h"
#include "ControlGraphFlow.h"

namespace Decompiler {
    namespace {
        std::string conditionOperandText(const std::vector<IRInstruction>& instrs, const size_t beforeIndex, const IROperand& operand) {
            const std::string value = ASTDetail::normalizeOperandForDisplay(operand.value);
            if (ASTDetail::isTemporaryName(value)) {
                return ASTDetail::substituteTempFromDefinitions(instrs, beforeIndex, operand.value);
            }
            return ASTDetail::operandForDisplay(operand);
        }

        bool isSyntheticSsaCopy(const IRInstruction& instruction) {
            return instruction.address == 0 && IRProperties::isAssign(instruction);
        }

        bool branchReentersHeader(const Graph& cfg, const size_t branchStart, const size_t header, const size_t mergeBlock) {
            if (!isValidBlockId(cfg, branchStart) || !isValidBlockId(cfg, header)) {
                return false;
            }
            return reaches(cfg, branchStart, header, mergeBlock);
        }

        bool branchReachesTarget(const Graph& cfg, const size_t branchStart, const size_t target, const size_t exclude = static_cast<size_t>(-1)) {
            if (!isValidBlockId(cfg, branchStart) || !isValidBlockId(cfg, target)) {
                return false;
            }
            return branchStart == target || reaches(cfg, branchStart, target, exclude);
        }

        std::string simplifyArithmeticText(std::string expression) {
            size_t pos = 0;
            while ((pos = expression.find(" + -", pos)) != std::string::npos) {
                expression.replace(pos, 4, " - ");
                pos += 3;
            }
            return expression;
        }

        std::string renderComparison(std::string lhs, const std::string& op, std::string rhs, const bool unsignedComparison) {
            lhs = simplifyArithmeticText(std::move(lhs));
            rhs = simplifyArithmeticText(std::move(rhs));
            if (!unsignedComparison || op == "==" || op == "!=") {
                return lhs + " " + op + " " + rhs;
            }
            return "(unsigned)(" + lhs + ") " + op + " (unsigned)(" + rhs + ")";
        }

        bool isPureConditionExpressionDef(const IRInstruction& instruction) {
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
                case IRType::NEG:
                case IRType::NOT:
                    return true;
                default:
                    return false;
            }
        }

        void collectConditionTempDefinition(const std::vector<IRInstruction>& instrs, const size_t beforeIndex, const IROperand& operand, std::vector<size_t>& eraseIndices) {
            const std::string value = ASTDetail::normalizeOperandForDisplay(operand.value);
            if (!ASTDetail::isTemporaryName(value)) {
                return;
            }

            for (size_t i = beforeIndex; i-- > 0;) {
                if (IRProperties::operandAt(instrs[i], 0).value != operand.value) {
                    continue;
                }
                if (isPureConditionExpressionDef(instrs[i])) {
                    eraseIndices.push_back(i);
                }
                return;
            }
        }

        void eraseConditionInstructions(std::vector<IRInstruction>& instrs, std::vector<size_t> eraseIndices) {
            eraseIndices.push_back(instrs.size() - 1);
            std::ranges::sort(eraseIndices);
            eraseIndices.erase(std::unique(eraseIndices.begin(), eraseIndices.end()), eraseIndices.end());
            for (auto it = eraseIndices.rbegin(); it != eraseIndices.rend(); ++it) {
                if (*it < instrs.size()) {
                    instrs.erase(instrs.begin() + *it);
                }
            }
        }

        std::optional<std::string> try_extract_condition_and_trim(std::vector<IRInstruction>& instrs, const bool invertOperator) {
            if (instrs.empty()) {
                return std::nullopt;
            }

            const auto& jumpInst = instrs.back();
            if (!IRProperties::isConditionalJump(jumpInst.type)) {
                return std::nullopt;
            }

            ConditionCode condition = jumpInst.condition;
            if (condition == ConditionCode::None) {
                return std::nullopt;
            }
            if (invertOperator) {
                condition = invertCondition(condition);
            }
            const std::string op_str = conditionToOperator(condition);
            const bool unsignedComparison = isUnsignedCondition(condition);

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
                    std::vector<size_t> eraseIndices = {condIndex};
                    collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 1), eraseIndices);
                    collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 2), eraseIndices);
                    eraseConditionInstructions(instrs, std::move(eraseIndices));
                    return renderComparison(lhs, op_str, rhs, unsignedComparison);
                }

                if (candidate.type == IRType::TEST && (op_str == "==" || op_str == "!=")) {
                    const std::string lhs = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 1));
                    const std::string rhs = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 2));
                    std::vector<size_t> eraseIndices = {condIndex};
                    collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 1), eraseIndices);
                    collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 2), eraseIndices);
                    eraseConditionInstructions(instrs, std::move(eraseIndices));
                    if (lhs == rhs) {
                        return lhs + " " + op_str + " 0";
                    }
                    return "(" + lhs + " & " + rhs + ") " + op_str + " 0";
                }

                if ((candidate.type == IRType::SUB || candidate.type == IRType::ADD) &&
                    ASTDetail::isTemporaryName(ASTDetail::normalizeOperandForDisplay(IRProperties::operandAt(candidate, 0).value))) {
                    const std::string lhs = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 1));
                    const std::string rhs = conditionOperandText(instrs, condIndex, IRProperties::operandAt(candidate, 2));
                    std::vector<size_t> eraseIndices = {condIndex};
                    collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 1), eraseIndices);
                    collectConditionTempDefinition(instrs, condIndex, IRProperties::operandAt(candidate, 2), eraseIndices);
                    eraseConditionInstructions(instrs, std::move(eraseIndices));

                    if (candidate.type == IRType::SUB) {
                        return renderComparison(lhs, op_str, rhs, unsignedComparison);
                    }
                    return renderComparison("(" + lhs + " + " + rhs + ")", op_str, "0", unsignedComparison);
                }

                break;
            }

            return std::nullopt;
        }
    }

    class LoopNodeBuilder {
    public:
        LoopNodeBuilder(const Graph& cfg, const std::vector<std::set<size_t>>& doms, const std::vector<size_t>& postdoms, const size_t stop_block, std::vector<std::unique_ptr<ASTNode>>& ast)
            : cfg_(cfg), doms_(doms), postdoms_(postdoms), stop_block_(stop_block), ast_(ast) {}

        bool try_create_while(size_t& curr, const bool is_loop_header, std::vector<IRInstruction> instrs) {
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

            const size_t body_start = loopTargets->loop_body_start;
            const size_t loop_exit = loopTargets->loop_exit;
            const bool has_self_loop = (body_start == curr);
            const bool invert_cond = (body_start != s0);

            const auto condition = try_extract_condition_and_trim(instrs, invert_cond);
            if (!condition.has_value()) {
                return false;
            }

            auto while_node = std::make_unique<WhileNode>();
            while_node->condition_expr = *condition;

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
                auto seq = std::make_unique<SimpleBlockNode>();
                seq->block_index = curr;
                seq->instructions = std::move(instrs);
                while_node->body.insert(while_node->body.begin(), std::move(seq));
            }

            ast_.push_back(std::move(while_node));
            curr = loop_exit;
            return true;
        }

        bool try_create_do_while(size_t& curr, const bool is_loop_header, std::vector<IRInstruction> instrs) {
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

                const size_t loop_exit = (s0 == curr) ? s1 : s0;
                const bool invertCondition = (s1 == curr);
                const auto condition = try_extract_condition_and_trim(instrs, invertCondition);
                if (!condition.has_value()) {
                    return false;
                }

                if (instrs.empty()) {
                    return false;
                }

                auto do_while_node = std::make_unique<DoWhileNode>();
                do_while_node->condition_expr = *condition;

                auto seq = std::make_unique<SimpleBlockNode>();
                seq->block_index = curr;
                seq->instructions = std::move(instrs);
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

            const size_t loop_header = s0_is_back_target ? s0 : s1;
            const size_t loop_exit = (loop_header == s0) ? s1 : s0;
            const bool invertCondition = !s0_is_back_target;
            const auto condition = try_extract_condition_and_trim(instrs, invertCondition);
            if (!condition.has_value()) {
                return false;
            }
            auto do_while_node = std::make_unique<DoWhileNode>();
            do_while_node->condition_expr = *condition;
            do_while_node->body = build_ast(cfg_, doms_, postdoms_, loop_header, curr, loop_header, loop_exit);

            if (!instrs.empty()) {
                auto seq = std::make_unique<SimpleBlockNode>();
                seq->block_index = curr;
                seq->instructions = std::move(instrs);
                do_while_node->body.push_back(std::move(seq));
            }

            ast_.push_back(std::move(do_while_node));
            curr = loop_exit;
            return true;
        }

    private:
        struct HeaderLoopTargets {
            size_t loop_body_start = 0;
            size_t loop_exit = 0;
        };

        bool successorStaysInLoop(const size_t header, const size_t successor) const {
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

        std::optional<HeaderLoopTargets> classifyHeaderSuccessors(const size_t header,
                                                                                 const size_t s0,
                                                                                 const size_t s1) const {
            const bool s0_in_loop = successorStaysInLoop(header, s0);
            const bool s1_in_loop = successorStaysInLoop(header, s1);
            if (s0_in_loop == s1_in_loop) {
                return std::nullopt;
            }

            if (s0_in_loop) {
                return HeaderLoopTargets{s0, s1};
            }
            return HeaderLoopTargets{s1, s0};
        }

        const Graph& cfg_;
        const std::vector<std::set<size_t>>& doms_;
        const std::vector<size_t>& postdoms_;
        size_t stop_block_;
        std::vector<std::unique_ptr<ASTNode>>& ast_;
    };

    std::vector<std::unique_ptr<ASTNode>> build_ast(const Graph& cfg,
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

            if (loopNodeBuilder.try_create_do_while(curr,is_loop_header, instrs)) {
                continue;
            }

            if (block.successors.size() == 2 && !instrs.empty()) {
                const size_t s0 = block.successors[0];
                const size_t s1 = block.successors[1];
                if (!isValidBlockId(cfg, s0) || !isValidBlockId(cfg, s1)) {
                    break;
                }

                auto condition = try_extract_condition_and_trim(instrs, false);
                if (!condition.has_value()) {
                    auto seq = std::make_unique<SimpleBlockNode>();
                    seq->block_index = curr;
                    seq->instructions = instrs;
                    ast.push_back(std::move(seq));
                    break;
                }

                auto if_node = std::make_unique<IfNode>();
                if_node->condition_expr = *condition;

                size_t merge_block = curr < postdoms.size() ? postdoms[curr] : static_cast<size_t>(-1);
                if (merge_block == curr || (merge_block >= cfg.blocks.size() && merge_block != static_cast<size_t>(-1))) {
                    merge_block = static_cast<size_t>(-1);
                }

                size_t true_block = s0;
                size_t false_block = s1;
                bool omit_false_branch = false;

                if (merge_block != static_cast<size_t>(-1)) {
                    if (s0 == merge_block && s1 != merge_block) {
                        if_node->condition_expr = ASTDetail::invertConditionExpr(if_node->condition_expr);
                        true_block = s1;
                        false_block = s0;
                        omit_false_branch = true;
                    } else if (s1 == merge_block && s0 != merge_block) {
                        true_block = s0;
                        false_block = s1;
                        omit_false_branch = true;
                    }
                }

                const bool has_loop_context =
                    isValidBlockId(cfg, continue_target) || isValidBlockId(cfg, break_target);
                const bool true_reenters_header = branchReentersHeader(cfg, true_block, curr, merge_block);
                const bool false_reenters_header =
                    false_block != merge_block && branchReentersHeader(cfg, false_block, curr, merge_block);
                const bool true_reaches_loop_boundary =
                    branchReachesTarget(cfg, true_block, continue_target, merge_block) ||
                    branchReachesTarget(cfg, true_block, break_target, merge_block);
                const bool false_reaches_loop_boundary =
                    false_block != merge_block &&
                    (branchReachesTarget(cfg, false_block, continue_target, merge_block) ||
                     branchReachesTarget(cfg, false_block, break_target, merge_block));

                if (has_loop_context && merge_block == static_cast<size_t>(-1) &&
                    (true_reaches_loop_boundary || false_reaches_loop_boundary)) {
                    merge_block = continue_target;
                }

                if (!has_loop_context && (true_reenters_header || false_reenters_header)) {
                    auto seq = std::make_unique<SimpleBlockNode>();
                    seq->block_index = curr;
                    seq->instructions = instrs;
                    ast.push_back(std::move(seq));
                    break;
                }

                if (!instrs.empty()) {
                    auto seq = std::make_unique<SimpleBlockNode>();
                    seq->block_index = curr;
                    seq->instructions = instrs;
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
                auto seq = std::make_unique<SimpleBlockNode>();
                seq->block_index = curr;
                seq->instructions = instrs;
                ast.push_back(std::move(seq));

                if (block.successors.empty()) break;
                if (!isValidBlockId(cfg, block.successors[0])) break;
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
}
