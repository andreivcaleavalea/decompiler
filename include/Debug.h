#pragma once

#include <vector>

#include "Decompiler.h"
#include "IRTypes.h"

namespace Decompiler::Debug {
    void dumpArtifacts(const std::vector<AssemblyInstruction>& input, const std::vector<IRInstruction>& irs);
}
