#include "ASTExpression.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace Decompiler
{
namespace
{
    std::string trimCopy(const std::string& value)
    {
        const auto first = value.find_first_not_of(' ');
        if (first == std::string::npos) {
            return {};
        }
        const auto last = value.find_last_not_of(' ');
        return value.substr(first, last - first + 1);
    }

    size_t firstPrimaryLength(const std::string& text)
    {
        size_t i = 0;
        while (i < text.size() && text[i] == '*') {
            ++i;
        }
        if (i >= text.size()) {
            return 0;
        }
        if (text[i] == '(') {
            int depth = 0;
            for (; i < text.size(); ++i) {
                if (text[i] == '(') {
                    ++depth;
                } else if (text[i] == ')') {
                    --depth;
                    if (depth == 0) {
                        return i + 1;
                    }
                }
            }
            return text.size();
        }
        while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_')) {
            ++i;
        }
        return i;
    }

    bool isKnownTypeName(const std::string& name)
    {
        static constexpr std::array<std::string_view, 16> types = { "double",  "float",   "int",     "unsigned", "char",     "void",     "int8_t", "int16_t",
                                                                    "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "long",   "short" };
        for (const auto type : types) {
            if (name == type) {
                return true;
            }
        }
        return false;
    }

    bool isIdentifierText(const std::string& text)
    {
        if (text.empty()) {
            return false;
        }

        const unsigned char first = static_cast<unsigned char>(text.front());
        if (std::isdigit(first) != 0) {
            return false;
        }

        for (const unsigned char c : text) {
            if (std::isalnum(c) == 0 && c != '_') {
                return false;
            }
        }
        return true;
    }

    bool isCppQualifiedNameText(const std::string& text)
    {
        if (text.empty()) {
            return false;
        }
        const unsigned char first = static_cast<unsigned char>(text.front());
        if (std::isdigit(first) != 0) {
            return false;
        }
        if (first == '+' || first == '-' || first == '/' || first == '|') {
            return false;
        }
        for (const char ch : text) {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (std::isalnum(c) || c == '_' || c == ':' || c == '<' || c == '>' || c == '&' || c == '*' || c == '~' || c == ' ') {
                continue;
            }
            return false;
        }
        return true;
    }

    bool isNumberText(const std::string& text)
    {
        if (text.empty()) {
            return false;
        }

        size_t index = 0;
        if (text[index] == '-') {
            ++index;
        }
        if (index >= text.size()) {
            return false;
        }

        if (index + 2 <= text.size() && text[index] == '0' && (text[index + 1] == 'x' || text[index + 1] == 'X')) {
            index += 2;
            if (index >= text.size()) {
                return false;
            }
            for (; index < text.size(); ++index) {
                if (std::isxdigit(static_cast<unsigned char>(text[index])) == 0) {
                    return false;
                }
            }
            return true;
        }

        for (; index < text.size(); ++index) {
            if (std::isdigit(static_cast<unsigned char>(text[index])) == 0) {
                return false;
            }
        }
        return true;
    }

    bool isWrappedInParentheses(const std::string& text)
    {
        if (text.size() < 2 || text.front() != '(' || text.back() != ')') {
            return false;
        }

        int depth = 0;
        for (size_t index = 0; index < text.size(); ++index) {
            if (text[index] == '(') {
                ++depth;
                continue;
            }
            if (text[index] == ')') {
                --depth;
                if (depth == 0 && index + 1 < text.size()) {
                    return false;
                }
            }
        }
        return depth == 0;
    }

    class ExpressionParser
    {
      public:
        ExpressionParser(std::string text) : expression(trimCopy(text))
        {
        }

        std::unique_ptr<Expression> parse() const
        {
            if (expression.empty()) {
                return nullptr;
            }

            const std::string unwrapped = unwrapOuterParentheses(expression);
            if (unwrapped != expression) {
                return ExpressionParser(unwrapped).parse();
            }

            if (expression.size() >= 2 && expression.front() == '"' && expression.back() == '"') {
                return std::make_unique<ConstantExpression>(expression);
            }
            if (isIdentifierText(expression)) {
                return std::make_unique<VariableExpression>(expression);
            }
            if (isNumberText(expression)) {
                return std::make_unique<ConstantExpression>(expression);
            }

            if (expression.starts_with("*") && firstPrimaryLength(expression) == expression.size()) {
                if (auto operand = ExpressionParser(expression.substr(1)).parse()) {
                    return std::make_unique<UnaryExpression>(UnaryOp::Dereference, std::move(operand));
                }
            }

            if (auto cast = parseCast()) {
                return cast;
            }
            if (auto call = parseCall()) {
                return call;
            }
            if (auto subscript = parseSubscript()) {
                return subscript;
            }
            if (auto comparison = parseComparison()) {
                return comparison;
            }
            if (auto binary = parseBinary()) {
                return binary;
            }
            if (auto unary = parseUnary()) {
                return unary;
            }
            return nullptr;
        }

      private:
        std::string expression;

        std::unique_ptr<Expression> parseCast() const
        {
            if (expression.empty() || expression.front() != '(') {
                return nullptr;
            }
            int depth         = 0;
            size_t closeParen = std::string::npos;
            for (size_t i = 0; i < expression.size(); ++i) {
                if (expression[i] == '(') {
                    ++depth;
                } else if (expression[i] == ')') {
                    --depth;
                    if (depth == 0) {
                        closeParen = i;
                        break;
                    }
                }
            }
            if (closeParen == std::string::npos || closeParen + 1 >= expression.size()) {
                return nullptr;
            }

            const std::string typeName = trimCopy(expression.substr(1, closeParen - 1));
            if (!isKnownTypeName(typeName)) {
                return nullptr;
            }
            auto operand = ExpressionParser(expression.substr(closeParen + 1)).parse();
            if (!operand) {
                return nullptr;
            }
            return std::make_unique<CastExpression>(typeName, std::move(operand));
        }

        static std::string unwrapOuterParentheses(const std::string& text)
        {
            if (!isWrappedInParentheses(text)) {
                return text;
            }
            return trimCopy(text.substr(1, text.size() - 2));
        }

        static std::optional<size_t> findTopLevelCallOpenParen(const std::string& text)
        {
            int depth = 0;
            for (size_t index = 0; index < text.size(); ++index) {
                if (text[index] == '(') {
                    if (depth == 0) {
                        return index;
                    }
                    ++depth;
                    continue;
                }
                if (text[index] == ')') {
                    --depth;
                }
            }
            return std::nullopt;
        }

        static std::vector<std::string> splitCallArguments(const std::string& text)
        {
            std::vector<std::string> arguments;
            size_t start = 0;
            int depth    = 0;
            for (size_t index = 0; index < text.size(); ++index) {
                if (text[index] == '(') {
                    ++depth;
                    continue;
                }
                if (text[index] == ')') {
                    --depth;
                    continue;
                }
                if (text[index] == ',' && depth == 0) {
                    arguments.push_back(trimCopy(text.substr(start, index - start)));
                    start = index + 1;
                }
            }

            const std::string lastArgument = trimCopy(text.substr(start));
            if (!lastArgument.empty()) {
                arguments.push_back(lastArgument);
            }
            return arguments;
        }

        template <typename Op>
        static std::optional<std::pair<size_t, Op>> findTopLevelOperator(const std::string& text, const std::vector<std::pair<std::string_view, Op>>& operators)
        {
            std::optional<std::pair<size_t, Op>> result;
            int resultPrecedence = 100;

            for (const auto& [operatorText, op] : operators) {
                int depth = 0;
                for (size_t index = 0; index + operatorText.size() <= text.size(); ++index) {
                    if (text[index] == '(') {
                        ++depth;
                        continue;
                    }
                    if (text[index] == ')') {
                        --depth;
                        continue;
                    }
                    if (depth == 0 && text.compare(index, operatorText.size(), operatorText) == 0) {
                        const int precedence = operatorPrecedence(op);
                        if (!result.has_value() || precedence < resultPrecedence || (precedence == resultPrecedence && index > result->first)) {
                            result           = std::make_pair(index, op);
                            resultPrecedence = precedence;
                        }
                    }
                }
            }
            return result;
        }

        static int operatorPrecedence(const BinaryOp op)
        {
            return binaryOpPrecedence(op);
        }

        static int operatorPrecedence(const ComparisonOp op)
        {
            return comparisonOpPrecedence(op);
        }

        std::unique_ptr<Expression> parseCall() const
        {
            if (!expression.ends_with(")")) {
                return nullptr;
            }

            const auto openParen = findTopLevelCallOpenParen(expression);
            if (!openParen.has_value()) {
                return nullptr;
            }

            const std::string callee = trimCopy(expression.substr(0, *openParen));
            if (!isIdentifierText(callee) && !isCppQualifiedNameText(callee)) {
                return nullptr;
            }

            std::vector<std::unique_ptr<Expression>> arguments;
            const std::string argumentText = expression.substr(*openParen + 1, expression.size() - *openParen - 2);
            for (const auto& argument : splitCallArguments(argumentText)) {
                auto argumentExpression = ExpressionParser(argument).parse();
                if (!argumentExpression) {
                    return nullptr;
                }
                arguments.push_back(std::move(argumentExpression));
            }
            return std::make_unique<CallExpression>(callee, std::move(arguments));
        }

        std::unique_ptr<Expression> parseComparison() const
        {
            const std::vector<std::pair<std::string_view, ComparisonOp>> operators = {
                { " == ", ComparisonOp::Equal },        { " != ", ComparisonOp::NotEqual }, { " <= ", ComparisonOp::LessEqual },
                { " >= ", ComparisonOp::GreaterEqual }, { " < ", ComparisonOp::Less },      { " > ", ComparisonOp::Greater },
            };

            const auto comparisonOperator = findTopLevelOperator(expression, operators);
            if (!comparisonOperator.has_value()) {
                return nullptr;
            }

            const std::string lhs = trimCopy(expression.substr(0, comparisonOperator->first));
            const std::string rhs = trimCopy(expression.substr(comparisonOperator->first + comparisonOpText(comparisonOperator->second).size() + 2));
            auto lhsExpression    = ExpressionParser(lhs).parse();
            auto rhsExpression    = ExpressionParser(rhs).parse();
            if (!lhsExpression || !rhsExpression) {
                return nullptr;
            }
            return std::make_unique<ComparisonExpression>(comparisonOperator->second, std::move(lhsExpression), std::move(rhsExpression));
        }

        std::unique_ptr<Expression> parseBinary() const
        {
            const std::vector<std::pair<std::string_view, BinaryOp>> operators = {
                { " | ", BinaryOp::BitOr }, { " ^ ", BinaryOp::BitXor }, { " & ", BinaryOp::BitAnd }, { " << ", BinaryOp::Shl }, { " >> ", BinaryOp::Shr },
                { " + ", BinaryOp::Add },   { " - ", BinaryOp::Sub },    { " * ", BinaryOp::Mul },    { " / ", BinaryOp::Div },  { " % ", BinaryOp::Mod },
            };

            const auto binaryOperator = findTopLevelOperator(expression, operators);
            if (!binaryOperator.has_value()) {
                return nullptr;
            }

            const std::string lhs = trimCopy(expression.substr(0, binaryOperator->first));
            const std::string rhs = trimCopy(expression.substr(binaryOperator->first + binaryOpText(binaryOperator->second).size() + 2));
            auto lhsExpression    = ExpressionParser(lhs).parse();
            auto rhsExpression    = ExpressionParser(rhs).parse();
            if (!lhsExpression || !rhsExpression) {
                return nullptr;
            }
            return std::make_unique<BinaryExpression>(binaryOperator->second, std::move(lhsExpression), std::move(rhsExpression));
        }

        std::unique_ptr<Expression> parseSubscript() const
        {
            if (!expression.ends_with("]")) {
                return nullptr;
            }
            const auto lb = expression.rfind('[');
            if (lb == std::string::npos || lb == 0) {
                return nullptr;
            }
            const std::string arrayPart = trimCopy(expression.substr(0, lb));
            if (!isIdentifierText(arrayPart)) {
                return nullptr;
            }
            const std::string indexPart = trimCopy(expression.substr(lb + 1, expression.size() - lb - 2));
            auto arrayExpr              = ExpressionParser(arrayPart).parse();
            auto indexExpr              = ExpressionParser(indexPart).parse();
            if (!arrayExpr || !indexExpr) {
                return nullptr;
            }
            return std::make_unique<SubscriptExpression>(std::move(arrayExpr), std::move(indexExpr));
        }

        std::unique_ptr<Expression> parseUnary() const
        {
            if (expression.ends_with("++")) {
                return parseUnaryOperand(UnaryOp::PostIncrement, 0, expression.size() - 2);
            }
            if (expression.ends_with("--")) {
                return parseUnaryOperand(UnaryOp::PostDecrement, 0, expression.size() - 2);
            }
            if (expression.starts_with("++")) {
                return parseUnaryOperand(UnaryOp::PreIncrement, 2, expression.size() - 2);
            }
            if (expression.starts_with("--")) {
                return parseUnaryOperand(UnaryOp::PreDecrement, 2, expression.size() - 2);
            }
            if (expression.starts_with("!")) {
                return parseUnaryOperand(UnaryOp::LogicalNot, 1, expression.size() - 1);
            }
            if (expression.starts_with("~")) {
                return parseUnaryOperand(UnaryOp::BitwiseNot, 1, expression.size() - 1);
            }
            return nullptr;
        }

        std::unique_ptr<Expression> parseUnaryOperand(const UnaryOp op, const size_t start, const size_t count) const
        {
            auto operand = ExpressionParser(expression.substr(start, count)).parse();
            if (!operand) {
                return nullptr;
            }
            return std::make_unique<UnaryExpression>(op, std::move(operand));
        }
    };
} // namespace

std::unique_ptr<Expression> makeExpressionFromText(const std::string& text)
{
    return ExpressionParser(text).parse();
}
} // namespace Decompiler
