#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "DecompilerTypes.h"

namespace Decompiler {
    struct GlobalMemoryRegion {
        uint64_t start = 0;
        uint64_t size = 0;
        uint64_t fileOffset = 0;
        std::string sectionName;
    };

    struct DecompileContext {
        std::vector<SectionInfo> sections;
        const std::vector<uint8_t>* rawData = nullptr;
    };

    struct ReferencedGlobal {
        uint64_t address = 0;
        size_t sizeBytes = 0;
        std::string symbol;
    };
}
