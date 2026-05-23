#include "AST.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ASTDetail.h"

namespace Decompiler
{
namespace
{
    bool containsCondition(const std::vector<std::string>& assumptions, const std::string& condition)
    {
        return std::find(assumptions.begin(), assumptions.end(), condition) != assumptions.end();
    }

    std::string trimCopy(const std::string& value)
    {
        const auto first = value.find_first_not_of(' ');
        if (first == std::string::npos) {
            return {};
        }
        const auto last = value.find_last_not_of(' ');
        return value.substr(first, last - first + 1);
    }

    std::string simplifyExpressionText(std::string expression)
    {
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

    bool isContinueLine(const std::string& line)
    {
        return trimCopy(line) == "continue";
    }

    std::vector<std::string> printNodesWithAssumptions(
          const std::vector<std::unique_ptr<ASTNode>>& nodes, int indent, const std::vector<std::string>& assumptions);

    std::vector<std::string> printIfWithAssumptions(const IfNode& node, const int indent, const std::vector<std::string>& assumptions)
    {
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

        std::vector<std::string> trueLines  = printNodesWithAssumptions(node.true_branch, indent + 4, trueAssumptions);
        std::vector<std::string> falseLines = printNodesWithAssumptions(node.false_branch, indent + 4, falseAssumptions);

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
          const std::vector<std::unique_ptr<ASTNode>>& nodes, const int indent, const std::vector<std::string>& assumptions)
    {
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
} // namespace

std::vector<std::string> SimpleBlockNode::print(int indent) const
{
    std::vector<std::string> result;
    for (const auto& statement : statements) {
        auto lines = statement->print(indent);
        result.insert(result.end(), lines.begin(), lines.end());
    }
    return result;
}

std::vector<std::string> IfNode::print(int indent) const
{
    return printIfWithAssumptions(*this, indent, {});
}

std::vector<std::string> WhileNode::print(int indent) const
{
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

std::vector<std::string> DoWhileNode::print(int indent) const
{
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

std::vector<std::string> BreakNode::print(int indent) const
{
    return { std::string(indent, ' ') + "break" };
}

std::vector<std::string> ContinueNode::print(int indent) const
{
    return { std::string(indent, ' ') + "continue" };
}
} // namespace Decompiler
