#include "ASTExpression.h"

#include <algorithm>

namespace Decompiler
{
namespace
{
    bool isRightAssociativeUnsafe(const BinaryOp op)
    {
        return op == BinaryOp::Sub || op == BinaryOp::Div || op == BinaryOp::Mod || op == BinaryOp::Shl || op == BinaryOp::Shr;
    }

    std::string renderBinaryChild(const BinaryOp parentOp, const Expression* child, const bool isRightChild)
    {
        if (child == nullptr) {
            return {};
        }

        const auto* childBinary = dynamic_cast<const BinaryExpression*>(child);
        if (childBinary == nullptr) {
            return child->render();
        }

        const int childPrecedence  = binaryOpPrecedence(childBinary->op);
        const int parentPrecedence = binaryOpPrecedence(parentOp);
        const bool needsParens =
              childPrecedence < parentPrecedence || (isRightChild && childPrecedence == parentPrecedence && isRightAssociativeUnsafe(parentOp));
        if (needsParens) {
            return "(" + child->render() + ")";
        }
        return child->render();
    }

    std::string renderComparisonChild(const ComparisonOp parentOp, const Expression* child)
    {
        (void) parentOp;
        if (child == nullptr) {
            return {};
        }

        if (const auto* childBinary = dynamic_cast<const BinaryExpression*>(child); childBinary != nullptr) {
            if (binaryOpPrecedence(childBinary->op) < comparisonOpPrecedence(parentOp)) {
                return "(" + child->render() + ")";
            }
        }
        if (dynamic_cast<const ComparisonExpression*>(child) != nullptr) {
            return "(" + child->render() + ")";
        }
        return child->render();
    }

    std::string parenthesizeIfNeeded(const Expression& expression)
    {
        if (dynamic_cast<const BinaryExpression*>(&expression) != nullptr) {
            return "(" + expression.render() + ")";
        }
        return expression.render();
    }

} // namespace

int binaryOpPrecedence(const BinaryOp op)
{
    switch (op) {
    case BinaryOp::BitOr:
        return 1;
    case BinaryOp::BitXor:
        return 2;
    case BinaryOp::BitAnd:
        return 3;
    case BinaryOp::Shl:
    case BinaryOp::Shr:
        return 4;
    case BinaryOp::Add:
    case BinaryOp::Sub:
        return 5;
    case BinaryOp::Mul:
    case BinaryOp::Div:
    case BinaryOp::Mod:
        return 6;
    }
    return 0;
}

int comparisonOpPrecedence(const ComparisonOp op)
{
    switch (op) {
    case ComparisonOp::Equal:
    case ComparisonOp::NotEqual:
        return 1;
    case ComparisonOp::Less:
    case ComparisonOp::LessEqual:
    case ComparisonOp::Greater:
    case ComparisonOp::GreaterEqual:
    case ComparisonOp::UnsignedLess:
    case ComparisonOp::UnsignedLessEqual:
    case ComparisonOp::UnsignedGreater:
    case ComparisonOp::UnsignedGreaterEqual:
        return 2;
    }
    return 0;
}

std::string binaryOpText(const BinaryOp op)
{
    switch (op) {
    case BinaryOp::Add:
        return "+";
    case BinaryOp::Sub:
        return "-";
    case BinaryOp::Mul:
        return "*";
    case BinaryOp::Div:
        return "/";
    case BinaryOp::Mod:
        return "%";
    case BinaryOp::BitAnd:
        return "&";
    case BinaryOp::BitOr:
        return "|";
    case BinaryOp::BitXor:
        return "^";
    case BinaryOp::Shl:
        return "<<";
    case BinaryOp::Shr:
        return ">>";
    }
    return {};
}

std::string comparisonOpText(const ComparisonOp op)
{
    switch (op) {
    case ComparisonOp::Equal:
        return "==";
    case ComparisonOp::NotEqual:
        return "!=";
    case ComparisonOp::Less:
    case ComparisonOp::UnsignedLess:
        return "<";
    case ComparisonOp::LessEqual:
    case ComparisonOp::UnsignedLessEqual:
        return "<=";
    case ComparisonOp::Greater:
    case ComparisonOp::UnsignedGreater:
        return ">";
    case ComparisonOp::GreaterEqual:
    case ComparisonOp::UnsignedGreaterEqual:
        return ">=";
    }
    return {};
}

std::string unaryPrefixText(const UnaryOp op)
{
    switch (op) {
    case UnaryOp::Negate:
        return "-";
    case UnaryOp::BitwiseNot:
        return "~";
    case UnaryOp::LogicalNot:
        return "!";
    case UnaryOp::PreIncrement:
        return "++";
    case UnaryOp::PreDecrement:
        return "--";
    case UnaryOp::Dereference:
        return "*";
    case UnaryOp::PostIncrement:
    case UnaryOp::PostDecrement:
        return {};
    }
    return {};
}

std::string unarySuffixText(const UnaryOp op)
{
    switch (op) {
    case UnaryOp::PostIncrement:
        return "++";
    case UnaryOp::PostDecrement:
        return "--";
    case UnaryOp::Negate:
    case UnaryOp::BitwiseNot:
    case UnaryOp::LogicalNot:
    case UnaryOp::PreIncrement:
    case UnaryOp::PreDecrement:
    case UnaryOp::Dereference:
        return {};
    }
    return {};
}

std::string BinaryExpression::render() const
{
    const std::string lhsText = renderBinaryChild(op, lhs.get(), false);
    const std::string rhsText = renderBinaryChild(op, rhs.get(), true);
    return lhsText + " " + binaryOpText(op) + " " + rhsText;
}

std::unique_ptr<Expression> BinaryExpression::clone() const
{
    return std::make_unique<BinaryExpression>(op, cloneExpression(lhs.get()), cloneExpression(rhs.get()));
}

bool BinaryExpression::referencesVariable(const std::string& variable) const
{
    return (lhs && lhs->referencesVariable(variable)) || (rhs && rhs->referencesVariable(variable));
}

std::string ComparisonExpression::render() const
{
    const std::string lhsText     = renderComparisonChild(op, lhs.get());
    const std::string rhsText     = renderComparisonChild(op, rhs.get());
    const bool unsignedComparison = op == ComparisonOp::UnsignedLess || op == ComparisonOp::UnsignedLessEqual || op == ComparisonOp::UnsignedGreater ||
                                    op == ComparisonOp::UnsignedGreaterEqual;
    if (unsignedComparison) {
        return "(unsigned)(" + lhsText + ") " + comparisonOpText(op) + " (unsigned)(" + rhsText + ")";
    }
    return lhsText + " " + comparisonOpText(op) + " " + rhsText;
}

std::unique_ptr<Expression> ComparisonExpression::clone() const
{
    return std::make_unique<ComparisonExpression>(op, cloneExpression(lhs.get()), cloneExpression(rhs.get()));
}

bool ComparisonExpression::referencesVariable(const std::string& variable) const
{
    return (lhs && lhs->referencesVariable(variable)) || (rhs && rhs->referencesVariable(variable));
}

std::string UnaryExpression::render() const
{
    if (!operand) {
        return {};
    }
    return unaryPrefixText(op) + parenthesizeIfNeeded(*operand) + unarySuffixText(op);
}

std::unique_ptr<Expression> UnaryExpression::clone() const
{
    return std::make_unique<UnaryExpression>(op, cloneExpression(operand.get()));
}

bool UnaryExpression::referencesVariable(const std::string& variable) const
{
    return operand && operand->referencesVariable(variable);
}

std::string CastExpression::render() const
{
    if (!operand) {
        return {};
    }
    return "(" + type + ")" + parenthesizeIfNeeded(*operand);
}

std::unique_ptr<Expression> CastExpression::clone() const
{
    return std::make_unique<CastExpression>(type, cloneExpression(operand.get()));
}

bool CastExpression::referencesVariable(const std::string& variable) const
{
    return operand && operand->referencesVariable(variable);
}

std::string CallExpression::render() const
{
    std::string result = callee + "(";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        if (arguments[i]) {
            result += arguments[i]->render();
        }
    }
    result += ")";
    return result;
}

