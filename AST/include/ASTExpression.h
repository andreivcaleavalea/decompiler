#pragma once

#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Decompiler
{
enum class BinaryOp { Add, Sub, Mul, Div, BitAnd, BitOr, BitXor, Shl, Shr };

enum class ComparisonOp { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, UnsignedLess, UnsignedLessEqual, UnsignedGreater, UnsignedGreaterEqual };

enum class UnaryOp { Negate, BitwiseNot, LogicalNot, PostIncrement, PostDecrement, PreIncrement, PreDecrement };

struct Expression {
    virtual ~Expression()                                              = default;
    virtual std::string render() const                                 = 0;
    virtual std::unique_ptr<Expression> clone() const                  = 0;
    virtual bool referencesVariable(const std::string& variable) const = 0;
};

struct VariableExpression : Expression {
    std::string name;

    VariableExpression(std::string value) : name(std::move(value))
    {
    }

    std::string render() const override
    {
        return name;
    }

    std::unique_ptr<Expression> clone() const override
    {
        return std::make_unique<VariableExpression>(name);
    }

    bool referencesVariable(const std::string& variable) const override
    {
        return name == variable;
    }
};

struct ConstantExpression : Expression {
    std::string value;

    ConstantExpression(std::string text) : value(std::move(text))
    {
    }

    std::string render() const override
    {
        return value;
    }

    std::unique_ptr<Expression> clone() const override
    {
        return std::make_unique<ConstantExpression>(value);
    }

    bool referencesVariable(const std::string&) const override
    {
        return false;
    }
};

struct BinaryExpression : Expression {
    BinaryOp op;
    std::unique_ptr<Expression> lhs;
    std::unique_ptr<Expression> rhs;

    BinaryExpression(BinaryOp operation, std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : op(operation), lhs(std::move(left)), rhs(std::move(right))
    {
    }

    std::string render() const override;
    std::unique_ptr<Expression> clone() const override;
    bool referencesVariable(const std::string& variable) const override;
};

struct ComparisonExpression : Expression {
    ComparisonOp op;
    std::unique_ptr<Expression> lhs;
    std::unique_ptr<Expression> rhs;

    ComparisonExpression(ComparisonOp operation, std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : op(operation), lhs(std::move(left)), rhs(std::move(right))
    {
    }

    std::string render() const override;
    std::unique_ptr<Expression> clone() const override;
    bool referencesVariable(const std::string& variable) const override;
};

struct UnaryExpression : Expression {
    UnaryOp op;
    std::unique_ptr<Expression> operand;

    UnaryExpression(UnaryOp operation, std::unique_ptr<Expression> value) : op(operation), operand(std::move(value))
    {
    }

    std::string render() const override;
    std::unique_ptr<Expression> clone() const override;
    bool referencesVariable(const std::string& variable) const override;
};

struct CallExpression : Expression {
    std::string callee;
    std::vector<std::unique_ptr<Expression>> arguments;

    CallExpression(std::string name, std::vector<std::unique_ptr<Expression>> args) : callee(std::move(name)), arguments(std::move(args))
    {
    }

    std::string render() const override;
    std::unique_ptr<Expression> clone() const override;
    bool referencesVariable(const std::string& variable) const override;
};

std::string binaryOpText(BinaryOp op);
std::string comparisonOpText(ComparisonOp op);
std::string unaryPrefixText(UnaryOp op);
std::string unarySuffixText(UnaryOp op);
int binaryOpPrecedence(BinaryOp op);
int comparisonOpPrecedence(ComparisonOp op);
std::unique_ptr<Expression> makeExpressionFromText(const std::string& text);
std::unique_ptr<Expression> cloneExpression(const Expression* expression);
bool sameVariableExpression(const Expression* lhs, const Expression* rhs);
std::unique_ptr<Expression> invertConditionExpression(const Expression& expression);
ComparisonOp invertedComparisonOp(ComparisonOp op);
} // namespace Decompiler
