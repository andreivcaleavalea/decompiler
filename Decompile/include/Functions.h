#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "AST.h"
#include "ControlGraphFlow.h"
#include "IRInstruction.h"
#include "StackFrame.h"
#include "TypeInference.h"

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
    bool isThunk = false;
    std::vector<IRInstruction> instructions;
    FunctionSignature signature;
    Graph graph;
    StackFrame stackFrame;
    TypeMap types;
    TypeMap baseTypes;
    std::vector<std::unique_ptr<ASTNode>> ast;
};

std::vector<DecompilerFunction> findFunctions(const std::vector<IRInstruction>& irs, bool is64Bit);
std::unordered_map<uint64_t, std::string> functionNamesByAddress(const std::vector<DecompilerFunction>& functions);
std::optional<uint64_t> parseAddressOperand(const std::string& raw);
std::string substituteArgumentRegisters(std::string line, const FunctionSignature& signature);
void resolveThunks(std::vector<DecompilerFunction>& functions, const std::unordered_map<uint64_t, std::string>& globalSymbols);
} // namespace Decompiler
