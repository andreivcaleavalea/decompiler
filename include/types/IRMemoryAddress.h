#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Decompiler {
    struct IRMemoryAddress {
        std::string base;
        std::string index;
        int scale = 1;
        int64_t displacement = 0;
        size_t sizeBytes = 0;
        bool ripRelative = false;
        std::optional<uint64_t> absolute;
    };
}
