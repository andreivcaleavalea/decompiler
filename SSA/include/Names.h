#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Decompiler {
    std::string ssaBaseName(std::string name);
    std::string ssaVersionName(std::string name, size_t version);
    std::vector<std::string> splitPhiInputs(std::string inputs);
    std::string joinPhiInputs(std::vector<std::string> inputs);
} // namespace Decompiler
