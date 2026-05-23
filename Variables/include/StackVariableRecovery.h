#pragma once

#include "ControlGraphFlow.h"

namespace Decompiler
{
void recoverStackVariables(Graph& cfg, StackFrameLayout layout);
}
