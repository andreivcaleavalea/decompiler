#include "Debug.h"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "AST.h"
#include "ControlGraphFlow.h"
#include "Dominators.h"
#include "SSA.h"

#include "../Decompile/Functions.h"

namespace Decompiler::Debug {
    namespace {
        struct FunctionDebugState {
            std::vector<IRInstruction> instructions;
            Graph beforeSsa;
            Graph afterApplySsa;
            Graph afterRemoveSsa;
            std::vector<std::unique_ptr<ASTNode>> ast;
        };

        std::filesystem::path outputPath(const std::string& fileName) {
            const auto directory = std::filesystem::path("output");
            std::filesystem::create_directories(directory);
            return directory / fileName;
        }

        std::string operandText(const IROperand& operand) {
            return operand.value.empty() ? "<empty>" : operand.value;
        }

        std::string conditionText(const ConditionCode condition) {
            switch (condition) {
                case ConditionCode::None: return "None";
                case ConditionCode::E: return "E";
                case ConditionCode::NE: return "NE";
                case ConditionCode::L: return "L";
                case ConditionCode::LE: return "LE";
                case ConditionCode::G: return "G";
                case ConditionCode::GE: return "GE";
                case ConditionCode::B: return "B";
                case ConditionCode::BE: return "BE";
                case ConditionCode::A: return "A";
                case ConditionCode::AE: return "AE";
            }
            return "None";
        }

        std::string callingConventionText(const CallingConvention callingConvention) {
            switch (callingConvention) {
                case CallingConvention::Unknown: return "Unknown";
                case CallingConvention::SystemV: return "SystemV";
                case CallingConvention::Win64: return "Win64";
            }
            return "Unknown";
        }

        void writeIrInstruction(std::ostream& out, const IRInstruction& instruction, const int indent) {
            out << std::string(indent, ' ')
                << "0x" << std::hex << instruction.address << std::dec
                << " op=" << instruction.op
                << " cond=" << conditionText(instruction.condition)
                << " operands=[";

            for (size_t i = 0; i < instruction.operands.size(); ++i) {
                if (i != 0) {
                    out << ", ";
                }
                out << operandText(instruction.operands[i]);
            }

            out << "]\n";
        }

        void writeIrInstructions(std::ostream& out, const std::vector<IRInstruction>& instructions, const int indent) {
            for (const auto& instruction : instructions) {
                writeIrInstruction(out, instruction, indent);
            }
        }

        void writeAsm(std::ostream& out, const std::vector<AssemblyInstruction>& instructions) {
            for (const auto& instruction : instructions) {
                std::ostringstream line;
                line << "0x" << std::hex << instruction.address << std::dec << ": " << instruction.mnemonic;
                if (!instruction.operands.empty()) {
                    line << " " << instruction.operands;
                }
                if (!instruction.comment.empty()) {
                    line << " ; " << instruction.comment;
                }
                if (!instruction.bytes.empty()) {
                    line << " ; bytes=" << instruction.bytes;
                }
                out << line.str() << '\n';
            }
        }

        void writeFunctions(std::ostream& out, const std::vector<DecompilerFunction>& functions) {
            for (const auto& function : functions) {
                out << "function " << function.name
                    << " start=0x" << std::hex << function.start_address << std::dec
                    << " returns=" << (function.signature.returns_value ? "int" : "void")
                    << " cc=" << callingConventionText(function.calling_convention)
                    << '\n';

                out << "parameters:";
                if (function.signature.parameters.empty()) {
                    out << " <none>";
                } else {
                    for (size_t i = 0; i < function.signature.parameters.size(); ++i) {
                        out << (i == 0 ? " " : ", ") << function.signature.parameters[i].name;
                    }
                }
                out << '\n';

                writeIrInstructions(out, function.instructions, 2);
                out << '\n';
            }
        }

        void writeGraph(std::ostream& out, const DecompilerFunction& function, const Graph& graph) {
            out << "function " << function.name
                << " start=0x" << std::hex << function.start_address << std::dec
                << " blocks=" << graph.blocks.size()
                << '\n';

            if (graph.blocks.empty()) {
                out << "  <no blocks>\n\n";
                return;
            }

            for (const auto& block : graph.blocks) {
                out << "  block " << block.index
                    << " start=0x" << std::hex << block.start_address
                    << " end=0x" << block.end_address << std::dec
                    << " instructions=" << block.instructions.size()
                    << '\n';

                out << "    predecessors:";
                if (block.predecessors.empty()) {
                    out << " <none>";
                } else {
                    for (size_t i = 0; i < block.predecessors.size(); ++i) {
                        out << (i == 0 ? " " : ", ") << block.predecessors[i];
                    }
                }
                out << '\n';

                out << "    successors:";
                if (block.successors.empty()) {
                    out << " <none>";
                } else {
                    for (size_t i = 0; i < block.successors.size(); ++i) {
                        out << (i == 0 ? " " : ", ") << block.successors[i];
                    }
                }
                out << '\n';

                out << "    ir:\n";
                writeIrInstructions(out, block.instructions, 4);
            }

            out << '\n';
        }

