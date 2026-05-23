#pragma once

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ASTExpression.h"
#include "IRTypes.h"

namespace Decompiler
{
struct Graph;

struct ASTNode {
    virtual ~ASTNode()                                       = default;
    virtual std::vector<std::string> print(int indent) const = 0;
};

struct SimpleBlockNode : ASTNode {
    size_t block_index = 0;
    std::vector<std::unique_ptr<ASTNode>> statements;

    std::vector<std::string> print(int indent) const override;
};

std::vector<std::unique_ptr<ASTNode>> lowerBlockToStatements(const std::vector<IRInstruction>& instructions, CallingConvention callingConvention);

struct AssignmentNode : ASTNode {
    std::unique_ptr<Expression> target;
    std::unique_ptr<Expression> value;

    AssignmentNode(std::unique_ptr<Expression> targetExpression, std::unique_ptr<Expression> valueExpression)
        : target(std::move(targetExpression)), value(std::move(valueExpression))
    {
    }

    std::vector<std::string> print(int indent) const override;
};

struct ReturnNode : ASTNode {
    std::unique_ptr<Expression> value;

    ReturnNode(std::unique_ptr<Expression> valueExpression = nullptr) : value(std::move(valueExpression))
    {
    }

    std::vector<std::string> print(int indent) const override;
};

struct ExpressionStatementNode : ASTNode {
    std::unique_ptr<Expression> expression;

    ExpressionStatementNode(std::unique_ptr<Expression> value) : expression(std::move(value))
    {
    }

    std::vector<std::string> print(int indent) const override;
};

struct IfNode : ASTNode {
    std::unique_ptr<Expression> condition;

    std::vector<std::unique_ptr<ASTNode>> true_branch;
    std::vector<std::unique_ptr<ASTNode>> false_branch;

    std::vector<std::string> print(int indent) const override;
};

struct WhileNode : ASTNode {
    std::unique_ptr<Expression> condition;

    std::vector<std::unique_ptr<ASTNode>> body;

    std::vector<std::string> print(int indent) const override;
};

struct DoWhileNode : ASTNode {
    std::unique_ptr<Expression> condition;

    std::vector<std::unique_ptr<ASTNode>> body;

    std::vector<std::string> print(int indent) const override;
};

struct ForNode : ASTNode {
    std::unique_ptr<ASTNode> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<ASTNode> increment;
    std::vector<std::unique_ptr<ASTNode>> body;

    std::vector<std::string> print(int indent) const override;
};

struct BreakNode : ASTNode {
    std::vector<std::string> print(int indent) const override;
};

struct ContinueNode : ASTNode {
    std::vector<std::string> print(int indent) const override;
};

void restructureForLoops(std::vector<std::unique_ptr<ASTNode>>& nodes);

size_t find_merge_block(const Graph& cfg, size_t branch_true, size_t branch_false);
bool reaches(const Graph& cfg, size_t start, size_t target, size_t exclude = static_cast<size_t>(-1));

std::vector<std::unique_ptr<ASTNode>> build_ast(
      const Graph& cfg,
      const std::vector<std::set<size_t>>& doms,
      const std::vector<size_t>& postdoms,
      size_t current_block,
      size_t stop_block,
      size_t continue_target               = static_cast<size_t>(-1),
      size_t break_target                  = static_cast<size_t>(-1),
      CallingConvention calling_convention = CallingConvention::Unknown);
} // namespace Decompiler
