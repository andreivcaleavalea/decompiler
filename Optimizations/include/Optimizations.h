#pragma once

#include "CopyPropagation.h"
#include "Program.h"
#include "RemoveUnusedGlobals.h"

namespace Decompiler::Optimizations
{
void runAll(DecompilerProgram& program);
}
