#include "Decompiler.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include "Functions.h"
#include "IR.h"
#include "Optimizations.h"
#include "Pipeline.h"
#include "Program.h"
#include "Show.h"
#include "SymbolTable.h"

namespace Decompiler
{

std::vector<std::string> Decompile(const std::vector<AssemblyInstruction>& input, const DecompileContext& context)
{
    const auto irs = transformASMToIR(input);
    if (irs.empty()) {
        return { "NO INSTRUCTIONS" };
    }

    DecompilerProgram program;
    program.context    = context;
    const bool is64Bit = context.subformat != BinarySubformat::PE32;
    program.functions  = findFunctions(irs, is64Bit);

    if (program.functions.empty()) {
        return { "NO FUNCTIONS" };
    }

    Symbols::ResolvedSymbols symbols;
    if (context.format == BinaryFormat::PE) {
        symbols = Symbols::resolveCOFFSymbols(context.sections, context.coffSymbols);
        Symbols::applyFunctionSymbols(program.functions, symbols);

        resolveThunks(program.functions, symbols.globals);
    }

    const auto functionNames = functionNamesByAddress(program.functions);
    program.globals          = buildGlobalSymbolTable(context, symbols.globals);

    for (auto& function : program.functions) {
        buildFunctionIR(function, program, functionNames);
    }

    const auto userSignatures = buildUserSignatureMap(program.functions);
    for (auto& function : program.functions) {
        refineInterProceduralTypes(function, userSignatures);
    }

    Optimizations::runAll(program);

    for (auto& function : program.functions) {
        buildFunctionAST(function);
    }

    return showFunctions(program);
}

std::string DecompileToString(const std::vector<AssemblyInstruction>& input, const DecompileContext& context, size_t maxInstructions)
{
    const auto limit = (maxInstructions == 0) ? input.size() : std::min(input.size(), maxInstructions);
    std::vector<AssemblyInstruction> instructions;
    instructions.reserve(limit);
    instructions.insert(instructions.end(), input.begin(), input.begin() + static_cast<std::ptrdiff_t>(limit));

    const auto lines = Decompile(instructions, context);

    std::ostringstream output;
    for (const auto& line : lines) {
        output << line << '\n';
    }

    if (limit < input.size()) {
        output << "\n/* Output truncated: instruction limit reached. */\n";
    }

    const std::string text = output.str();

    const char* dumpPath = std::getenv("GVIEW_DECOMPILER_DUMP");
    std::ofstream dumpFile(dumpPath != nullptr ? dumpPath : "decompiler_output.txt");
    if (dumpFile.is_open()) {
        dumpFile << text;
    }

    return text;
}

DecompiledProgramOutput DecompileFunctions(const std::vector<AssemblyInstruction>& input, const DecompileContext& context)
{
    const auto irs = transformASMToIR(input);
    if (irs.empty()) {
        return {};
    }

    DecompilerProgram program;
    program.context    = context;
    const bool is64Bit = context.subformat != BinarySubformat::PE32;
    program.functions  = findFunctions(irs, is64Bit);

    if (program.functions.empty()) {
        return {};
    }

    Symbols::ResolvedSymbols symbols;
    if (context.format == BinaryFormat::PE) {
        symbols = Symbols::resolveCOFFSymbols(context.sections, context.coffSymbols);
        Symbols::applyFunctionSymbols(program.functions, symbols);
        resolveThunks(program.functions, symbols.globals);
    }

    const auto functionNames = functionNamesByAddress(program.functions);
    program.globals          = buildGlobalSymbolTable(context, symbols.globals);

    for (auto& function : program.functions) {
        buildFunctionIR(function, program, functionNames);
    }

    const auto userSignatures = buildUserSignatureMap(program.functions);
    for (auto& function : program.functions) {
        refineInterProceduralTypes(function, userSignatures);
    }

    Optimizations::runAll(program);

    for (auto& function : program.functions) {
        buildFunctionAST(function);
    }

    const auto rendered = showFunctionsPerFunction(program);

    DecompiledProgramOutput result;
    result.globalDeclarations = rendered.globalDeclarations;
    result.functions.reserve(rendered.functions.size());
    for (const auto& r : rendered.functions) {
        result.functions.push_back({ r.startAddress, r.lines });
    }
    return result;
}
} // namespace Decompiler
