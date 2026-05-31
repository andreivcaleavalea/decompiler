#include "Pipeline.h"

#include <cstdio>
#include <string>
#include <vector>

#include "AST.h"
#include "ASTDetail.h"
#include "ControlGraphFlow.h"
#include "Dominators.h"
#include "IRProperties.h"
#include "Names.h"
#include "WindowsX64.h"
#include "SSA.h"
#include "StackVariableRecovery.h"
#include "TypeInference.h"

namespace Decompiler
{
namespace
{
    bool isRuntimeReachabilityStop(const std::string& functionName)
    {
        return functionName == "__main" || functionName == "mainCRTStartup" || functionName == "WinMainCRTStartup" ||
               functionName.starts_with("__do_global_") || functionName.starts_with("__tmainCRTStartup");
    }

    void resolveInstructionOperands(std::vector<IRInstruction>& instructions, const std::unordered_map<uint64_t, std::string>& functionNames)
    {
        for (auto& instruction : instructions) {
            if (IRProperties::isCall(instruction.type)) {
                const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).name);
                if (target.has_value()) {
                    const auto nameIt = functionNames.find(*target);
                    if (nameIt != functionNames.end()) {
                        if (IRProperties::hasOperandAt(instruction, 0)) {
                            instruction.operands[0].name = nameIt->second;
                        }
                        if (isRuntimeReachabilityStop(nameIt->second)) {
                            instruction.op   = "nop";
                            instruction.type = IRType::NOP;
                        }
                    }
                }
            }

            for (auto& operand : instruction.operands) {
                if (!operand.isHeapDeref() || !operand.heapDeref.absolute.has_value()) {
                    continue;
                }
                const auto nameIt = functionNames.find(*operand.heapDeref.absolute);
                if (nameIt != functionNames.end()) {
                    operand.tag  = OperandTag::Immediate;
                    operand.name = nameIt->second;
                }
            }
        }
    }
} // namespace

void buildFunctionIR(DecompilerFunction& function, DecompilerProgram& program, const std::unordered_map<uint64_t, std::string>& functionNames)
{
    std::vector<IRInstruction> instructions = function.instructions;
    resolveInstructionOperands(instructions, functionNames);

    function.graph = buildCFG(instructions);
    if (function.graph.blocks.empty()) {
        return;
    }

    const bool is64Bit  = program.context.subformat != BinarySubformat::PE32;
    function.stackFrame = StackFrame::analyze(function.graph, is64Bit);
    recoverStackVariables(function.graph, function.stackFrame);
    recoverGlobalVariables(function.graph, program.globals);

    applySSA(function.graph);
    function.types = inferTypes(function.graph);

    for (const auto& [ssaName, type] : function.types) {
        std::string base;
        if (ssaName.starts_with("var_") || ssaName.starts_with("arg_")) {
            base = ssaName;
        } else {
            if (type.isUnknown())
                continue;
            base = normalizedRegisterBase(ssaBaseName(ssaName));
            if (type.category == TypeCategory::Float && !base.starts_with("xmm")) {
                continue;
            }
        }
        function.baseTypes[base].meet(type);
    }

    removeSSA(function.graph);

    for (auto& block : function.graph.blocks) {
        for (auto& instr : block.instructions) {
            if (instr.type != IRType::STORE || instr.operands.size() < 2)
                continue;
            const auto& dest = instr.operands[0];
            auto& src        = instr.operands[1];
            if (!dest.isStackVar() || !src.isImmediate())
                continue;
            const auto typeIt = function.baseTypes.find(dest.name);
            if (typeIt == function.baseTypes.end())
                continue;
            const VarType& varType = typeIt->second;
            if (varType.sign == TypeSign::Unsigned)
                continue;
            uint64_t signBit = 0;
            if (varType.size == TypeSize::Bits8)
                signBit = 0x80;
            else if (varType.size == TypeSize::Bits16)
                signBit = 0x8000;
            else
                continue;
            long long immValue = 0;
            if (!ASTDetail::parseSignedInteger(src.name, immValue) || immValue < 0)
                continue;
            const uint64_t uval = static_cast<uint64_t>(immValue);
            if (uval < signBit)
                continue;
            const int64_t signedVal = (static_cast<int64_t>(uval) ^ static_cast<int64_t>(signBit)) - static_cast<int64_t>(signBit);
            src.name                = std::to_string(signedVal);
        }
    }
}

