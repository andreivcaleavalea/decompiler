#include "AST.h"

#include <cctype>
#include <string>
#include <vector>

namespace Decompiler
{
namespace
{
    std::string lastTextLine(const ASTNode& node)
    {
        if (const auto* text = dynamic_cast<const TextNode*>(&node)) {
            return text->text;
        }
        if (const auto* block = dynamic_cast<const SimpleBlockNode*>(&node)) {
            if (!block->statements.empty()) {
                return lastTextLine(*block->statements.back());
            }
        }
        return {};
    }

    const ASTNode* lastMeaningfulNode(const std::vector<std::unique_ptr<ASTNode>>& nodes)
    {
        for (size_t k = nodes.size(); k-- > 0;) {
            if (!dynamic_cast<const ContinueNode*>(nodes[k].get())) {
                return nodes[k].get();
            }
        }
        return nullptr;
    }

    void removeLastText(std::vector<std::unique_ptr<ASTNode>>& nodes)
    {
        if (nodes.empty()) {
            return;
        }
        if (dynamic_cast<TextNode*>(nodes.back().get())) {
            nodes.pop_back();
            return;
        }
        if (auto* block = dynamic_cast<SimpleBlockNode*>(nodes.back().get())) {
            if (!block->statements.empty()) {
                block->statements.pop_back();
            }
            if (block->statements.empty()) {
                nodes.pop_back();
            }
        }
    }

    void stripTrailingContinues(std::vector<std::unique_ptr<ASTNode>>& nodes)
    {
        while (!nodes.empty() && dynamic_cast<const ContinueNode*>(nodes.back().get())) {
            nodes.pop_back();
        }
    }

    std::string assignmentTarget(const std::string& line)
    {
        const auto eq = line.find(" = ");
        if (eq == std::string::npos) {
            return {};
        }
        const std::string lhs = line.substr(0, eq);
        if (lhs.empty()) {
            return {};
        }
        for (const unsigned char c : lhs) {
            if (!std::isalnum(c) && c != '_') {
                return {};
            }
        }
        return lhs;
    }

    std::string modifiedVariable(const std::string& line)
    {
        if (line.ends_with("++")) {
            return line.substr(0, line.size() - 2);
        }
        if (line.ends_with("--")) {
            return line.substr(0, line.size() - 2);
        }
        return assignmentTarget(line);
    }

    bool mentionsVariable(const std::string& expr, const std::string& var)
    {
        size_t pos = 0;
        while ((pos = expr.find(var, pos)) != std::string::npos) {
            const bool leftOk = pos == 0 || (!std::isalnum(static_cast<unsigned char>(expr[pos - 1])) && expr[pos - 1] != '_');
            const bool rightOk =
                  pos + var.size() >= expr.size() || (!std::isalnum(static_cast<unsigned char>(expr[pos + var.size()])) && expr[pos + var.size()] != '_');
            if (leftOk && rightOk) {
                return true;
            }
            pos += var.size();
        }
        return false;
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

        const std::string initLine = lastTextLine(*nodes[i - 1]);
        const std::string initVar  = assignmentTarget(initLine);
        if (initVar.empty() || !mentionsVariable(whileNode->condition_expr, initVar)) {
            ++i;
            continue;
        }

        const ASTNode* lastMeaningful = lastMeaningfulNode(whileNode->body);
        if (!lastMeaningful) {
            ++i;
            continue;
        }
        const std::string incLine = lastTextLine(*lastMeaningful);
        if (modifiedVariable(incLine) != initVar) {
            ++i;
            continue;
        }

        auto forNode            = std::make_unique<ForNode>();
        forNode->init_expr      = initLine;
        forNode->condition_expr = whileNode->condition_expr;
        forNode->increment_expr = incLine;
        forNode->body           = std::move(whileNode->body);
        stripTrailingContinues(forNode->body);
        removeLastText(forNode->body);

        if (dynamic_cast<TextNode*>(nodes[i - 1].get())) {
            nodes.erase(nodes.begin() + static_cast<ptrdiff_t>(i - 1));
            --i;
        } else if (auto* block = dynamic_cast<SimpleBlockNode*>(nodes[i - 1].get())) {
            block->statements.pop_back();
            if (block->statements.empty()) {
                nodes.erase(nodes.begin() + static_cast<ptrdiff_t>(i - 1));
                --i;
            }
        }

        nodes[i] = std::move(forNode);
        ++i;
    }
}
} // namespace Decompiler
