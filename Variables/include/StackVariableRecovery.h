#pragma once

#include "ControlGraphFlow.h"
#include "StackFrame.h"

namespace Decompiler
{
void recoverStackVariables(Graph& cfg, const StackFrame& stackFrame);
}