CallSiteSignatureMap buildUserSignatureMap(const std::vector<DecompilerFunction>& functions)
{
    CallSiteSignatureMap map;
    for (const auto& function : functions) {
        CallSiteSignature sig;

        if (function.signature.returns_value) {
            const auto it = function.baseTypes.find("rax");
            if (it != function.baseTypes.end()) {
                sig.returnType = it->second;
            }
        }

        for (size_t i = 0; i < function.signature.parameters.size(); ++i) {
            const std::string regBase = registerBaseForArgumentIndex(i);
            if (regBase.empty())
                break;
            const auto it = function.baseTypes.find(regBase);
            sig.paramTypes.push_back(it != function.baseTypes.end() ? it->second : VarType{});
        }

        if (!sig.returnType.isUnknown() || !sig.paramTypes.empty()) {
            map[function.name] = std::move(sig);
        }
    }
    return map;
}

void refineInterProceduralTypes(DecompilerFunction& function, const CallSiteSignatureMap& userSignatures)
{
    if (function.graph.blocks.empty() || userSignatures.empty())
        return;

    for (const auto& block : function.graph.blocks) {
        for (size_t instrIdx = 0; instrIdx < block.instructions.size(); ++instrIdx) {
            const auto& instr = block.instructions[instrIdx];
            if (!IRProperties::isCall(instr.type) || instr.operands.empty())
                continue;

            const auto sigIt = userSignatures.find(instr.operands[0].name);
            if (sigIt == userSignatures.end())
                continue;
            const auto& sig = sigIt->second;

            for (size_t argIdx = 0; argIdx < sig.paramTypes.size(); ++argIdx) {
                if (sig.paramTypes[argIdx].isUnknown())
                    continue;
                const std::string regBase = registerBaseForArgumentIndex(argIdx);
                if (regBase.empty())
                    break;

                function.baseTypes[regBase].meet(sig.paramTypes[argIdx]);

                for (size_t j = instrIdx; j-- > 0;) {
                    const auto& prev = block.instructions[j];
                    if (prev.operands.empty())
                        continue;
                    const auto& dest = prev.operands[0];
                    if (dest.tag != OperandTag::Register || dest.name.empty())
                        continue;
                    if (normalizedRegisterBase(dest.name) != regBase)
                        continue;
                    if (prev.operands.size() > 1 && prev.operands[1].tag == OperandTag::Register && !prev.operands[1].name.empty()) {
                        const std::string src = prev.operands[1].name;
                        if (src.starts_with("var_") || src.starts_with("arg_")) {
                            function.baseTypes[src].meet(sig.paramTypes[argIdx]);
                        } else {
                            function.baseTypes[normalizedRegisterBase(src)].meet(sig.paramTypes[argIdx]);
                        }
                    }
                    break;
                }
            }

            if (!sig.returnType.isUnknown()) {
                function.baseTypes["rax"].meet(sig.returnType);

                for (size_t j = instrIdx + 1; j < block.instructions.size(); ++j) {
                    const auto& next = block.instructions[j];
                    if (next.operands.size() < 2)
                        continue;
                    const auto& dest = next.operands[0];
                    const auto& src  = next.operands[1];
                    if (src.tag != OperandTag::Register || src.name.empty())
                        continue;
                    if (normalizedRegisterBase(src.name) != "rax")
                        continue;
                    if (dest.tag != OperandTag::Register || dest.name.empty())
                        continue;
                    const std::string destBase =
                          (dest.name.starts_with("var_") || dest.name.starts_with("arg_")) ? dest.name : normalizedRegisterBase(dest.name);
                    function.baseTypes[destBase].meet(sig.returnType);
                    break;
                }
            }
        }
    }
}

void buildFunctionAST(DecompilerFunction& function)
{
    if (function.graph.blocks.empty()) {
        return;
    }

    const auto dominators     = Dominators::computeDominators(function.graph);
    const auto postdominators = Dominators::computeImmediatePostDominators(Dominators::computePostDominators(function.graph));
    function.ast = build_ast(function.graph, dominators, postdominators, 0, static_cast<size_t>(-1), static_cast<size_t>(-1), static_cast<size_t>(-1));

    restructureForLoops(function.ast);
}
} // namespace Decompiler
