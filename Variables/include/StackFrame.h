#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "ControlGraphFlow.h"

namespace Decompiler
{
enum class StackRegionKind {
    SingleVar,
    Array,
    Union,
    Argument,
};

struct StackRegion {
    int64_t offset       = 0;
    uint32_t totalSize   = 0;
    uint32_t elementSize = 0;
    uint32_t count       = 0;
    StackRegionKind kind = StackRegionKind::SingleVar;
    std::string name;
    std::vector<std::string> initValues;
};

struct StackFrame {
    std::vector<StackRegion> regions;
    std::unordered_set<uint64_t> initStoreAddrs;

    const StackRegion* findRegion(int64_t offset, uint32_t sizeBytes) const;

    static StackFrame analyze(const Graph& cfg, bool is64Bit);
};
} // namespace Decompiler
