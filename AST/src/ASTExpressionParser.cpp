#include "ASTExpression.h"

#include <optional>
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

            if (isIdentifierText(expression)) {
                return std::make_unique<VariableExpression>(expression);
            }
            if (isNumberText(expression)) {
                return std::make_unique<ConstantExpression>(expression);
            }

            if (auto call = parseCall()) {
                return call;
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
            if (!isIdentifierText(callee)) {
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
                { " + ", BinaryOp::Add },   { " - ", BinaryOp::Sub },    { " * ", BinaryOp::Mul },    { " / ", BinaryOp::Div },
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
