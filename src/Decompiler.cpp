#include "Decompiler.h"

#include <vector>

#include "Debug.h"
#include "Decompile/Functions.h"
#include "Decompile/Show.h"
#include "IR.h"

namespace Decompiler {
    std::vector<std::string> Decompile(const std::vector<AssemblyInstruction>& input, const DecompileContext& context) {
        const auto irs = asm_to_ir(input);
        Debug::dumpArtifacts(input, irs);
        if (irs.empty()) {
            return {"NO INSTRUCTIONS"};
        }

        const auto functions = findFunctions(irs);
        if (functions.empty()) {
            return {"NO FUNCTIONS"};
        }

        return showFunctions(functions, context);
    }
}
