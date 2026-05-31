#pragma once

#include <vector>

#include "Decompiler.h"
#include "IRInstruction.h"
#include "SymbolTable.h"

namespace Decompiler
{
struct DecompilerFunction;
}

namespace Decompiler::Debug
{
void dumpArtifacts(
      const std::vector<AssemblyInstruction>& input,
      const std::vector<IRInstruction>& irs,
      const Symbols::ResolvedSymbols& symbols,
      const std::vector<DecompilerFunction>& functions);
}
