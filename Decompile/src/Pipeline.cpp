#include "Pipeline.h"

#include <string>
#include <vector>

#include "ASTDetail.h"
#include "ControlGraphFlow.h"
#include "Dominators.h"
#include "IRPatternRecognition.h"
#include "SSA.h"
#include "StackVariableRecovery.h"

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
                const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).value);
                if (target.has_value()) {
                    const auto nameIt = functionNames.find(*target);
                    if (nameIt != functionNames.end()) {
                        if (IRProperties::hasOperandAt(instruction, 0)) {
                            instruction.operands[0].value = nameIt->second;
                        }
                        if (isRuntimeReachabilityStop(nameIt->second)) {
                            instruction.op   = "nop";
                            instruction.type = IRType::NOP;
                        }
                    }
                }
            }

            for (auto& operand : instruction.operands) {
                if (operand.type != OpType::MEM || !operand.memory.has_value() || !operand.memory->absolute.has_value()) {
                    continue;
                }
                const auto nameIt = functionNames.find(*operand.memory->absolute);
                if (nameIt != functionNames.end()) {
                    operand.type  = OpType::IMM;
                    operand.value = nameIt->second;
                    operand.memory.reset();
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

    for (auto& block : function.graph.blocks) {
        recognizeOptimizedDivision(block.instructions);
    }

    recoverStackVariables(function.graph, stackFrameLayoutForCallingConvention(function.calling_convention));
    recoverGlobalVariables(function.graph, program.globals);

    applySSA(function.graph);
    removeSSA(function.graph);
}

void buildFunctionAST(DecompilerFunction& function)
{
    if (function.graph.blocks.empty()) {
        return;
    }

    const auto dominators     = Dominators::computeDominators(function.graph);
    const auto postdominators = Dominators::computeImmediatePostDominators(Dominators::computePostDominators(function.graph));
    function.ast              = build_ast(
          function.graph,
          dominators,
          postdominators,
          0,
          static_cast<size_t>(-1),
          static_cast<size_t>(-1),
          static_cast<size_t>(-1),
          function.calling_convention);

    restructureForLoops(function.ast);
}
} // namespace Decompiler
