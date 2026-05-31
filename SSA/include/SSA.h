#pragma once

#include "ControlGraphFlow.h"

namespace Decompiler
{
void applySSA(Graph& cfg);
void removeSSA(Graph& cfg);
} // namespace Decompiler
