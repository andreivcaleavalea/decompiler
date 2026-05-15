#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "DecompilerContext.h"

namespace Decompiler {
    struct DecompilerFunction;
}

namespace Decompiler::Symbols {
    struct ResolvedSymbols {
        std::unordered_map<uint64_t, std::string> functions;
        std::unordered_map<uint64_t, std::string> globals;
    };

    ResolvedSymbols resolveCOFFSymbols(const std::vector<SectionInfo>& sections, const std::vector<COFFSymbolInfo>& symbols);
    void applyFunctionSymbols(std::vector<DecompilerFunction>& functions, const ResolvedSymbols& symbols);
}