        std::vector<IRInstruction> normalizeFunctionInstructions(
            const DecompilerFunction& function,
            const std::unordered_map<uint64_t, std::string>& functionNames) {
            std::vector<IRInstruction> instructions = function.instructions;
            for (auto& instruction : instructions) {
                if (!IRProperties::isCall(instruction.type)) {
                    continue;
                }

                const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).value);
                if (!target.has_value()) {
                    continue;
                }

                const auto nameIt = functionNames.find(*target);
                if (nameIt == functionNames.end() || !IRProperties::hasOperandAt(instruction, 0)) {
                    continue;
                }

                instruction.operands[0].value = nameIt->second;
            }
            return instructions;
        }

        std::vector<std::unique_ptr<ASTNode>> buildAst(const Graph& graph) {
            const auto dominators = Dominators::computeDominators(graph);
            const auto postdominators =
                Dominators::computeImmediatePostDominators(Dominators::computePostDominators(graph));
            return build_ast(graph, dominators, postdominators, 0, static_cast<size_t>(-1));
        }

        FunctionDebugState analyzeFunction(
            const DecompilerFunction& function,
            const std::unordered_map<uint64_t, std::string>& functionNames) {
            FunctionDebugState state;
            state.instructions = normalizeFunctionInstructions(function, functionNames);
            state.beforeSsa = buildCFG(state.instructions);
            state.afterApplySsa = state.beforeSsa;
            applySSA(state.afterApplySsa);
            state.afterRemoveSsa = state.afterApplySsa;
            removeSSA(state.afterRemoveSsa);
            state.ast = buildAst(state.afterRemoveSsa);
            return state;
        }

        void writeAstNodes(
            std::ostream& out,
            const std::vector<std::unique_ptr<ASTNode>>& nodes,
            const int indent) {
            const std::string spaces(indent, ' ');
            for (const auto& node : nodes) {
                if (const auto* block = dynamic_cast<const SimpleBlockNode*>(node.get())) {
                    out << spaces << "simple_block block=" << block->block_index
                        << " instructions=" << block->instructions.size() << '\n';
                    writeIrInstructions(out, block->instructions, indent + 2);
                    continue;
                }

                if (const auto* ifNode = dynamic_cast<const IfNode*>(node.get())) {
                    out << spaces << "if condition=" << ifNode->condition_expr << '\n';
                    out << spaces << "  true:\n";
                    writeAstNodes(out, ifNode->true_branch, indent + 4);
                    if (!ifNode->false_branch.empty()) {
                        out << spaces << "  false:\n";
                        writeAstNodes(out, ifNode->false_branch, indent + 4);
                    }
                    continue;
                }

                if (const auto* whileNode = dynamic_cast<const WhileNode*>(node.get())) {
                    out << spaces << "while condition=" << whileNode->condition_expr << '\n';
                    writeAstNodes(out, whileNode->body, indent + 2);
                    continue;
                }

                if (const auto* doWhileNode = dynamic_cast<const DoWhileNode*>(node.get())) {
                    out << spaces << "do_while condition=" << doWhileNode->condition_expr << '\n';
                    writeAstNodes(out, doWhileNode->body, indent + 2);
                    continue;
                }

                if (dynamic_cast<const BreakNode*>(node.get()) != nullptr) {
                    out << spaces << "break\n";
                    continue;
                }

                if (dynamic_cast<const ContinueNode*>(node.get()) != nullptr) {
                    out << spaces << "continue\n";
                }
            }
        }

        void writeAst(std::ostream& out, const DecompilerFunction& function, const FunctionDebugState& state) {
            out << "function " << function.name
                << " start=0x" << std::hex << function.start_address << std::dec
                << '\n';
            out << "ast:\n";
            writeAstNodes(out, state.ast, 2);
            out << "rendered:\n";
            for (const auto& node : state.ast) {
                for (const auto& line : node->print(2)) {
                    out << line << '\n';
                }
            }
            out << '\n';
        }
    }

    void dumpArtifacts(const std::vector<AssemblyInstruction>& input, const std::vector<IRInstruction>& irs) {
        std::ofstream asmOut(outputPath("asm.txt"));
        std::ofstream irOut(outputPath("ir.txt"));
        std::ofstream functionsOut(outputPath("functions.txt"));
        std::ofstream blocksOut(outputPath("blocks.txt"));
        std::ofstream afterApplyOut(outputPath("after_apply_ssa.txt"));
        std::ofstream afterRemoveOut(outputPath("after_remove_ssa.txt"));
        std::ofstream afterAstOut(outputPath("after_ast.txt"));

        writeAsm(asmOut, input);
        writeIrInstructions(irOut, irs, 0);

        const auto functions = findFunctions(irs);
        writeFunctions(functionsOut, functions);

        const auto functionNames = functionNamesByAddress(functions);
        for (const auto& function : functions) {
            const auto state = analyzeFunction(function, functionNames);
            writeGraph(blocksOut, function, state.beforeSsa);
            writeGraph(afterApplyOut, function, state.afterApplySsa);
            writeGraph(afterRemoveOut, function, state.afterRemoveSsa);
            writeAst(afterAstOut, function, state);
        }
    }
}
