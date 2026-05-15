#include "Decompiler.h"

#include <sstream>
#include <vector>

#include "Debug.h"
#include "Functions.h"
#include "IR.h"
#include "Show.h"
#include "SymbolTable.h"

namespace Decompiler {
    std::vector<std::string> Decompile(const std::vector<AssemblyInstruction>& input, const DecompileContext& context) {
        const auto irs = asm_to_ir(input);
        if (irs.empty()) {
            return {"NO INSTRUCTIONS"};
        }

        auto functions = findFunctions(irs);
        if (functions.empty()) {
            Debug::dumpArtifacts(input, irs, {}, functions);
            return {"NO FUNCTIONS"};
        }

        Symbols::ResolvedSymbols symbols;
        if (context.format == BinaryFormat::PE) {
            symbols = Symbols::resolveCOFFSymbols(context.sections, context.coffSymbols);
            Symbols::applyFunctionSymbols(functions, symbols);
        }

        Debug::dumpArtifacts(input, irs, symbols, functions);

        return showFunctions(functions, context, symbols.globals);
    }

    std::string DecompileToString(const std::vector<AssemblyInstruction>& input, const DecompileContext& context, size_t maxInstructions) {
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

        return output.str();
    }
}
