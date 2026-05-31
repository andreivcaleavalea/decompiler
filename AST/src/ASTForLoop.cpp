#include "AST.h"

#include <vector>

namespace Decompiler
{
namespace
{
    const ASTNode* lastMeaningfulNode(const std::vector<std::unique_ptr<ASTNode>>& nodes)
    {
        for (size_t k = nodes.size(); k-- > 0;) {
            if (!dynamic_cast<const ContinueNode*>(nodes[k].get())) {
                return nodes[k].get();
            }
        }
        return nullptr;
    }

    void stripTrailingContinues(std::vector<std::unique_ptr<ASTNode>>& nodes)
    {
        while (!nodes.empty() && dynamic_cast<const ContinueNode*>(nodes.back().get())) {
            nodes.pop_back();
        }
    }

    const Expression* assignmentTargetExpression(const ASTNode* node)
    {
        if (const auto* assignment = dynamic_cast<const AssignmentNode*>(node)) {
            return assignment->target.get();
        }
        if (const auto* block = dynamic_cast<const SimpleBlockNode*>(node)) {
            if (!block->statements.empty()) {
                return assignmentTargetExpression(block->statements.back().get());
            }
        }
        return nullptr;
    }

    const Expression* modifiedExpression(const ASTNode* node)
    {
        if (const auto* assignment = dynamic_cast<const AssignmentNode*>(node)) {
            return assignment->target.get();
        }
        if (const auto* expressionStatement = dynamic_cast<const ExpressionStatementNode*>(node)) {
            const auto* unary = dynamic_cast<const UnaryExpression*>(expressionStatement->expression.get());
            if (unary != nullptr && (unary->op == UnaryOp::PostIncrement || unary->op == UnaryOp::PostDecrement || unary->op == UnaryOp::PreIncrement ||
                                     unary->op == UnaryOp::PreDecrement)) {
                return unary->operand.get();
            }
        }
        if (const auto* block = dynamic_cast<const SimpleBlockNode*>(node)) {
            if (!block->statements.empty()) {
                return modifiedExpression(block->statements.back().get());
            }
        }
        return nullptr;
    }

    bool conditionReferencesInitTarget(const Expression* condition, const Expression* initTarget)
    {
        const auto* variable = dynamic_cast<const VariableExpression*>(initTarget);
        return condition != nullptr && variable != nullptr && condition->referencesVariable(variable->name);
    }

    std::unique_ptr<ASTNode> takeLastStatement(std::vector<std::unique_ptr<ASTNode>>& nodes)
    {
        if (nodes.empty()) {
            return nullptr;
        }

        if (auto* block = dynamic_cast<SimpleBlockNode*>(nodes.back().get())) {
            if (block->statements.empty()) {
                return nullptr;
            }

            auto statement = std::move(block->statements.back());
            block->statements.pop_back();
            if (block->statements.empty()) {
                nodes.pop_back();
            }
            return statement;
        }

        auto statement = std::move(nodes.back());
        nodes.pop_back();
        return statement;
    }

    struct TakenInitializer {
        std::unique_ptr<ASTNode> statement;
        bool removed_node = false;
    };

    TakenInitializer takeInitializerStatement(std::vector<std::unique_ptr<ASTNode>>& nodes, const size_t index)
    {
        if (dynamic_cast<AssignmentNode*>(nodes[index].get())) {
            auto initializer = std::move(nodes[index]);
            nodes.erase(nodes.begin() + static_cast<ptrdiff_t>(index));
            return { std::move(initializer), true };
        }

        auto* block = dynamic_cast<SimpleBlockNode*>(nodes[index].get());
        if (block == nullptr || block->statements.empty()) {
            return {};
        }

        auto initializer = std::move(block->statements.back());
        block->statements.pop_back();
        if (block->statements.empty()) {
            nodes.erase(nodes.begin() + static_cast<ptrdiff_t>(index));
            return { std::move(initializer), true };
        }
        return { std::move(initializer), false };
    }
} // namespace

void restructureForLoops(std::vector<std::unique_ptr<ASTNode>>& nodes)
{
    for (auto& node : nodes) {
        if (auto* block = dynamic_cast<SimpleBlockNode*>(node.get())) {
            restructureForLoops(block->statements);
        } else if (auto* ifNode = dynamic_cast<IfNode*>(node.get())) {
            restructureForLoops(ifNode->true_branch);
            restructureForLoops(ifNode->false_branch);
        } else if (auto* whileNode = dynamic_cast<WhileNode*>(node.get())) {
            restructureForLoops(whileNode->body);
        } else if (auto* doWhileNode = dynamic_cast<DoWhileNode*>(node.get())) {
            restructureForLoops(doWhileNode->body);
        }
    }

    size_t i = 1;
    while (i < nodes.size()) {
        auto* whileNode = dynamic_cast<WhileNode*>(nodes[i].get());
        if (!whileNode || whileNode->body.empty()) {
            ++i;
            continue;
        }

        const Expression* initTarget = assignmentTargetExpression(nodes[i - 1].get());
        if (!conditionReferencesInitTarget(whileNode->condition.get(), initTarget)) {
            ++i;
            continue;
        }

        const ASTNode* lastMeaningful = lastMeaningfulNode(whileNode->body);
        if (lastMeaningful == nullptr) {
            ++i;
            continue;
        }

        const Expression* incrementTarget = modifiedExpression(lastMeaningful);
        if (!sameVariableExpression(initTarget, incrementTarget)) {
            ++i;
            continue;
        }

        auto initializer = takeInitializerStatement(nodes, i - 1);
        if (!initializer.statement) {
            ++i;
            continue;
        }

        if (initializer.removed_node) {
            --i;
        }

        auto forNode  = std::make_unique<ForNode>();
        forNode->init = std::move(initializer.statement);
        forNode->body = std::move(whileNode->body);
        stripTrailingContinues(forNode->body);
        forNode->increment = takeLastStatement(forNode->body);

        forNode->condition = std::move(whileNode->condition);

        nodes[i] = std::move(forNode);
        ++i;
    }
}
} // namespace Decompiler
