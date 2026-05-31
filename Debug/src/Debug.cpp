#include "Debug.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Functions.h"

namespace Decompiler::Debug
{
namespace
{
    std::filesystem::path outputPath(const std::string& fileName)
    {
        const auto directory = std::filesystem::path("output");
        std::filesystem::create_directories(directory);
        return directory / fileName;
    }

    std::string operandText(const IROperand& operand)
    {
        return operand.name.empty() ? "<empty>" : operand.name;
    }

    std::string conditionText(const ConditionCode condition)
    {
        switch (condition) {
        case ConditionCode::None:
            return "None";
        case ConditionCode::E:
            return "E";
        case ConditionCode::NE:
            return "NE";
        case ConditionCode::L:
            return "L";
        case ConditionCode::LE:
            return "LE";
        case ConditionCode::G:
            return "G";
        case ConditionCode::GE:
            return "GE";
        case ConditionCode::B:
            return "B";
        case ConditionCode::BE:
            return "BE";
        case ConditionCode::A:
            return "A";
        case ConditionCode::AE:
            return "AE";
        }
        return "None";
    }

    void writeIrInstruction(std::ostream& out, const IRInstruction& instruction, const int indent)
    {
        out << std::string(indent, ' ') << "0x" << std::hex << instruction.address << std::dec << " op=" << instruction.op
            << " cond=" << conditionText(instruction.condition) << " operands=[";

        for (size_t i = 0; i < instruction.operands.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << operandText(instruction.operands[i]);
        }

        out << "]\n";
    }

    void writeIrInstructions(std::ostream& out, const std::vector<IRInstruction>& instructions, const int indent)
    {
        for (const auto& instruction : instructions) {
            writeIrInstruction(out, instruction, indent);
        }
    }

    void writeAsm(std::ostream& out, const std::vector<AssemblyInstruction>& instructions)
    {
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

    void writeFunctions(std::ostream& out, const std::vector<DecompilerFunction>& functions)
    {
        for (const auto& function : functions) {
            out << "function " << function.name << " start=0x" << std::hex << function.start_address << std::dec
                << " returns=" << (function.signature.returns_value ? "int" : "void") << '\n';

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

    using SymbolEntry = std::pair<uint64_t, std::string>;

    bool symbolEntryByAddress(const SymbolEntry& lhs, const SymbolEntry& rhs)
    {
        return lhs.first < rhs.first;
    }

    std::vector<SymbolEntry> sortedSymbols(const std::unordered_map<uint64_t, std::string>& symbols)
    {
        std::vector<SymbolEntry> entries(symbols.begin(), symbols.end());
        std::sort(entries.begin(), entries.end(), symbolEntryByAddress);
        return entries;
    }

    void writeSymbolGroup(std::ostream& out, const std::string& title, const std::unordered_map<uint64_t, std::string>& symbols)
    {
        out << title << ":\n";
        if (symbols.empty()) {
            out << "  <none>\n\n";
            return;
        }

        for (const auto& [address, name] : sortedSymbols(symbols)) {
            out << "  0x" << std::hex << address << std::dec << " " << name << '\n';
        }
        out << '\n';
    }

    void writeSymbols(std::ostream& out, const Symbols::ResolvedSymbols& symbols)
    {
        writeSymbolGroup(out, "functions", symbols.functions);
        writeSymbolGroup(out, "globals", symbols.globals);
    }
} // namespace

void dumpArtifacts(
      const std::vector<AssemblyInstruction>& input,
      const std::vector<IRInstruction>& irs,
      const Symbols::ResolvedSymbols& symbols,
      const std::vector<DecompilerFunction>& functions)
{
    std::ofstream asmOut(outputPath("asm.txt"));
    std::ofstream irOut(outputPath("ir.txt"));
    std::ofstream symbolsOut(outputPath("symbols.txt"));
    std::ofstream functionsOut(outputPath("functions.txt"));

    writeAsm(asmOut, input);
    writeIrInstructions(irOut, irs, 0);
    writeSymbols(symbolsOut, symbols);
    writeFunctions(functionsOut, functions);
}
} // namespace Decompiler::Debug
