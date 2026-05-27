#pragma once

#include <vector>
#include "IRTypes.h"

namespace Decompiler
{
void recognizeOptimizedDivision(std::vector<IRInstruction>& instructions);
}
