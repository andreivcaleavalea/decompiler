#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "IRInstruction.h"
#include "IRProperties.h"

namespace Decompiler::ASTDetail
{
std::string trimCopy(const std::string& value);
bool parseSignedInteger(const std::string& raw, long long& out);
std::string normalizeImmediateForDisplay(const std::string& operand);
std::string invertComparisonOperator(const std::string& op);
bool startsWithAny(const std::string& value, std::initializer_list<std::string_view> prefixes);
bool isTemporaryName(const std::string& value);
std::string substituteTempFromDefinitions(const std::vector<IRInstruction>& instrs, size_t cmpIndex, const std::string& operand);
std::string renderArrayAccess(const std::vector<IRInstruction>& instrs, size_t cmpIndex, const std::string& addressReg);
std::string convertCastType(const IROperand& dest);
std::string invertConditionExpr(const std::string& expr);
std::string stripReturnType(const std::string& demangledName);
} // namespace Decompiler::ASTDetail
