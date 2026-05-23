#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "AST.h"
#include "ControlGraphFlow.h"
#include "IRTypes.h"

namespace Decompiler
{
struct FunctionParameter {
    std::string name;
};

struct FunctionSignature {
    std::vector<FunctionParameter> parameters;
    bool returns_value = false;
};

struct DecompilerFunction {
    uint64_t start_address = 0;
    std::string name;
    std::vector<IRInstruction> instructions;
    FunctionSignature signature;
    CallingConvention calling_convention = CallingConvention::Unknown;
    Graph graph;
    std::vector<std::unique_ptr<ASTNode>> ast;
};

std::vector<DecompilerFunction> findFunctions(const std::vector<IRInstruction>& irs);
std::unordered_map<uint64_t, std::string> functionNamesByAddress(const std::vector<DecompilerFunction>& functions);
std::optional<uint64_t> parseAddressOperand(const std::string& raw);
std::string substituteArgumentRegisters(std::string line, const FunctionSignature& signature, CallingConvention callingConvention);
} // namespace Decompiler