std::unique_ptr<Expression> CallExpression::clone() const
{
    std::vector<std::unique_ptr<Expression>> clonedArguments;
    clonedArguments.reserve(arguments.size());
    for (const auto& argument : arguments) {
        clonedArguments.push_back(cloneExpression(argument.get()));
    }
    return std::make_unique<CallExpression>(callee, std::move(clonedArguments));
}

bool CallExpression::referencesVariable(const std::string& variable) const
{
    for (const auto& argument : arguments) {
        if (argument && argument->referencesVariable(variable)) {
            return true;
        }
    }
    return false;
}

std::string SubscriptExpression::render() const
{
    return (array ? array->render() : std::string{}) + "[" + (index ? index->render() : std::string{}) + "]";
}

std::unique_ptr<Expression> SubscriptExpression::clone() const
{
    return std::make_unique<SubscriptExpression>(cloneExpression(array.get()), cloneExpression(index.get()));
}

bool SubscriptExpression::referencesVariable(const std::string& variable) const
{
    return (array && array->referencesVariable(variable)) || (index && index->referencesVariable(variable));
}

std::unique_ptr<Expression> cloneExpression(const Expression* expression)
{
    if (expression == nullptr) {
        return nullptr;
    }
    return expression->clone();
}

bool sameVariableExpression(const Expression* lhs, const Expression* rhs)
{
    const auto* leftVariable  = dynamic_cast<const VariableExpression*>(lhs);
    const auto* rightVariable = dynamic_cast<const VariableExpression*>(rhs);
    return leftVariable != nullptr && rightVariable != nullptr && leftVariable->name == rightVariable->name;
}

ComparisonOp invertedComparisonOp(const ComparisonOp op)
{
    switch (op) {
    case ComparisonOp::Equal:
        return ComparisonOp::NotEqual;
    case ComparisonOp::NotEqual:
        return ComparisonOp::Equal;
    case ComparisonOp::Less:
        return ComparisonOp::GreaterEqual;
    case ComparisonOp::LessEqual:
        return ComparisonOp::Greater;
    case ComparisonOp::Greater:
        return ComparisonOp::LessEqual;
    case ComparisonOp::GreaterEqual:
        return ComparisonOp::Less;
    case ComparisonOp::UnsignedLess:
        return ComparisonOp::UnsignedGreaterEqual;
    case ComparisonOp::UnsignedLessEqual:
        return ComparisonOp::UnsignedGreater;
    case ComparisonOp::UnsignedGreater:
        return ComparisonOp::UnsignedLessEqual;
    case ComparisonOp::UnsignedGreaterEqual:
        return ComparisonOp::UnsignedLess;
    default:
        return ComparisonOp::Equal;
    }
}

std::unique_ptr<Expression> invertConditionExpression(const Expression& expression)
{
    if (const auto* comparison = dynamic_cast<const ComparisonExpression*>(&expression); comparison != nullptr) {
        return std::make_unique<ComparisonExpression>(
              invertedComparisonOp(comparison->op), cloneExpression(comparison->lhs.get()), cloneExpression(comparison->rhs.get()));
    }
    if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression); unary != nullptr && unary->op == UnaryOp::LogicalNot && unary->operand) {
        return cloneExpression(unary->operand.get());
    }
    return std::make_unique<UnaryExpression>(UnaryOp::LogicalNot, expression.clone());
}
} // namespace Decompiler
