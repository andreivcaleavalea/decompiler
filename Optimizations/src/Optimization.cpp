#include "Optimizations.h"

#include <vector>

void Decompiler::Optimizations::runAll(DecompilerProgram& program)
{
    for (auto& function : program.functions) {
        propagateCopies(function.graph);
    }
}
